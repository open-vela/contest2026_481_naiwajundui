#include "velatime_ui.h"
#include "../core/core_recommend.h"
#include "../core/core_schedule.h"
#include "../core/core_task.h"
#include "../core/core_agent_sync.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/*
 * W1 首页 —— 圆形表盘（454x454 圆屏，为真机烧录准备）
 *
 * 设计定稿（用户 2026-09-16 手稿 + 说明）：
 *   - **不显示"现在推荐"的决策卡片**：那块内容属于通知中心(W5)。
 *   - 只放：当前时间 + 日期 + 一个小信封（有未完成任务时亮红点）。
 *   - 系统**不再主动弹窗**打扰用户；用户自己点信封进通知中心。
 *
 * 圆屏做法（关键）：
 *   真机是圆的，方屏四角会被物理圆框切掉。所以本页自己画一个
 *   "表盘圆"：一个直径 = 屏幕宽度、圆角 = 半径的实心圆对象，
 *   屏幕底色设为纯黑。这样四角天然是黑的，观感就是一块圆表。
 *   所有内容都放在这个圆内，并按半径收敛位置，保证不贴边。
 *
 * 目标屏幕 454x454 圆屏，所有尺寸按运行时实际分辨率按比例计算，
 * 因此 1280x800 模拟器上同样能正常显示。
 */

/*
 * 本页只用文字/强调/危险三色；表盘底色、圆外黑底
 * 都由 ui_theme.c 的共用主题负责。
 * 环形刻度用到的两个色单独定义，避免依赖主题内部宏。
 */
#define CLR_TEXT     0xFFFFFF
#define CLR_MUTED    0x9AA3B4    /* 次要文字：在盘面上保持可读 */
#define CLR_ACCENT   0xFF8A3D
#define CLR_RED      0xFF3B30
#define CLR_RING_BG  0x3A4356    /* 环形刻度底：与盘面同色系、更亮一档 */
#define CLR_RING_FG  0xFF8A3D    /* 环形刻度高亮段 */

/* 诊断开关：为 1 时显示最近识别到的手势方向（确认手势生效后可改 0） */
#define VELATIME_UI_GESTURE_DIAG 1

static lv_obj_t *g_time_label = NULL;
static lv_obj_t *g_date_label = NULL;
static lv_obj_t *g_envelope = NULL;
static lv_obj_t *g_badge = NULL;
static lv_obj_t *g_diag_label = NULL;
static lv_timer_t *g_clock_timer = NULL;

static char g_reminder[192] = "";

/* ---------------------------------------------------------------- */
/* 时间 / 日期                                                        */
/* ---------------------------------------------------------------- */

static void update_clock(void)
{
  static const char *wday[7] = { "日", "一", "二", "三", "四", "五", "六" };
  struct timespec ts;
  struct tm tm_now;
  char time_buf[16];
  char date_buf[32];

  if (clock_gettime(CLOCK_REALTIME, &ts) != 0 ||
      localtime_r(&ts.tv_sec, &tm_now) == NULL)
    {
      return;
    }

  if (g_time_label != NULL)
    {
      snprintf(time_buf, sizeof(time_buf), "%02d:%02d",
               tm_now.tm_hour, tm_now.tm_min);
      lv_label_set_text(g_time_label, time_buf);
    }

  if (g_date_label != NULL)
    {
      snprintf(date_buf, sizeof(date_buf), "%d月%d日 · 周%s",
               tm_now.tm_mon + 1, tm_now.tm_mday, wday[tm_now.tm_wday]);
      lv_label_set_text(g_date_label, date_buf);
    }
}

/* ---------------------------------------------------------------- */
/* 信封红点：有未完成任务就点亮                                          */
/* ---------------------------------------------------------------- */

static int has_pending_work(void)
{
  int i;
  int total = core_task_count();

  for (i = 0; i < total; i++)
    {
      const velatime_task_t *t = core_task_get(i);

      if (t != NULL && t->status != VELATIME_STATUS_DONE)
        {
          return 1;
        }
    }

  return 0;
}

static void update_badge(void)
{
  if (g_badge == NULL)
    {
      return;
    }

  if (has_pending_work())
    {
      lv_obj_remove_flag(g_badge, LV_OBJ_FLAG_HIDDEN);
    }
  else
    {
      lv_obj_add_flag(g_badge, LV_OBJ_FLAG_HIDDEN);
    }
}

void velatime_ui_set_reminder(const char *text)
{
  if (text == NULL)
    {
      return;
    }

  strncpy(g_reminder, text, sizeof(g_reminder) - 1);
  g_reminder[sizeof(g_reminder) - 1] = '\0';

  /* 不自动弹窗：只点亮红点，用户自己进通知中心看 */
  update_badge();
}

