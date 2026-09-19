#include "core_recommend.h"
#include "../include/velatime_time.h"
#include "core_task.h"
#include "core_schedule.h"

#include <string.h>
#include <stdio.h>
#include <time.h>

#define URGENCY_OVERDUE   100
#define URGENCY_TODAY     90
#define URGENCY_TOMORROW  70
#define URGENCY_SOON      50
#define URGENCY_WEEK      30
#define URGENCY_FAR       10
#define URGENCY_UNKNOWN   20

#define URGENCY_OVERDUE_MIN (-1440)   /* 逾期一天内仍按重度紧急；更久则降级 */

/*
 * 取当前星期，映射到 VelaTime 的约定：1=周一 … 7=周日。
 * 之前调用方硬编码传 1，导致周末取不到空闲窗口、推荐退化。
 */
int core_recommend_today_weekday(void)
{
  struct timespec ts;
  struct tm now_tm;

  if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
    {
      return 1;
    }

  if (velatime_localtime(ts.tv_sec, &now_tm) == NULL)
    {
      return 1;
    }

  /* tm_wday: 0=周日 … 6=周六 */
  return (now_tm.tm_wday == 0) ? 7 : now_tm.tm_wday;
}

/* 根据任务所属课程，找出该课程当天的教室（同名课程取非空教室） */
static const char *find_course_room(const char *course, int weekday)
{
  int i;

  if (course == NULL || course[0] == '\0')
    {
      return "";
    }

  for (i = 0; i < core_schedule_count(); i++)
    {
      const velatime_course_t *c = core_schedule_get(i);

      if (c != NULL && c->weekday == weekday &&
          strcmp(c->name, course) == 0 && c->room[0] != '\0')
        {
          return c->room;
        }
    }

  return "";
}

static int priority_score(const char *priority)
{
  if (strcmp(priority, "high") == 0)
    {
      return 40;
    }
  else if (strcmp(priority, "medium") == 0)
    {
      return 20;
    }
  return 0;
}

/*
 * 解析 "YYYY-MM-DD" 或 "YYYY-MM-DD HH:MM"。
 * 只给日期时按当天 23:59 处理（作业类截止更符合直觉）。
 * 返回 0 成功，-1 失败；has_time 表示是否显式带时间。
 */
static int parse_deadline(const char *s, struct tm *out, int *has_time)
{
  int y, mo, d, h = 23, mi = 59;

  if (s == NULL)
    {
      return -1;
    }

  if (sscanf(s, "%d-%d-%d %d:%d", &y, &mo, &d, &h, &mi) == 5)
    {
      if (has_time != NULL)
        {
          *has_time = 1;
        }
    }
  else if (sscanf(s, "%d-%d-%d", &y, &mo, &d) == 3)
    {
      h = 23;
      mi = 59;
      if (has_time != NULL)
        {
          *has_time = 0;
        }
    }
  else
    {
      return -1;
    }

  if (mo < 1 || mo > 12 || d < 1 || d > 31 ||
      h < 0 || h > 23 || mi < 0 || mi > 59)
    {
      return -1;
    }

  memset(out, 0, sizeof(*out));
  out->tm_year = y - 1900;
  out->tm_mon  = mo - 1;
  out->tm_mday = d;
  out->tm_hour = h;
  out->tm_min  = mi;
  out->tm_isdst = -1;
  return 0;
}

/*
 * mktime 会把结构体规范化，因此规范化后同一天的两个 tm 一定有相同的
 * 年月日。用这个特性算"日历天差"，不依赖 localtime 实现细节。
 */
static int day_index(const struct tm *t)
{
  return (t->tm_year + 1900) * 1000 + (t->tm_mon + 1) * 40 + t->tm_mday;
}

static int urgency_score(const char *deadline)
{
  struct tm dl_tm;
  struct tm today;
  struct timespec ts;
  time_t now, dl;
  long diff_minutes;
  int has_time;
  int day_diff;

  if (parse_deadline(deadline, &dl_tm, &has_time) < 0)
    {
      return URGENCY_UNKNOWN;   /* 没填截止 / 格式不认识：给中间分 */
    }

  if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
    {
      return URGENCY_UNKNOWN;
    }

  now = ts.tv_sec;
  dl = velatime_mktime(&dl_tm);
  velatime_localtime(now, &today);

  day_diff = day_index(&dl_tm) - day_index(&today);
  diff_minutes = (long)(dl - now) / 60;

  if (diff_minutes <= 0)
    {
      return (diff_minutes >= URGENCY_OVERDUE_MIN) ? URGENCY_OVERDUE
                                                   : URGENCY_TOMORROW;
    }

  if (day_diff <= 0)
    {
      return URGENCY_TODAY;
    }
  if (day_diff == 1)
    {
      return URGENCY_TOMORROW;
    }
  if (day_diff <= 3)
    {
      return URGENCY_SOON;
    }
  if (day_diff <= 7)
    {
      return URGENCY_WEEK;
    }
  return URGENCY_FAR;
}

