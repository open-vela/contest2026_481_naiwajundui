#include "velatime_ui.h"
#include "../core/core_recommend.h"
#include "../core/core_task.h"
#include "../core/core_agent_sync.h"

#include <stdio.h>

/* 通知中心（W5）：展示 Agent 或端侧给出的提醒，并可直接操作。
   设计变更（2026-09-16）：不再是"主动弹窗"打断用户，而是用户从 W1 点信封
   主动进入。系统只负责点亮 W1 的红点，看不看由用户自己决定。 */

static void on_close_click(lv_event_t *e)
{
  (void)e;
  velatime_ui_home_show();
}

/* 下滑关闭通知中心，回到 W1（与"点信封进入"对称） */
static void on_notify_gesture(lv_event_t *e)
{
  lv_indev_t *indev = lv_indev_active();

  (void)e;

  if (indev != NULL && lv_indev_get_gesture_dir(indev) == LV_DIR_BOTTOM)
    {
      velatime_ui_home_show();
    }
}

static void on_start_click(lv_event_t *e)
{
  velatime_recomm_book_t rec;
  (void)e;

  if (core_recommend_pick(core_recommend_today_weekday(), &rec))
    {
      /* 提醒弹窗里的"现在开始"直接把该任务置为进行中 */
      core_task_set_status(rec.task_id, VELATIME_STATUS_DOING);
    }

  velatime_ui_home_show();
}

void velatime_ui_popup_show(void)
{
  velatime_recomm_book_t rec;
  char reminder[192];
  int has_rec = core_recommend_pick(core_recommend_today_weekday(), &rec);

  lv_obj_t *scr = lv_obj_create(NULL);
  lv_obj_t *col;

  velatime_ui_style_screen(scr);
  col = velatime_ui_page_column(scr);

  /* 与其它页面统一：放进居中内容列，列内铺满宽度 */
  lv_obj_t *card = lv_obj_create(col);
  lv_obj_set_size(card, LV_PCT(100), 360);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x1C2130), 0);
  lv_obj_set_style_radius(card, 16, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_pad_all(card, VELATIME_UI_PAD_CARD, 0);
  lv_obj_set_style_pad_row(card, 16, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_EVENLY,
                        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(card);
  lv_label_set_text(title, "通知中心");
  lv_obj_set_style_text_color(title, lv_color_hex(0xFF8A3D), 0);

  lv_obj_t *task = lv_label_create(card);
  lv_label_set_text(task, has_rec ? rec.task_title : "暂无待办任务");
  lv_obj_set_style_text_color(task, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_width(task, LV_PCT(100));
  lv_label_set_long_mode(task, LV_LABEL_LONG_WRAP);

  lv_obj_t *detail = lv_label_create(card);
  if (has_rec &&
      core_agent_reminder_local(rec.task_title, rec.reason,
                                rec.suggested_start,
                                reminder, sizeof(reminder)) == 0)
    {
      lv_label_set_text(detail, reminder);
    }
  else
    {
      lv_label_set_text(detail, "添加任务后我会主动提醒你");
    }
  lv_obj_set_style_text_color(detail, lv_color_hex(0x8890A0), 0);
  lv_obj_set_width(detail, LV_PCT(100));
  lv_label_set_long_mode(detail, LV_LABEL_LONG_WRAP);

  /* 有教室就补一行，方便直接出门上课 */
  if (has_rec && rec.room[0] != '\0')
    {
      lv_obj_t *where = lv_label_create(card);
      lv_label_set_text_fmt(where, "地点：%s", rec.room);
      lv_obj_set_style_text_color(where, lv_color_hex(0x00D26A), 0);
      lv_obj_set_width(where, LV_PCT(100));
      lv_label_set_long_mode(where, LV_LABEL_LONG_DOT);
    }

  lv_obj_t *row = lv_obj_create(card);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_pad_column(row, 24, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *btn_start = lv_button_create(row);
  lv_obj_set_size(btn_start, VELATIME_UI_BTN_W, VELATIME_UI_BTN_H);
  lv_obj_t *start_label = lv_label_create(btn_start);
  lv_label_set_text(start_label, "现在开始");
  lv_obj_center(start_label);
  lv_obj_add_event_cb(btn_start, on_start_click, LV_EVENT_CLICKED, NULL);

  lv_obj_t *btn_close = lv_button_create(row);
  lv_obj_set_size(btn_close, VELATIME_UI_BTN_W, VELATIME_UI_BTN_H);
  lv_obj_t *close_label = lv_label_create(btn_close);
  lv_label_set_text(close_label, "稍后再说");
  lv_obj_center(close_label);
  lv_obj_add_event_cb(btn_close, on_close_click, LV_EVENT_CLICKED, NULL);

  /* 底部返回：与其它页面一致，贴在内容列左下 */
  lv_obj_t *btn_back = lv_button_create(col);
  lv_obj_set_size(btn_back, VELATIME_UI_BTN_W, VELATIME_UI_BTN_H);
  lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_t *back_label = lv_label_create(btn_back);
  lv_label_set_text(back_label, "返回首页");
  lv_obj_center(back_label);
  lv_obj_add_event_cb(btn_back, on_close_click, LV_EVENT_CLICKED, NULL);

  /* 手势：下滑关闭（与 W1 点信封进入对称） */
  lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(scr, on_notify_gesture, LV_EVENT_GESTURE, NULL);

  /* 常驻翻页栏（通知中心属二级页面，不高亮任何一个） */
  velatime_ui_build_nav(scr, VELATIME_PAGE_OTHER);

  lv_scr_load(scr);
}