void velatime_ui_home_refresh(void)
{
  update_badge();
}

static void clock_timer_cb(lv_timer_t *timer)
{
  (void)timer;
  update_clock();
}

/* ---------------------------------------------------------------- */
/* 手势（LVGL 原生）                                                  */
/* ---------------------------------------------------------------- */

static void enable_gesture_bubble(lv_obj_t *obj)
{
  if (obj != NULL)
    {
      lv_obj_add_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
    }
}

static void on_home_gesture(lv_event_t *e)
{
  lv_indev_t *indev = lv_indev_active();
  lv_dir_t dir;

  (void)e;

  if (indev == NULL)
    {
      return;
    }

  dir = lv_indev_get_gesture_dir(indev);

#if VELATIME_UI_GESTURE_DIAG
  if (g_diag_label != NULL)
    {
      const char *name = "NONE";

      switch (dir)
        {
          case LV_DIR_LEFT:   name = "LEFT";   break;
          case LV_DIR_RIGHT:  name = "RIGHT";  break;
          case LV_DIR_TOP:    name = "UP";     break;
          case LV_DIR_BOTTOM: name = "DOWN";   break;
          default:            break;
        }

      lv_label_set_text_fmt(g_diag_label, "gesture: %s", name);
    }
#endif

  switch (dir)
    {
      case LV_DIR_TOP:    velatime_ui_popup_show();    break;  /* 上滑 -> 通知中心 */
      case LV_DIR_LEFT:   velatime_ui_tasks_show();    break;  /* 左滑 -> 任务列表 */
      case LV_DIR_RIGHT:  velatime_ui_schedule_show(); break;  /* 右滑 -> 课程表 */
      default: break;
    }
}

static void on_envelope_click(lv_event_t *e)
{
  (void)e;
  velatime_ui_popup_show();
}

/* ---------------------------------------------------------------- */
/* 构造                                                              */
/* ---------------------------------------------------------------- */

void velatime_ui_init(void)
{
}

/*
 * 小信封 + 红点。
 * 位置用圆的半径约束，保证整体（含对角线与红点）落在圆内：
 *   dist(圆心 -> 信封中心) + 信封半对角线 <= 半径 * 0.90
 */