/* 生成可解释的推荐理由，例如 "今天 18:00 截止 · 120 分钟空档可完成" */
static void build_reason(const velatime_task_t *t, int slot_minutes,
                         char *out, size_t out_size)
{
  struct tm dl_tm;
  struct tm today;
  struct timespec ts;
  time_t now;
  long minutes_left = 0;
  int has_time = 0;
  int mo = 0, d = 0;
  int day_diff = 9999;
  char when[48] = "";

  if (clock_gettime(CLOCK_REALTIME, &ts) == 0)
    {
      now = ts.tv_sec;
      velatime_localtime(now, &today);
    }
  else
    {
      memset(&today, 0, sizeof(today));
    }

  if (parse_deadline(t->deadline, &dl_tm, &has_time) == 0)
    {
      if (clock_gettime(CLOCK_REALTIME, &ts) == 0)
        {
          minutes_left = (long)(velatime_mktime(&dl_tm) - ts.tv_sec) / 60;
        }

      sscanf(t->deadline, "%*d-%d-%d", &mo, &d);
      day_diff = day_index(&dl_tm) - day_index(&today);

      if (minutes_left <= 0)
        {
          snprintf(when, sizeof(when), "已逾期");
        }
      else if (day_diff <= 0)
        {
          /* 今天：离截止不足 2 小时才报具体时间，否则只说到今天为止，
           * 避免"22:00 提醒今天 08:00 截止"这种自相矛盾的说法 */
          if (has_time && minutes_left <= 120)
            {
              snprintf(when, sizeof(when), "今天 %02d:%02d 截止",
                       dl_tm.tm_hour, dl_tm.tm_min);
            }
          else
            {
              snprintf(when, sizeof(when), "今天截止");
            }
        }
      else if (day_diff == 1)
        {
          if (has_time)
            {
              snprintf(when, sizeof(when), "明天 %02d:%02d 截止",
                       dl_tm.tm_hour, dl_tm.tm_min);
            }
          else
            {
              snprintf(when, sizeof(when), "明天截止");
            }
        }
      else if (day_diff <= 3)
        {
          snprintf(when, sizeof(when), "%d 天后截止", day_diff);
        }
      else
        {
          snprintf(when, sizeof(when), "%02d-%02d 截止", mo, d);
        }

      when[sizeof(when) - 1] = '\0';
    }

  if (when[0] == '\0')
    {
      snprintf(out, out_size, "%d 分钟可完成，适合现在开始",
               t->estimated_minutes);
      return;
    }

  if (minutes_left <= 0)
    {
      snprintf(out, out_size, "%s · %d 分钟可完成",
               when, t->estimated_minutes);
    }
  else if (slot_minutes > 0)
    {
      snprintf(out, out_size, "%s · %d 分钟空档", when, slot_minutes);
    }
  else
    {
      snprintf(out, out_size, "%s · 预计 %d 分钟",
               when, t->estimated_minutes);
    }
}

int core_recommend_pick(int weekday, velatime_recomm_book_t *out)
{
  velatime_free_slot_t slots[VELATIME_FREE_SLOT_MAX];
  int slot_count;
  int total;
  int i;
  int best_index = -1;
  int best_score = -1;

  if (!out)
    {
      return 0;
    }

  slot_count = core_schedule_free_slots(weekday, slots, VELATIME_FREE_SLOT_MAX);
  total = core_task_count();

  for (i = 0; i < total; i++)
    {
      const velatime_task_t *t = core_task_get(i);
      int score;
      int match = 0;

      if (!t || t->status != VELATIME_STATUS_WAITING)
        {
          continue;
        }

      if (slot_count > 0 && t->estimated_minutes <= slots[0].minutes)
        {
          match = 50;
        }

      score = urgency_score(t->deadline) + priority_score(t->priority) + match;

      if (score > best_score)
        {
          best_score = score;
          best_index = i;
        }
    }

  if (best_index < 0)
    {
      return 0;
    }

  {
    const velatime_task_t *t = core_task_get(best_index);
    strncpy(out->task_id, t->id, VELATIME_MAX_ID - 1);
    out->task_id[VELATIME_MAX_ID - 1] = '\0';

    strncpy(out->task_title, t->title, VELATIME_MAX_TITLE - 1);
    out->task_title[VELATIME_MAX_TITLE - 1] = '\0';

    strncpy(out->deadline, t->deadline, VELATIME_MAX_DEADLINE - 1);
    out->deadline[VELATIME_MAX_DEADLINE - 1] = '\0';

    if (slot_count > 0)
      {
        strncpy(out->suggested_start, slots[0].start, 7);
        out->suggested_start[7] = '\0';
        out->available_minutes = slots[0].minutes;
      }
    else
      {
        out->available_minutes = 30;
        strncpy(out->suggested_start, "15:20", 7);
        out->suggested_start[7] = '\0';
      }

    {
      const char *room = find_course_room(t->course, weekday);

      strncpy(out->room, room, sizeof(out->room) - 1);
      out->room[sizeof(out->room) - 1] = '\0';
    }

    build_reason(t, out->available_minutes, out->reason,
                 sizeof(out->reason));
  }

  return 1;
}
