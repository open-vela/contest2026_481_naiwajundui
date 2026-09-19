#ifndef VELATIME_UI_H
#define VELATIME_UI_H

#include <lvgl/lvgl.h>



/* ------------------------------------------------------------------
 * 页面页序（2026-09-19）—— 用于滑动切页的胶片方向判定
 * 顺序与底部导航栏一致：  0=任务(W3)  1=首页(W1)  2=课表(W2)
 * ------------------------------------------------------------------ */
#define VELATIME_PAGE_IDX_TASKS     0
#define VELATIME_PAGE_IDX_HOME      1
#define VELATIME_PAGE_IDX_SCHEDULE  2
#define VELATIME_PAGE_IDX_DETAIL     3   /* W4 任务详情 */
#define VELATIME_PAGE_IDX_NOTIFY     4   /* W5 通知中心 */
#define VELATIME_PAGE_IDX_OTHER    (-1)

void velatime_ui_set_page_index(int idx);
int  velatime_ui_get_page_index(void);

/* ==================================================================
 * 字库（严格按设计规范，不自行选字体）
 *
 *   12 个小时数字   Roboto Light   300
 *   时间            Roboto Regular 400
 *   日期/辅助数字    Roboto Regular 400
 *   中文页面入口     Noto Sans SC   Regular 400
 *
 * 全部由 lv_font_conv 从官方字体源按"只取实际用到的字符"生成，
 * 合计约 297KB（原 simhei 中文字库为 3.2MB）。
 * 生成方法见 C:\xiaomi\字库生成方法.md。
 * ================================================================== */

/* 12 个小时数字：Roboto Light 300，56px，仅 0-9（21.5KB） */
extern const lv_font_t velatime_font_hour56;
#define VELATIME_FONT_HOUR (&velatime_font_hour56)

/* 当前时间：Roboto Regular 400，64px，仅数字与冒号（25KB）
   精修：由 72px 下调到 64px（约 -11%），避免时间过大压满表盘 */
extern const lv_font_t velatime_font_time72;
#define VELATIME_FONT_TIME (&velatime_font_time72)

/* 日期与辅助数字：Roboto Regular 400，16px，ASCII（41.1KB） */
extern const lv_font_t velatime_font_ui16;
#define VELATIME_FONT_UI (&velatime_font_ui16)

/* 中文页面入口：Noto Sans SC 400，16px，仅"任务首页课表"（7.3KB） */
extern const lv_font_t velatime_font_nav16;
#define VELATIME_FONT_NAV (&velatime_font_nav16)

/* 信息行主字：Noto Sans SC 400，20px（2026-09-18 W3 改版）
 *   用于 W1「9月17日 星期四」、W3 页眉「任务」与任务标题。
 *   ★ 采用 GB2312 level-1 完整字符集（3869 字），而不是手工枚举的小子集。
 *     原因：任务标题来自用户在 TASKS.md 里的自由输入，无法穷举；
 *     子集字库必然缺字，缺字会渲染成方块（已反复踩坑）。
 *
 * ★ 本字库**不包含空格字形**（lv_font_conv 丢弃空白字形）。
 *   界面文字里不能出现空格，需要间隔时拆成多个标签 + pad_column，
 *   或使用「·」「/」当分隔符。
 */
extern const lv_font_t velatime_font_name20;
#define VELATIME_FONT_INFO (&velatime_font_name20)

/* 信息行小字：Noto Sans SC 400，15px（2026-09-18 W3 改版）
 *   用于 W3 任务行下方的截止时间「今天 · 18:00」。
 *   小字符集（109 字）。同样不含空格字形。
 *   页眉辅助信息「2 个待办」也用它（规格要求 9~10px，见 ui_tasks.c 说明）。 */
extern const lv_font_t velatime_font_meta15;
#define VELATIME_FONT_META (&velatime_font_meta15)

/* 中文正文：Noto Sans SC 400，16px，UI 实际用到的 234 字（205KB） */
extern const lv_font_t velatime_font_cn16;
#define VELATIME_FONT_CN (&velatime_font_cn16)

/* ==================================================================
 * W4 详情页字号（2026-09-18，用户要求"把圆用满"）
 *
 * 最终取值：任务名 36px、其余正文 30px、页眉 30px
 *   —— 由圆的几何反算出的最大可用倍率约 ×1.55（相对旧的 24/20），
 *      取 ×1.5 落地，留一点安全余量。
 *
 * 代价与前提：
 *   1) 30px 及以上的 GB2312 完整字库，lv_font_conv 会写入
 *      `#if (LV_FONT_FMT_TXT_LARGE == 0) #error` —— 位图偏移超出 16 位。
 *      因此必须给 LVGL 提供 lv_conf.h 并打开 LV_FONT_FMT_TXT_LARGE。
 *   2) 字库体积明显变大（30px 约 9.6MB、36px 约 13.5MB）。
 *      goldfish 平台 CONFIG_FLASH_SIZE = 122MB，放得下。
 * ================================================================== */

