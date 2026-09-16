#ifndef VELATIME_UI_H
#define VELATIME_UI_H

#include <lvgl/lvgl.h>

/* VelaTime 自带中文字库（GB2312 一级汉字 + ASCII），见 ui/velatime_font_cn.c
 * 说明：16px 用于全部正文；如需更大字号需改用"常用字子集"方案 */
extern const lv_font_t velatime_font_cn;
#define VELATIME_FONT_CN (&velatime_font_cn)

/* ------------------------------------------------------------------
 * 统一布局度量（模拟器屏幕固定 1280x800）
 * 所有页面共用这一套，保证边距、对齐、控件尺寸一致：
 *   - 页面左右边距 32，内容整体左对齐并等宽
 *   - 卡片内边距 24
 *   - 主按钮 200x64
 * ------------------------------------------------------------------ */
#define VELATIME_UI_SCREEN_W    1280
#define VELATIME_UI_SCREEN_H    800
#define VELATIME_UI_PAD         32     /* 页面边距 */
#define VELATIME_UI_PAD_CARD    24     /* 卡片内边距 */
#define VELATIME_UI_CONTENT_W   (VELATIME_UI_SCREEN_W - VELATIME_UI_PAD * 2)  /* 1216 */
#define VELATIME_UI_COL_W       960    /* 居中内容列宽度（所有页面统一） */
#define VELATIME_UI_BTN_W       200
#define VELATIME_UI_BTN_H       64
#define VELATIME_UI_ROW_H       76     /* 列表行高 */

/* 圆屏安全区：内容宽度取圆直径的这个百分比。
   圆的内切正方形是 70.7%，取 66% 留出余量，保证不被圆边切到。 */
#define VELATIME_UI_SAFE_PCT    66

/* 建一个居中内容列并返回它；页面所有控件都放进去，
   这样各页面的"内容停靠方式"完全一致（列居中、列内左对齐）。 */
lv_obj_t *velatime_ui_page_column(lv_obj_t *scr);

/* 圆屏安全方框（宽度 = 直径 * pct%，高度按屏幕算）。margin_pct 传 0 用默认值。 */
void velatime_ui_safe_box(int *box_w, int *box_h, int margin_pct);

/* 画表盘圆并返回它。velatime_ui_style_screen() 已自动调用；
   需要按半径定位内容（如 W1）时才单独用。 */
lv_obj_t *velatime_ui_build_dial(lv_obj_t *scr);

/* 一级页面标识：翻页栏靠它决定高亮哪个、以及不给自己加点击 */
typedef enum
{
  VELATIME_PAGE_HOME = 0,
  VELATIME_PAGE_SCHEDULE,
  VELATIME_PAGE_TASKS,
  VELATIME_PAGE_OTHER        /* 二级页面（如任务操作面板），不高亮任何一个 */
} velatime_ui_page_t;

/* 常驻翻页栏：三个小按钮（首页/课表/任务），各页面长相与位置统一。
   模拟器里手势可能失效，这是保证用户不卡在单页的主要手段。 */
void velatime_ui_build_nav(lv_obj_t *scr, velatime_ui_page_t current);

/* 各页面共用的根对象样式（底色 / 字体 / 滚动），见 ui/ui_theme.c */
void velatime_ui_style_screen(lv_obj_t *scr);

/* 运行时实际屏幕尺寸（真机 454x454 圆屏 / 模拟器 1280x800）。
   任何按比例排版的页面都应先调用它取尺寸，而不是直接用编译期常量。 */
void velatime_ui_screen_size(int *w, int *h);

void velatime_ui_init(void);
void velatime_ui_home_show(void);
void velatime_ui_home_refresh(void);
void velatime_ui_set_reminder(const char *text);
void velatime_ui_schedule_show(void);
void velatime_ui_tasks_show(void);
void velatime_ui_task_actions_show(const char *task_id);
void velatime_ui_popup_show(void);

#endif /* VELATIME_UI_H */
