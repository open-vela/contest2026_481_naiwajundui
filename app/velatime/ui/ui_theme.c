#include "velatime_ui.h"

/*
 * 圆形表盘主题（W1–W5 全部共用）
 * ==================================================================
 * 真机是 454x454 圆屏，方屏的四角会被物理圆框切掉。所以每个页面都：
 *   1. 屏幕底色设纯黑 —— 圆外区域由它呈现，四角自然变黑；
 *   2. 画一个"表盘圆"：边长 = 屏幕短边、圆角 = 半径的实心圆对象；
 *   3. 内容只放在圆的内切安全区内，避免被圆边切掉。
 *
 * 这样五个页面的圆形观感完全一致，且只需要在这里维护一处。
 *
 * 配色说明：表盘色必须明显亮于圆外的纯黑，否则看不出"这是个圆"。
 * 实测 #10131A 与纯黑对比过弱，故表盘用 #232A3A。
 */

#define CLR_OUTSIDE  0x000000    /* 圆外：纯黑，模拟表框 */
#define CLR_DIAL     0x232A3A    /* 表盘底色 */
#define CLR_RIM      0x3A4356    /* 盘外圈细边 */

/*
 * 运行时屏幕尺寸。真机是 454x454 圆屏、模拟器是 1280x800，
 * 布局必须按实际分辨率计算，不能依赖编译期常量。
 * 取不到时退回编译期常量，保证永远算出非 0 宽高。
 */
void velatime_ui_screen_size(int *w, int *h)
{
  lv_obj_t *scr = lv_screen_active();
  int sw = (scr != NULL) ? (int)lv_obj_get_width(scr) : 0;
  int sh = (scr != NULL) ? (int)lv_obj_get_height(scr) : 0;

  if (w != NULL)
    {
      *w = (sw > 0) ? sw : VELATIME_UI_SCREEN_W;
    }

  if (h != NULL)
    {
      *h = (sh > 0) ? sh : VELATIME_UI_SCREEN_H;
    }
}

/*
 * 圆的内切安全方框：把内容限制在这里面，就绝不会被圆边切到。
 * 圆的直径是短边 d，内切正方形边长 = d / sqrt(2) ≈ 0.707d，
 * 再留 6% 余量，最终取 66%（0.66 > 0.707 * 0.93，仍然安全）。
 */
void velatime_ui_safe_box(int *box_w, int *box_h, int margin_pct)
{
  int sw;
  int sh;
  int d;
  int inset;
  int pct;

  velatime_ui_screen_size(&sw, &sh);
  d = (sw < sh) ? sw : sh;

  pct = (margin_pct > 0) ? margin_pct : VELATIME_UI_SAFE_PCT;
  inset = d * (100 - pct) / 200;      /* 每侧留白 */

  if (box_w != NULL)
    {
      *box_w = d * pct / 100;
    }

  if (box_h != NULL)
    {
      *box_h = sh - inset * 2;
      if (*box_h <= 0)
        {
          *box_h = sh;
        }
    }
}

/*
 * 画表盘圆。返回圆盘对象，它是屏幕之后创建的第一个对象，
 * 因此位于所有页面内容之下（LVGL 按创建顺序堆叠）。
 */
