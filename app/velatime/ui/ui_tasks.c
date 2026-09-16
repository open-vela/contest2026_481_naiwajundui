#include "velatime_ui.h"
#include "../core/core_task.h"
#include "../core/core_recommend.h"
#include "../core/core_schedule.h"
#include "../core/core_agent_sync.h"

#include <stdio.h>
#include <string.h>

/* 任务列表：与其它页面共用统一布局度量；点某一行进入"完成/延后/删除"面板 */

static void on_actions_back(lv_event_t *e);

static const char *status_icon(const velatime_task_t *task)
{
  switch (task->status)
    {
      case VELATIME_STATUS_DONE:      return "[x]";
      case VELATIME_STATUS_DOING:     return "[>]";
      case VELATIME_STATUS_POSTPONED: return "[~]";
      default:                        return "[ ]";
    }
}

static const char *status_text(const velatime_task_t *task)
{
  switch (task->status)
    {
      case VELATIME_STATUS_DONE:      return "已完成";
      case VELATIME_STATUS_DOING:     return "进行中";
      case VELATIME_STATUS_POSTPONED: return "已延后";
      default:                        return "待办";
    }
}

static uint32_t status_color(const velatime_task_t *task)
{
  switch (task->status)
    {
      case VELATIME_STATUS_DONE:      return 0x6B7280;
      case VELATIME_STATUS_DOING:     return 0x00D26A;
      case VELATIME_STATUS_POSTPONED: return 0xF0B429;
      default:                        return 0xFFFFFF;
    }
}

static char g_active_id[VELATIME_MAX_ID];
static int g_confirm_delete;

static void on_back_click(lv_event_t *e)
{
  (void)e;
  velatime_ui_home_show();
}

/* ---------- 操作面板 ---------- */

static void on_action_complete(lv_event_t *e)
{
  (void)e;
  if (g_active_id[0] != '\0')
    {
      core_task_set_status(g_active_id, VELATIME_STATUS_DONE);
      core_agent_sync_save();
      printf("VelaTime: task completed\n");
      fflush(stdout);
    }
  g_confirm_delete = 0;
  velatime_ui_tasks_show();
}

static void on_action_postpone(lv_event_t *e)
{
  (void)e;
  if (g_active_id[0] != '\0')
    {
      core_task_set_status(g_active_id, VELATIME_STATUS_POSTPONED);
      core_agent_sync_save();
      printf("VelaTime: task postponed\n");
      fflush(stdout);
    }
  g_confirm_delete = 0;
  velatime_ui_tasks_show();
}

static void on_action_delete(lv_event_t *e)
{
  (void)e;

  if (g_active_id[0] == '\0')
    {
      velatime_ui_tasks_show();
      return;
    }

  /* 删除不可逆：第一次点击只切到"确认删除"，再点一次才真删 */
  if (!g_confirm_delete)
    {
      g_confirm_delete = 1;
      velatime_ui_task_actions_show(g_active_id);
      return;
    }

  if (core_task_delete(g_active_id) == 0)
    {
      core_agent_sync_save();
      printf("VelaTime: task deleted\n");
      fflush(stdout);
    }

  g_active_id[0] = '\0';
  g_confirm_delete = 0;
  velatime_ui_tasks_show();
}

static void on_row_click(lv_event_t *e)
{
  const char *id = (const char *)lv_event_get_user_data(e);

  if (id == NULL || id[0] == '\0')
    {
      return;
    }

  strncpy(g_active_id, id, sizeof(g_active_id) - 1);
  g_active_id[sizeof(g_active_id) - 1] = '\0';
  g_confirm_delete = 0;
  velatime_ui_task_actions_show(g_active_id);
}

static void on_actions_back(lv_event_t *e)
{
  (void)e;
  g_confirm_delete = 0;
  velatime_ui_tasks_show();
}