static void build_envelope(lv_obj_t *scr, int d)
{
  lv_obj_t *icon;
  lv_obj_t *flap;
  int box = d / 7;
  int badge = d / 26;
  int r = d / 2;
  int off_x;
  int off_y;

  if (box < 34)
    {
      box = 34;
    }
  if (badge < 7)
    {
      badge = 7;
    }

  /* 放在圆内右上方：横向 34% 半径、纵向 46% 半径。
     此时 圆心到信封中心距离 + 信封半对角线 ≈ 0.62r，远小于半径，安全。 */
  off_x = r * 34 / 100;
  off_y = -(r * 46 / 100);

  g_envelope = lv_button_create(scr);
  lv_obj_set_size(g_envelope, box, box);
  lv_obj_align(g_envelope, LV_ALIGN_CENTER, off_x, off_y);
  lv_obj_set_style_bg_opa(g_envelope, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(g_envelope, 0, 0);
  lv_obj_set_style_shadow_width(g_envelope, 0, 0);
  lv_obj_set_style_pad_all(g_envelope, 0, 0);
  enable_gesture_bubble(g_envelope);

  /* 信封主体：细边圆角矩形 */
  icon = lv_obj_create(g_envelope);
  lv_obj_set_size(icon, box * 3 / 4, box / 2);
  lv_obj_center(icon);
  lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(icon, 2, 0);
  lv_obj_set_style_border_color(icon, lv_color_hex(CLR_MUTED), 0);
  lv_obj_set_style_radius(icon, 4, 0);
  lv_obj_remove_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE);
  enable_gesture_bubble(icon);

  /* 折角：一条短横线 */
  flap = lv_obj_create(icon);
  lv_obj_set_size(flap, box * 3 / 4 - 10, 2);
  lv_obj_align(flap, LV_ALIGN_TOP_MID, 0, box / 8);
  lv_obj_set_style_bg_color(flap, lv_color_hex(CLR_MUTED), 0);
  lv_obj_set_style_bg_opa(flap, LV_OPA_60, 0);
  lv_obj_set_style_border_width(flap, 0, 0);
  lv_obj_remove_flag(flap, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(flap, LV_OBJ_FLAG_CLICKABLE);
  enable_gesture_bubble(flap);

  /* 红点 */
  g_badge = lv_obj_create(g_envelope);
  lv_obj_set_size(g_badge, badge, badge);
  lv_obj_align(g_badge, LV_ALIGN_TOP_RIGHT, 2, -2);
  lv_obj_set_style_radius(g_badge, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(g_badge, lv_color_hex(CLR_RED), 0);
  lv_obj_set_style_bg_opa(g_badge, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(g_badge, 0, 0);
  lv_obj_remove_flag(g_badge, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(g_badge, LV_OBJ_FLAG_CLICKABLE);
  enable_gesture_bubble(g_badge);

  lv_obj_add_event_cb(g_envelope, on_envelope_click, LV_EVENT_CLICKED, NULL);
}

void velatime_ui_home_show(void)
{
  lv_obj_t *scr = lv_obj_create(NULL);
  int w = 0;
  int h = 0;
  int d;
  int r;

  velatime_ui_screen_size(&w, &h);
  if (w <= 0)
    {
      w = VELATIME_UI_SCREEN_W;
    }
  if (h <= 0)
    {
      h = VELATIME_UI_SCREEN_H;
    }

  d = (w < h) ? w : h;
  r = d / 2;

  /* 共用主题：纯黑底（圆外）+ 表盘圆 + 中文字库 + 关滚动。
     五个页面用同一套，圆形观感一致。 */
  velatime_ui_style_screen(scr);

  /*
   * 排版要点：
   *   - 时间与日期作为一组**整体居中**，视觉锚点在圆心；
   *   - 时间字号远大于日期，拉开层次（日期用黄色系以外的灰，避免抢焦点）；
   *   - 盘内加一道细环形刻度，强化"这是一块表"的观感；
   *   - 信封贴圆内右上方，动作区在顶部，不与时间抢位置。
   */

  /* 环形刻度：只保留外圈一小段，像表盘的刻度环 */
  {
    lv_obj_t *ring = lv_arc_create(scr);
    int size = d * 88 / 100;

    lv_obj_set_size(ring, size, size);
    lv_obj_center(ring);

    /*
     * 只画圆环，不要旋钮、不要自动可点：
     * 底环整圈（细、暗），高亮段只留右上一段（粗、橙）。
     * 整圈都高亮会太抢眼，只留一段更像表盘的"刻度弧"。
     */
    lv_arc_set_rotation(ring, 0);
    lv_arc_set_bg_angles(ring, 0, 360);
    lv_arc_set_angles(ring, 0, 70);
    lv_obj_remove_style(ring, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ring, 1, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ring, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ring, lv_color_hex(CLR_RING_BG), LV_PART_MAIN);
    lv_obj_set_style_arc_color(ring, lv_color_hex(CLR_RING_FG),
                               LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(ring, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(ring, LV_OPA_80, LV_PART_INDICATOR);
    lv_obj_add_flag(ring, LV_OBJ_FLAG_GESTURE_BUBBLE);
  }

  /* 时间：表盘最大视觉重点，略高于圆心 */
  g_time_label = lv_label_create(scr);
  lv_obj_set_style_text_font(g_time_label,
                             (d > 600) ? &lv_font_montserrat_32
                                       : &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(g_time_label, lv_color_hex(CLR_TEXT), 0);
  lv_obj_align(g_time_label, LV_ALIGN_CENTER, 0, -(r * 10 / 100));

  /* 日期 + 星期：紧跟时间下方，同一视觉组 */
  g_date_label = lv_label_create(scr);
  lv_obj_set_style_text_color(g_date_label, lv_color_hex(CLR_MUTED), 0);
  lv_obj_align(g_date_label, LV_ALIGN_CENTER, 0, r * 16 / 100);

  /* 信封 + 红点（点它进通知中心） */
  build_envelope(scr, d);

  /* 常驻翻页栏（任务 · 首页 · 课表）：手势失效时靠它切页 */
  velatime_ui_build_nav(scr, VELATIME_PAGE_HOME);

#if VELATIME_UI_GESTURE_DIAG
  /* 诊断行：放到最顶部（细小、不干扰主体），确认手势是否生效用 */
  g_diag_label = lv_label_create(scr);
  lv_label_set_text(g_diag_label, "gesture: none");
  lv_obj_set_style_text_font(g_diag_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(g_diag_label, lv_color_hex(CLR_MUTED), 0);
  lv_obj_align(g_diag_label, LV_ALIGN_TOP_MID, 0, r * 20 / 100);
#else
  g_diag_label = NULL;
#endif

  /* 屏幕负责接收冒泡上来的手势 */
  lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
  enable_gesture_bubble(scr);
  lv_obj_add_event_cb(scr, on_home_gesture, LV_EVENT_GESTURE, NULL);

  /* 时钟每秒刷新；只建一个定时器，避免反复进首页时泄漏 */
  if (g_clock_timer == NULL)
    {
      g_clock_timer = lv_timer_create(clock_timer_cb, 1000, NULL);
    }

  update_clock();
  update_badge();
  lv_scr_load(scr);
}
