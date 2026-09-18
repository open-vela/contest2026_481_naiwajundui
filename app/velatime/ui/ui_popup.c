#include "velatime_ui.h"
#include "../core/core_recommend.h"
#include "../core/core_task.h"
#include "../core/core_agent_sync.h"

#include <stdio.h>

/*
 * W5 通知中心（2026-09-18 版式 H3，用户选定）
 * ==================================================================
 * ■ 定位修正（用户明确指出）：
 *   通知 = 告诉你「现在该去做什么」。
 *   它【不是】任务管理面板 —— 不在这里改任务状态、不出现"完成/开始"按钮。
 *   看完点一下或左滑即可关闭。
 *
 * ■ 结构照抄 esp32-c3-mini / src/ui/ui.c 的 ui_alertPanel（第 2594-2628 行）：
 *     bg     0x000000 + bg_opa 240
 *     border 0xFFFFFF + border_opa 240 + border_width 1
 *     图标在左 (LV_ALIGN_LEFT_MID) + 文字在右
 *     无任何按钮
 *   行为（第 641-656 行）：
 *     LV_EVENT_CLICKED     -> 隐藏
 *     GESTURE LV_DIR_LEFT  -> 隐藏
 *   本工程保留原有的"下滑关闭"，三者都回 W1。
 *
 * ■ 外观改动（用户要求"圆形边框"）：
 *   C3 是 200x55 直角矩形；这里改为 400x130 药丸形（radius = H/2）。
 *   底色/边框颜色与不透明度仍沿用 C3 原值。
 *
 * ■ 尺寸换算 240 -> 454：
 *   C3 图标约 26 -> 48；C3 文字宽 142 -> 240
 */

/* 尺寸（2026-09-18 修正为自适应；比例来自 454 屏实测）
   面板 400/454=881‰  高 130/454=286‰  图标 48/454=106‰
   文字宽 240/454=529‰  文字x 94/454=207‰  图标x 36/454=79‰ */
#define W5_PANEL_W_PERMILLE  881
#define W5_PANEL_H_PERMILLE  286
#define W5_ICON_D_PERMILLE   106
#define W5_TEXT_W_PERMILLE   529
#define W5_TEXT_X_PERMILLE   207
#define W5_ICON_X_PERMILLE    79
#define W5_CLR_ICON  0x2196F3

/* 关闭通知，回 W1（点击 / 左滑 / 下滑 都走这里） */
static void on_notify_dismiss(lv_event_t *e)
{
  (void)e;
  velatime_ui_home_show();
}

/*
 * 手势关闭：与 C3 的 ui_event_alertPanel 一致（左滑关闭），
 * 并保留本工程原有的"下滑关闭"（用户从 W1 点信封进入，下滑回去对称）。
 */
static void on_notify_gesture(lv_event_t *e)
{
  lv_indev_t *indev = lv_indev_active();
  lv_dir_t dir;

  (void)e;

  if (indev == NULL)
    {
      return;
    }

  dir = lv_indev_get_gesture_dir(indev);

  if (dir == LV_DIR_LEFT || dir == LV_DIR_BOTTOM)
    {
      velatime_ui_home_show();
    }
}

void velatime_ui_popup_show(void)
{
  velatime_ui_set_page_index(VELATIME_PAGE_IDX_NOTIFY);
  velatime_recomm_book_t rec;
  char reminder[192];
  int has_rec = core_recommend_pick(core_recommend_today_weekday(), &rec);
  lv_obj_t *scr;
  lv_obj_t *panel;
  lv_obj_t *icon;
  lv_obj_t *text;
  int sw5 = 0, sh5 = 0, d5;
  int p_w, p_h, i_d, t_w, t_x, i_x;

  if (has_rec &&
      core_agent_reminder_local(rec.task_title, rec.reason,
                                rec.suggested_start,
                                reminder, sizeof(reminder)) != 0)
    {
      has_rec = 0;
    }

  if (!has_rec)
    {
      snprintf(reminder, sizeof(reminder), "暂无待办任务");
    }

  velatime_ui_screen_size(&sw5, &sh5);
  d5 = (sw5 < sh5) ? sw5 : sh5;
  p_w = d5 * W5_PANEL_W_PERMILLE / 1000;
  p_h = d5 * W5_PANEL_H_PERMILLE / 1000;
  i_d = d5 * W5_ICON_D_PERMILLE / 1000;
  t_w = d5 * W5_TEXT_W_PERMILLE / 1000;
  t_x = d5 * W5_TEXT_X_PERMILLE / 1000;
  i_x = d5 * W5_ICON_X_PERMILLE / 1000;

  /* ---- 屏幕：纯黑（C3 的做法）---- */
  scr = lv_obj_create(NULL);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  /* 屏幕底色与其它页面统一为 CLR_BG（原来是纯黑，与圆屏底色不一致） */
  lv_obj_set_style_bg_color(scr, lv_color_hex(CLR_BG), 0);
  lv_obj_set_style_bg_opa(scr, 255, 0);
  lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(scr, on_notify_dismiss, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(scr, on_notify_gesture, LV_EVENT_GESTURE, NULL);

  /* ---- 药丸面板（C3 的 ui_alertPanel 底色/边框原值）---- */
  panel = lv_obj_create(scr);
  lv_obj_set_size(panel, p_w, p_h);
  lv_obj_set_align(panel, LV_ALIGN_CENTER);
  lv_obj_set_style_radius(panel, p_h / 2, 0);          /* = H/2 → 药丸 */
  /*
   * 面板底色与屏幕同色（CLR_BG），靠 1px 白边框区分 —— 这是 C3 的结构
   * （C3 屏幕与面板都是纯黑）。若照抄纯黑，在 CLR_BG 圆屏上会是一块黑饼。
   */
  lv_obj_set_style_bg_color(panel, lv_color_hex(CLR_BG), 0);
  lv_obj_set_style_bg_opa(panel, 255, 0);
  lv_obj_set_style_border_color(panel, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_opa(panel, 240, 0);             /* C3 原值 */
  lv_obj_set_style_border_width(panel, 1, 0);             /* C3 原值 */
  lv_obj_set_style_shadow_width(panel, 0, 0);
  lv_obj_set_style_pad_all(panel, 0, 0);
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(panel, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(panel, on_notify_dismiss, LV_EVENT_CLICKED, NULL);

  /* ---- 左侧图标（C3 用 PNG，本工程无图片资源，用几何圆占位）---- */
  icon = lv_obj_create(panel);
  lv_obj_set_size(icon, i_d, i_d);
  lv_obj_align(icon, LV_ALIGN_LEFT_MID, i_x, 0);
  lv_obj_set_style_radius(icon, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(icon, lv_color_hex(W5_CLR_ICON), 0);
  lv_obj_set_style_bg_opa(icon, 255, 0);
  lv_obj_set_style_border_width(icon, 0, 0);
  lv_obj_remove_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE);

  /* ---- 右侧文字（C3 的 ui_alertText）---- */
  text = lv_label_create(panel);
  lv_obj_set_width(text, t_w);
  lv_obj_set_height(text, LV_SIZE_CONTENT);
  lv_obj_align(text, LV_ALIGN_LEFT_MID, t_x, 0);
  lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
  lv_label_set_text(text, reminder);
  lv_obj_set_style_text_font(text, VELATIME_FONT_TASKMETA, 0);
  lv_obj_set_style_text_color(text, lv_color_hex(0xFFFFFF), 0);

  lv_scr_load(scr);
}