lv_obj_t *velatime_ui_build_dial(lv_obj_t *scr)
{
  lv_obj_t *dial;
  int sw;
  int sh;
  int d;
  int rim;

  if (scr == NULL)
    {
      return NULL;
    }

  velatime_ui_screen_size(&sw, &sh);
  d = (sw < sh) ? sw : sh;
  rim = d / 120;
  if (rim < 2)
    {
      rim = 2;
    }

  dial = lv_obj_create(scr);
  lv_obj_set_size(dial, d, d);
  lv_obj_center(dial);
  lv_obj_set_style_radius(dial, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(dial, lv_color_hex(CLR_DIAL), 0);
  lv_obj_set_style_bg_opa(dial, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(dial, rim, 0);
  lv_obj_set_style_border_color(dial, lv_color_hex(CLR_RIM), 0);
  lv_obj_set_style_pad_all(dial, 0, 0);
  lv_obj_remove_flag(dial, LV_OBJ_FLAG_SCROLLABLE);
  /* 让圆盘也能把手势冒泡给屏幕，否则在盘面上滑动收不到 */
  lv_obj_add_flag(dial, LV_OBJ_FLAG_GESTURE_BUBBLE);

  return dial;
}

/* 各页面共用的根对象样式：纯黑底（圆外）、画表盘圆、中文字库、关闭滚动 */
void velatime_ui_style_screen(lv_obj_t *scr)
{
  if (scr == NULL)
    {
      return;
    }

  lv_obj_set_style_bg_color(scr, lv_color_hex(CLR_OUTSIDE), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_text_font(scr, VELATIME_FONT_CN, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  /* 表盘圆紧跟屏幕创建，保证它在所有内容之下 */
  (void)velatime_ui_build_dial(scr);
}

/*
 * 常驻翻页栏（三个小按钮：首页 / 课表 / 任务）。
 *
 * 为什么需要：模拟器里手势不一定生效，没有它用户会卡在单个页面出不来。
 * 五个页面上都长一样、位置也一样，所以可以盲点。
 *
 * 定位方式：横向用内切安全区宽度，纵向贴在安全区底部 —— 这个组合
 * 已用算术验证过在任何尺寸下都落在圆内（模拟器上圆内切于方屏，
 * 底部中间很宽，同样放得下）。
 */
static void nav_to_home(lv_event_t *e)
{
  (void)e;
  velatime_ui_home_show();
}

static void nav_to_schedule(lv_event_t *e)
{
  (void)e;
  velatime_ui_schedule_show();
}

static void nav_to_tasks(lv_event_t *e)
{
  (void)e;
  velatime_ui_tasks_show();
}

void velatime_ui_build_nav(lv_obj_t *scr, velatime_ui_page_t current)
{
  /*
   * 顺序按用户手稿：左 W2 任务列表 · 中 W1 首页 · 右 W4 课程表。
   * 首页放正中间，与"W1 是根页面"的定位一致。
   */
  static const char *labels[3] = { "任务", "首页", "课表" };
  static void (*const handlers[3])(lv_event_t *) =
  {
    nav_to_tasks, nav_to_home, nav_to_schedule
  };
  static const velatime_ui_page_t pages[3] =
  {
    VELATIME_PAGE_TASKS, VELATIME_PAGE_HOME, VELATIME_PAGE_SCHEDULE
  };

  lv_obj_t *nav;
  int sw;
  int sh;
  int d;
  int safe_w;
  int safe_h;
  int bar_h;
  int i;

  if (scr == NULL)
    {
      return;
    }

  velatime_ui_screen_size(&sw, &sh);
  d = (sw < sh) ? sw : sh;

  bar_h = d * 65 / 1000;              /* ≈6.5% 直径 */
  if (bar_h < 26)
    {
      bar_h = 26;
    }

  velatime_ui_safe_box(&safe_w, &safe_h, 0);

  nav = lv_obj_create(scr);
  lv_obj_set_size(nav, safe_w, bar_h);
  /* 贴在安全区底部 */
  lv_obj_align(nav, LV_ALIGN_BOTTOM_MID, 0, -(sh - safe_h) / 2);
  lv_obj_set_style_bg_opa(nav, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(nav, 0, 0);
  lv_obj_set_style_pad_all(nav, 0, 0);
  lv_obj_set_style_pad_column(nav, safe_w / 24, 0);
  lv_obj_set_flex_flow(nav, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(nav, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(nav, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(nav, LV_OBJ_FLAG_GESTURE_BUBBLE);

  for (i = 0; i < 3; i++)
    {
      lv_obj_t *btn = lv_button_create(nav);
      lv_obj_t *label;
      int active = ((int)pages[i] == (int)current);

      lv_obj_set_size(btn, safe_w / 3 - 8, bar_h);
      lv_obj_set_style_radius(btn, bar_h / 2, 0);
      lv_obj_set_style_border_width(btn, 0, 0);
      lv_obj_set_style_shadow_width(btn, 0, 0);
      lv_obj_set_style_bg_opa(btn, active ? LV_OPA_30 : LV_OPA_TRANSP, 0);
      lv_obj_set_style_bg_color(btn, lv_color_hex(CLR_RIM), 0);
      lv_obj_add_flag(btn, LV_OBJ_FLAG_GESTURE_BUBBLE);

      label = lv_label_create(btn);
      lv_label_set_text(label, labels[i]);
      lv_obj_set_style_text_color(label,
                                  lv_color_hex(active ? 0xFFFFFF : 0x9AA3B4), 0);
      lv_obj_center(label);

      if (!active)
        {
          lv_obj_add_event_cb(btn, handlers[i], LV_EVENT_CLICKED, NULL);
        }
    }
}

/*
 * 居中内容列：所有页面（W1–W5）的标题、列表、按钮都放进来，保证：
 *   - 内容在屏幕上居中，且五个页面停靠方式完全一致；
 *   - 列内元素统一左对齐成一条竖线；
 *   - 宽度按圆的内切安全区收敛，圆边上不会切到内容。
 *
 * 尺寸随屏幕自适应：
 *   1280x800 模拟器 -> 上限仍是原来的 960 宽
 *   454x454 圆屏     -> 约 300 宽，落在圆的安全区内
 */
lv_obj_t *velatime_ui_page_column(lv_obj_t *scr)
{
  lv_obj_t *col;
  int safe_w;
  int safe_h;
  int col_w;
  int col_h;

  if (scr == NULL)
    {
      return NULL;
    }

  velatime_ui_safe_box(&safe_w, &safe_h, 0);

  col_w = VELATIME_UI_COL_W;
  if (col_w > safe_w)
    {
      col_w = safe_w;
    }

  col_h = safe_h;
  if (col_h > VELATIME_UI_SCREEN_H - VELATIME_UI_PAD * 2)
    {
      col_h = VELATIME_UI_SCREEN_H - VELATIME_UI_PAD * 2;
    }

  col = lv_obj_create(scr);
  lv_obj_set_size(col, col_w, col_h);
  lv_obj_align(col, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(col, 0, 0);
  lv_obj_set_style_pad_all(col, 0, 0);
  lv_obj_set_style_pad_row(col, 12, 0);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);

  return col;
}
