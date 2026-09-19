#include "velatime_ui.h"
#include "../core/core_task.h"
#include "../core/core_schedule.h"
#include "../core/core_recommend.h"

#include <stdio.h>


static void build_course_row(lv_obj_t *parent, const velatime_course_t *course)
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

  lv_obj_t *name = lv_label_create(row);
  lv_label_set_text(name, course->name);
  lv_obj_set_style_text_color(name, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_width(name, LV_PCT(100));
  lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);

  lv_obj_t *detail = lv_label_create(row);
  if (course->room[0] != '\0')
    {
      lv_label_set_text_fmt(detail, "%s-%s · %s", course->start, course->end,
                            course->room);
    }
  else
    {
      lv_label_set_text_fmt(detail, "%s-%s", course->start, course->end);
    }
  lv_obj_set_style_text_color(detail, lv_color_hex(0x8890A0), 0);
  lv_obj_set_width(detail, LV_PCT(100));
  lv_label_set_long_mode(detail, LV_LABEL_LONG_DOT);
}

static void build_slot_row(lv_obj_t *parent, const velatime_free_slot_t *slot)
{
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_height(row, 60);
  lv_obj_set_style_bg_color(row, lv_color_hex(0x16301F), 0);
  lv_obj_set_style_radius(row, 12, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_left(row, 16, 0);
  lv_obj_set_style_pad_right(row, 16, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *tag = lv_label_create(row);
  lv_label_set_text_fmt(tag, "空闲 %s-%s", slot->start, slot->end);
  lv_obj_set_style_text_color(tag, lv_color_hex(0x00D26A), 0);

  lv_obj_t *mins = lv_label_create(row);
  lv_label_set_text_fmt(mins, "%d 分钟", slot->minutes);
  lv_obj_set_style_text_color(mins, lv_color_hex(0x8890A0), 0);
}

void velatime_ui_schedule_show(void)
{
  velatime_ui_set_page_index(VELATIME_PAGE_IDX_SCHEDULE);

  /* 2026-09-20 缓存优化：课表页内容不随时间变化，直接复用 */
  {
    lv_obj_t *cached = velatime_ui_scr_cache_get(VELATIME_PAGE_IDX_SCHEDULE);

    if (cached != NULL)
      {
        lv_scr_load(cached);
        return;
      }
  }

  velatime_free_slot_t slots[VELATIME_FREE_SLOT_MAX];
  int weekday = core_recommend_today_weekday();
  int slot_count;
  int course_count = 0;
  int i;
  char summary[64];
  lv_obj_t *col;
  lv_obj_t *list;

  lv_obj_t *scr = lv_obj_create(NULL);
  velatime_ui_style_screen(scr);
  col = velatime_ui_page_column(scr);

  lv_obj_t *title = lv_label_create(col);
  lv_label_set_text(title, "今日课程");
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);

  for (i = 0; i < core_schedule_count(); i++)
    {
      const velatime_course_t *c = core_schedule_get(i);
      if (c != NULL && c->weekday == weekday)
        {
          course_count++;
        }
    }

  slot_count = core_schedule_free_slots(weekday, slots, VELATIME_FREE_SLOT_MAX);
  snprintf(summary, sizeof(summary), "周%d · %d 门课 · %d 段空闲",
           weekday, course_count, slot_count);

  lv_obj_t *subtitle = lv_label_create(col);
  lv_label_set_text(subtitle, summary);
  lv_obj_set_style_text_color(subtitle, lv_color_hex(0x8890A0), 0);

  list = lv_obj_create(col);

  /*
   * 2026-09-20 修正：原来这里写死高度 520px。
   *
   * 454 屏幕上 col 只有约 285px（圆的内切安全区），
   * 而 col 里的标题 + 副标题 + list(520) 合计约 608px，
   * 超出 300 多 px；col 又在 velatime_ui_page_column() 里被禁用了滚动，
   * 于是下半部分的课程既看不到、也滑不动。
   *
   * 改成 flex 纵向容器里占满剩余高度：
   *   - 不再依赖任何硬编码像素，任何分辨率都成立；
   *   - list 自身保留 LV_DIR_VER，课程多了会自然出现上下滚动。
   * 注：原来的「返回」按钮已按要求移除，页面底部现在是翻页栏。
   *     导航仍可用：底部常驻翻页栏 + 左右滑动切页。
   */
  lv_obj_set_width(list, LV_PCT(100));
  lv_obj_set_flex_grow(list, 1);

  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 0, 0);
  lv_obj_set_style_pad_row(list, 12, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  /* 内容超出时显示滚动条，让"可以滑"这件事一眼可见 */
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

  for (i = 0; i < core_schedule_count(); i++)
    {
      const velatime_course_t *c = core_schedule_get(i);
      if (c != NULL && c->weekday == weekday)
        {
          build_course_row(list, c);
        }
    }

  for (i = 0; i < slot_count; i++)
    {
      build_slot_row(list, &slots[i]);
    }

  if (course_count == 0 && slot_count == 0)
    {
      lv_obj_t *empty = lv_label_create(list);
      lv_label_set_text(empty, "今天没有课，整天空闲");
      lv_obj_set_style_text_color(empty, lv_color_hex(0x8890A0), 0);
    }

  /*
   * 2026-09-20 用户要求：课程表页不需要「返回」按钮，已移除。
   * 导航仍然可用 —— 底部常驻翻页栏（任务/首页/课表）+ 左右滑动切页。
   */

  /* 常驻翻页栏 */
  velatime_ui_build_nav(scr, VELATIME_PAGE_SCHEDULE);

  {
    lv_obj_t *old_scr = velatime_ui_scr_cache_take_old(VELATIME_PAGE_IDX_SCHEDULE);

    velatime_ui_scr_cache_put(VELATIME_PAGE_IDX_SCHEDULE, scr);
    lv_scr_load(scr);

    if (old_scr != NULL)
      {
        lv_obj_delete(old_scr);
      }
  }
}
