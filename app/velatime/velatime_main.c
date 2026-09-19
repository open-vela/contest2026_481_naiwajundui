#include <nuttx/config.h>
#include <unistd.h>
#include <sys/boardctl.h>

#include <lvgl/lvgl.h>

#include "ui/velatime_ui.h"
#include "include/velatime_time.h"
#include "core/core_task.h"
#include "core/core_schedule.h"
#include "core/core_recommend.h"
#include "core/core_agent_sync.h"

#include <stdio.h>
#include <time.h>

#undef NEED_BOARDINIT

#if defined(CONFIG_BOARDCTL) && !defined(CONFIG_NSH_ARCHINIT)
#  define NEED_BOARDINIT 1
#endif

static void import_mock_tasks(void)
{
  extern velatime_task_t mock_tasks[5];
  int i;

  for (i = 0; i < 3; i++)
    {
      core_task_add(&mock_tasks[i]);
    }
}

/* 端侧主动提醒：不依赖网络和模型，启动后立刻给出"现在该做什么"。
   若稍后 Agent 通过 heartbeat 写回 REMINDER.txt，会用模型版本覆盖它。 */
static void publish_local_reminder(void)
{
  velatime_recomm_book_t rec;
  struct timespec ts;
  struct tm now_tm;
  int weekday = 1;
  char text[192];

  if (clock_gettime(CLOCK_REALTIME, &ts) == 0 &&
      velatime_localtime(ts.tv_sec, &now_tm) != NULL)
    {
      /* tm_wday: 0=周日 … 6=周六；VelaTime 用 1=周一 … 7=周日 */
      weekday = (now_tm.tm_wday == 0) ? 7 : now_tm.tm_wday;
    }

  if (!core_recommend_pick(weekday, &rec))
    {
      printf("VelaTime: no recommendation yet, reminder skipped\n");
      return;
    }

  if (core_agent_reminder_local(rec.task_title, rec.reason,
                                rec.suggested_start, text, sizeof(text)) == 0)
    {
      velatime_ui_set_reminder(text);
    }
}

static void agent_sync_timer_cb(lv_timer_t *timer)
{
  char reminder[192];
  int result;

  (void)timer;
  result = core_agent_sync_if_changed();
  if (result > 0)
    {
      velatime_ui_home_refresh();
      /* 任务变了：重新武装 heartbeat，让 Agent 下一次主动检查 */
      core_agent_reminder_publish(core_task_count());
    }

  /* 主动提醒：Agent 写回新建议就显示到界面 */
  if (core_agent_reminder_check(core_task_count(), reminder,
                                sizeof(reminder)) > 0)
    {
      printf("VelaTime: proactive reminder: %s\n", reminder);
      velatime_ui_set_reminder(reminder);
    }
}

int main(int argc, FAR char *argv[])
{
  lv_nuttx_dsc_t info;
  lv_nuttx_result_t result;
  int imported;

  if (lv_is_initialized())
    {
      return -1;
    }

#ifdef NEED_BOARDINIT
  boardctl(BOARDIOC_INIT, 0);
#endif

  lv_init();

  core_task_init();

  core_agent_skill_install();
  imported = core_agent_sync_from_file();
  if (imported <= 0)
    {
      printf("VelaTime: no agent tasks imported, using mock tasks\n");
      import_mock_tasks();
    }
  else
    {
      printf("VelaTime: imported %d agent task(s)\n", imported);
    }

  /* 武装主动提醒：让 Agent 的 heartbeat 定时器知道有待办要处理 */
  core_agent_reminder_publish(core_task_count());

  core_schedule_init();

  lv_nuttx_dsc_init(&info);

#ifdef CONFIG_LV_USE_NUTTX_LCD
  info.fb_path = "/dev/lcd0";
#endif

#ifdef CONFIG_INPUT_TOUCHSCREEN
  info.input_path = "/dev/input0";
#endif

  lv_nuttx_init(&info, &result);

  if (result.disp == NULL)
    {
      return 1;
    }

  velatime_ui_init();
  velatime_ui_home_show();

  /* 启动即给出一次端侧主动提醒（演示/离线都能看到效果） */
  publish_local_reminder();

  if (lv_timer_create(agent_sync_timer_cb, 1000, NULL) == NULL)
    {
      printf("VelaTime: cannot create agent sync timer\n");
    }

  while (1)
    {
      uint32_t idle;
      idle = lv_timer_handler();
      idle = idle ? idle : 1;
      usleep(idle * 1000);
    }

  lv_nuttx_deinit(&result);
  lv_deinit();
  return 0;
}