void velatime_ui_task_actions_show(const char *task_id)
{
  const velatime_task_t *t = core_task_find(task_id);
  char detail[128];
  lv_obj_t *scr;
  lv_obj_t *col;
  lv_obj_t *card;
  lv_obj_t *row;

  scr = lv_obj_create(NULL);
  velatime_ui_style_screen(scr);
  col = velatime_ui_page_column(scr);

  card = lv_obj_create(col);
  lv_obj_set_size(card, LV_PCT(100), 320);
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
  lv_label_set_text(title, "任务操作");
  lv_obj_set_style_text_color(title, lv_color_hex(0xFF8A3D), 0);

  lv_obj_t *name = lv_label_create(card);
  lv_label_set_text(name, (t != NULL) ? t->title : "(任务不存在)");
  lv_obj_set_style_text_color(name, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_width(name, LV_PCT(100));
  lv_label_set_long_mode(name, LV_LABEL_LONG_WRAP);

  if (t != NULL && t->deadline[0] != '\0')
    {
      snprintf(detail, sizeof(detail), "%s · 截止 %s", status_text(t),
               t->deadline);
    }
  else
    {
      snprintf(detail, sizeof(detail), "%s",
               (t != NULL) ? status_text(t) : "-");
    }

  lv_obj_t *meta = lv_label_create(card);
  lv_label_set_text(meta, detail);
  lv_obj_set_style_text_color(meta, lv_color_hex(0x8890A0), 0);

  row = lv_obj_create(card);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_pad_column(row, 20, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *btn_done = lv_button_create(row);
  lv_obj_set_size(btn_done, VELATIME_UI_BTN_W, VELATIME_UI_BTN_H);
  lv_obj_t *done_label = lv_label_create(btn_done);
  lv_label_set_text(done_label, "标为完成");
  lv_obj_center(done_label);
  lv_obj_add_event_cb(btn_done, on_action_complete, LV_EVENT_CLICKED, NULL);

  lv_obj_t *btn_later = lv_button_create(row);
  lv_obj_set_size(btn_later, VELATIME_UI_BTN_W, VELATIME_UI_BTN_H);
  lv_obj_t *later_label = lv_label_create(btn_later);
  lv_label_set_text(later_label, "延后处理");
  lv_obj_center(later_label);
  lv_obj_add_event_cb(btn_later, on_action_postpone, LV_EVENT_CLICKED, NULL);

  lv_obj_t *btn_del = lv_button_create(row);
  lv_obj_set_size(btn_del, VELATIME_UI_BTN_W, VELATIME_UI_BTN_H);
  lv_obj_set_style_bg_color(btn_del, lv_color_hex(0x8C2F2F), 0);
  lv_obj_t *del_label = lv_label_create(btn_del);
  lv_label_set_text(del_label, g_confirm_delete ? "确认删除" : "删除任务");
  lv_obj_center(del_label);
  lv_obj_add_event_cb(btn_del, on_action_delete, LV_EVENT_CLICKED, NULL);

  lv_obj_t *btn_back = lv_button_create(col);
  lv_obj_set_size(btn_back, VELATIME_UI_BTN_W, VELATIME_UI_BTN_H);
  lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_t *back_label = lv_label_create(btn_back);
  lv_label_set_text(back_label, "返回");
  lv_obj_center(back_label);
  lv_obj_add_event_cb(btn_back, on_actions_back, LV_EVENT_CLICKED, NULL);

  /* 常驻翻页栏（二级页面，不高亮任何一个） */
  velatime_ui_build_nav(scr, VELATIME_PAGE_OTHER);

  lv_scr_load(scr);
}

/* ---------- 列表页 ---------- */

static void build_row(lv_obj_t *parent, const velatime_task_t *task)
{
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_height(row, VELATIME_UI_ROW_H);
  lv_obj_set_style_bg_color(row, lv_color_hex(0x1C2130), 0);
  lv_obj_set_style_radius(row, 12, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 16, 0);
  lv_obj_set_style_pad_row(row, 4, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

  /* 整行可点击：进入该任务的操作面板 */
  lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(row, on_row_click, LV_EVENT_CLICKED, (void *)task->id);

  lv_obj_t *title = lv_label_create(row);
  lv_label_set_text_fmt(title, "%s %s", status_icon(task), task->title);
  lv_obj_set_style_text_color(title, lv_color_hex(status_color(task)), 0);
  lv_obj_set_width(title, LV_PCT(100));
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);

  lv_obj_t *meta = lv_label_create(row);
  if (task->deadline[0] != '\0')
    {
      lv_label_set_text_fmt(meta, "%s · 截止 %s · 点击可操作",
                            status_text(task), task->deadline);
    }
  else
    {
      lv_label_set_text_fmt(meta, "%s · 点击可操作", status_text(task));
    }
  lv_obj_set_style_text_color(meta, lv_color_hex(0x8890A0), 0);
  lv_obj_set_width(meta, LV_PCT(100));
  lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);
}

void velatime_ui_tasks_show(void)
{
  int total = core_task_count();
  int i;
  int waiting = 0;
  char summary[64];
  lv_obj_t *scr;
  lv_obj_t *col;
  lv_obj_t *list;

  scr = lv_obj_create(NULL);
  velatime_ui_style_screen(scr);
  col = velatime_ui_page_column(scr);

  lv_obj_t *title = lv_label_create(col);
  lv_label_set_text(title, "任务列表");
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);

  for (i = 0; i < total; i++)
    {
      const velatime_task_t *t = core_task_get(i);
      if (t != NULL && t->status == VELATIME_STATUS_WAITING)
        {
          waiting++;
        }
    }

  snprintf(summary, sizeof(summary), "共 %d 条 · 待办 %d 条", total, waiting);
  lv_obj_t *subtitle = lv_label_create(col);
  lv_label_set_text(subtitle, summary);
  lv_obj_set_style_text_color(subtitle, lv_color_hex(0x8890A0), 0);

  list = lv_obj_create(col);
  lv_obj_set_size(list, LV_PCT(100), 520);
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 0, 0);
  lv_obj_set_style_pad_row(list, 12, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);

  if (total <= 0)
    {
      lv_obj_t *empty = lv_label_create(list);
      lv_label_set_text(empty, "暂无任务\n对 Agent 说：帮我创建一个任务");
      lv_obj_set_style_text_color(empty, lv_color_hex(0x8890A0), 0);
      lv_obj_set_width(empty, LV_PCT(100));
      lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);
    }
  else
    {
      for (i = 0; i < total; i++)
        {
          const velatime_task_t *t = core_task_get(i);
          if (t != NULL)
            {
              build_row(list, t);
            }
        }
    }

  lv_obj_t *btn_back = lv_button_create(col);
  lv_obj_set_size(btn_back, VELATIME_UI_BTN_W, VELATIME_UI_BTN_H);
  lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_t *back_label = lv_label_create(btn_back);
  lv_label_set_text(back_label, "返回");
  lv_obj_center(back_label);
  lv_obj_add_event_cb(btn_back, on_back_click, LV_EVENT_CLICKED, NULL);

  /* 常驻翻页栏 */
  velatime_ui_build_nav(scr, VELATIME_PAGE_TASKS);

  lv_scr_load(scr);
}