/* 详情页任务名：Noto Sans SC 36px，GB2312 完整集。 */
extern const lv_font_t velatime_font_name36;
#define VELATIME_FONT_TITLE (&velatime_font_name36)

/* 详情页正文（页眉/课程/状态/截止/动作）：Noto Sans SC 30px。 */
extern const lv_font_t velatime_font_body28;
#define VELATIME_FONT_BODY (&velatime_font_body28)

/* W3 任务名：Noto Sans SC 22px（常用字子集，规避 LVGL 位图 1MiB 上限） */
extern const lv_font_t velatime_font_name22;
#define VELATIME_FONT_TASKNAME (&velatime_font_name22)

/* W3 截止时间：Noto Sans SC 17px */
extern const lv_font_t velatime_font_meta17;
#define VELATIME_FONT_TASKMETA (&velatime_font_meta17)

/* ==================================================================
 * 设计系统色板（VelaTime UI 规范 v1，2026-09-17 定稿）
 *
 * 五个页面共享同一套视觉语言，颜色只从这里取，禁止在页面里写十六进制。
 *
 *   CLR_BG         底色：极深蓝黑
 *   CLR_TX_MAIN    主文字：任务名、正文
 *   CLR_TX_SUB     次文字：截止时间、副标题
 *   CLR_TX_MUTED   三级文字：提示、空状态、非当前页导航
 *   CLR_ACCENT     唯一强调色：未读红点 / 删除确认 / 必要警示
 *
 * 禁止出现的颜色：绿、橙、黄、蓝、紫。
 * 禁止的做法：卡片圆角框、按钮边框、Material 阴影、返回按钮。
 * ================================================================== */
#define CLR_BG        0x0E121C
#define CLR_TX_MAIN   0xF2F4F7
#define CLR_TX_SUB    0xC3CBD6
#define CLR_TX_MUTED  0x6B7280
#define CLR_ACCENT    0xFF3B30

/* 最亮文字：仅用于 W4 详情页的任务名与主操作「完成任务」。
 * 2026-09-18 加：用户反馈详情页文字"看不清楚"，要求调得更白。
 * 仍属同一套灰白体系（不是彩色），不引入新色相。 */
#define CLR_TX_BRIGHT 0xFFFFFF

/* 详情页次文字：课程名 / 状态 / 截止时间。
 * 原来用 CLR_TX_MUTED(#6B7280) 在深底上偏暗、读不清；
 * 提到 CLR_TX_SUB(#C3CBD6) 一级，既更清楚又仍低于任务名的亮度。 */
#define CLR_TX_DETAIL CLR_TX_SUB

/* 任务状态符号：三个字形表达"待办 / 进行中 / 完成"，
   用符号而不是彩色标签，避免 W2-W5 出现 Android 标签感 */
#define VELATIME_MARK_WAITING  "○"
#define VELATIME_MARK_DOING    "◐"
#define VELATIME_MARK_DONE     "●"
#define VELATIME_MARK_POSTPONE "~"

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

/* 表盘几何（按规范统一用 cx/cy/R 做基准，避免散落的魔法数字）。
   屏幕是正方形时 R_face = d/2；内容与圆周数字的位置都由 R 推导。 */
void velatime_ui_face_metrics(int *cx, int *cy, int *r_face);

/* 表盘背景（月球/星空），源为 guest 侧 /share/moon_dial.png。
   读不到文件就返回 NULL 并静默跳过，不影响任何功能。
   必须在 velatime_ui_style_screen() 之前调用，才能位于表盘圆之下。 */
lv_obj_t *velatime_ui_build_background(lv_obj_t *scr, int d);

/* 设置表盘圆底盘的不透明度：有背景图时调低让它透出来。 */
void velatime_ui_set_dial_bg_opa(lv_obj_t *scr, lv_opa_t opa);

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

/*
 * 屏幕缓存（2026-09-20，滑动卡顿优化）
 * ==================================================================
 * 每个页面只构建一次，切回时直接 lv_scr_load 复用，不再重建控件树。
 * 数据变化后调用 invalidate 把对应页面标脏，下次进入才重建。
 * idx 用 VELATIME_PAGE_IDX_* 常量。
 */
lv_obj_t *velatime_ui_scr_cache_get(int idx);
void      velatime_ui_scr_cache_put(int idx, lv_obj_t *scr);
void      velatime_ui_scr_cache_invalidate(int idx);
/* 取出旧屏幕（槽位让出），供新屏幕 load 之后删除 */
lv_obj_t *velatime_ui_scr_cache_take_old(int idx);

void velatime_ui_init(void);
void velatime_ui_home_show(void);
void velatime_ui_home_refresh(void);
void velatime_ui_set_reminder(const char *text);
void velatime_ui_schedule_show(void);
void velatime_ui_tasks_show(void);
void velatime_ui_task_actions_show(const char *task_id);
void velatime_ui_popup_show(void);

#endif /* VELATIME_UI_H */
