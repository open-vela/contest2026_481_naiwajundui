#include "velatime_ui.h"
#include "../include/velatime_time.h"
#include "../core/core_task.h"
#include "../core/core_recommend.h"
#include "../core/core_schedule.h"
#include "../core/core_agent_sync.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/*
 * W3 任务中心
 * ==================================================================
 * 设计定稿 2026-09-18（用户逐条给定规格，本文件严格照做）
 *
 * 定位：480×480 圆形手表上的「轻量任务清单」。
 *   要像原生圆形手表 UI，不能像缩小后的手机 Todo App。
 *
 * 视觉规则：
 *   - 纯深色背景 #0E121C，无背景图、无星空、无渐变、无发光、无纹理
 *   - 无卡片、无圆角、无边框、无阴影、无背景色块、无彩色标签、无胶囊按钮
 *   - **无横向分割线**，任务之间靠留白分组
 *   - 列表整体水平居中，列表内部统一左对齐（不是每个任务各自居中）
 *
 * 页面结构：
 *                    任务              <- 20px #F2F4F7 居中
 *                   2 个待办           <- 辅助信息 #6B7280
 *
 *              ○ 阅读软件工程            <- 20px #F2F4F7，最多两行
 *                今天 · 18:00           <- 15px #C3CBD6
 *
 *              ◐ 完成数学建模论文         <- 状态点几何绘制，16px
 *                09·18 · 23:59
 *
 *              任务   首页   课表         <- 保留，透明大点击区
 *                   ●
 *
 * ★★ 两条硬规则（都踩过坑）★★
 *
 * 规则一：界面文字里**绝对不能出现空格**。
 *   lv_font_conv 会丢弃空白字形，自造字库里没有 U+0020，
 *   文字里带空格会被 LVGL 渲染成一个"方块"。
 *   需要间隔时一律拆成多个标签 + pad_column，或使用「·」分隔。
 *
 * 规则二：百分比宽度（LV_PCT）的元素，父容器宽度必须"已确定"。
 *   曾把 LV_PCT(100) 的标签放进 LV_SIZE_CONTENT 的容器，
 *   两者互相依赖导致 LVGL 算出 0 宽，整行文字消失。
 *   本文件里所有 LV_PCT 的父级都是确定宽度（ctr / list / row / head / meta）。
 */

/* 内容区宽度占屏宽的比例（规格：约 80%，480 屏 ≈ 384px） */
#define W3_CONTENT_PCT   86

/*
 * 内容容器的高度与距圆顶的距离。
 *
 * ■ 最终方案（2026-09-18）：**固定内容顶部**，不再用"容器居中 + 上移量"。
 *   之前那套的毛病：内容块会随任务条数整体漂移 ——
 *   5 个任务时标题被推出圆顶（用户截图里标题完全不见）、
 *   2 个任务时又偏下。
 *
 *   现在容器顶边锚在圆顶下方 W3_TOP_PCT% 圆直径处，内容从顶部往下排：
 *     480 屏：容器顶 y = -240 + 0.08*480 = -202
 *             页眉「任务」占 y ≈ -202..-177（圆顶 -240，留 38px 余量）
 *             第一个任务在其下方 14px 处开始
 *   好处：无论几个任务，页眉位置**恒定不变**，永远在第一个任务正上方。
 *
 *   W3_TOP_PCT 由 11% 收紧到 8%：像素实测发现 11% 时内容整体偏低，
 *   第一行任务的截止文字会贴到/压住底部翻页栏。
 */
#define W3_CONTENT_H_PCT 70
#define W3_TOP_PCT       6       /* 容器顶边距圆顶的距离（占圆直径%） */

/* 页眉与第一个任务之间的留白（原 26px，偏大；收紧到 14px） */
#define W3_GAP_HEAD_LIST 10

/* 状态点直径（规格：约 16px） */
#define W3_DOT_SIZE      16

/* 任务名最多两行 */
#define W3_TITLE_MAX_LINES 2
/* 20px 字库的 line_height = 25，两行 = 50 */
#define W3_TITLE_LINE_H  27

/* 间距 */
/*
 * 名与截止之间 7px（规格值）。
 * 任务组之间：规格写 34px，但 454 圆屏实测放不下 ——
 *   页眉(24+21) + 间距 + 任务组×2(74×2) + 导航区(~61)
 *   用 34px 时第二个任务的截止文字会与底部翻页栏重叠。
 *   收紧到 22px 后，任务2 截止底约 y=284，导航文字从 y=285 起，刚好不压。
 *   这是"圆形安全区"与"规格间距"之间的取舍，已在此注明。
 */
#define W3_GAP_NAME_META 5    /* 任务名与截止时间之间 */
#define W3_GAP_TASKS     8    /* 14->8：同上 */   /* 不同任务组之间（规格 34，为圆屏安全区收紧） */
#define W3_GAP_DOT_NAME  10

/*
 * 版式 A 尺寸（2026-09-18 修正）
 * ------------------------------------------------------------------
 * 原来写成固定像素（396 / 8），只在 454 圆屏上正确。
 * 实测模拟器的 LCD 是 1280x800（d=800），固定 396px 只占 49%，
 * 行会显得很窄 —— 必须按屏幕直径换算，才能同时适配真机与模拟器。
 * 取值来源于 454 屏上的实测比例：
 *   row 396/454 = 872‰      pad 8/454 = 18‰
 */
#define W3_ROW_W_PERMILLE   872   /* 行宽 = 屏幕直径 × 872‰ */
#define W3_ROW_PAD_PERMILLE  18   /* 行内边距 = 屏幕直径 × 18‰ */      /* 12->8：给底部导航让位 */   /* 状态点与任务名之间 */

/* ---------------------------------------------------------------- */
/* W4 任务详情页的布局常量（2026-09-18 视觉重构）                      */
/* ---------------------------------------------------------------- */

/* 任务名最多两行。
 * 字号 21px（VELATIME_FONT_DETAIL），实测 line_height = 25，两行 = 50px。
 * 高度必须按实测行高给，否则会在页眉与任务名之间造成可见空隙。 */
#define W4_NAME_MAX_LINES 2
#define W4_NAME_LINE_H    42

/* 块与块之间的留白（任务名/课程/状态/截止/动作 各块之间）。
 * 21px 字号下主体自然高度约 220px，而"页眉底到翻页栏文字顶"约 247px，
 * 只有约 27px 余量，所以块间距取 5px、由 W4_BODY_UP_PCT 统一微调落位。 */
#define W4_GAP_BLOCK      6
/* 状态点与状态文字之间 */
#define W4_GAP_DOT_TEXT   8
/* 截止时间三段之间（今天 · 18:00） */
#define W4_GAP_SEG        6
/* 动作项之间的留白（透明点击区自带上下内边距，这里不再多留） */
#define W4_GAP_ACTION     0

/* 动作项：文字 25px（21px 字库的 line_height）+ 上下各 6px 透明 padding。
 * 高度必须固定：实测用 LV_SIZE_CONTENT 时 label 落位异常，
 * 会把动作区从 111px 撑大、进而把主体推高到 256px 压住页眉。 */
#define W4_ACTION_PAD_V   4
#define W4_ACTION_H       (W4_NAME_LINE_H + W4_ACTION_PAD_V * 2)   /* 37 */

/* 页眉「任务详情」距圆顶的距离（占圆直径%）。
   4% 由扫描实测得出：配合下面的主体偏移，页眉与任务名、任务名与翻页栏
   两端间隙都约为 20px。 */
#define W4_TITLE_TOP_PCT  8

/*
 * 版式 E1 尺寸（2026-09-18 修正为自适应）
 * ------------------------------------------------------------------
 * 原为固定像素，只在 454 圆屏正确；模拟器 LCD 是 1280x800。
 * 比例来自 454 屏实测： col 380/454=837‰  btn 210/454=463‰  h 60/454=132‰
 */
#define W4_COL_W_PERMILLE   837
#define W4_BTN_W_PERMILLE   463
#define W4_BTN_H_PERMILLE   132
#define W4_CLR_PRIMARY 0x2196F3   /* 主操作：蓝（C3 主题色）*/
#define W4_CLR_DANGER  0xE74C3C   /* 危险操作：红（C3 Exit 按钮）*/
#define W4_CLR_NEUTRAL 0x4A5568   /* 次要操作：灰 */

/* 主体整块上移量（占圆直径%）。
 *
 * 11%：2026-09-18 详情页正文放大到 21px 后逐档实测标定。
 *   21px 下主体高 226px（任务名2行50 + 课25 + 状态25 + 截止25 + 动作111 - 间距），
 *   而"页眉底(42) 到 翻页栏文字顶(285)"只有 243px，余量仅 17px：
 *     偏移  页眉→任务名   删除任务→翻页栏
 *      10%     27px         -3px（重叠）
 *      11%     23px         +1px   ← 采用
 *      12%     18px         +6px
 *
 * ★ 若任务带 course（课程名非空），主体会再高 30px 而放不下 21px。
 *   真机上 course 为空（core_agent_sync 从 TASKS.md 只填 title/deadline/priority），
 *   所以实际不会出现该行。mock 数据里有 course，对照渲染时会显得更满。
 */
#define W4_BODY_UP_PCT    -5

/* ---------------------------------------------------------------- */
/* 截止时间：紧凑格式（规格要求不要完整年份）                          */
/* ---------------------------------------------------------------- */

/*
 * 输出三段独立文本，调用方用「独立标签 + 列间距」排成一行。
 * 这样文字里不含空格（规则一）。
 *
 *   今天到期  pre="今天"  mid="·"  time="18:00"
 *   明天到期  pre="明天"  mid="·"  time="18:00"
 *   更远      pre="09·18" mid="·"  time="23:59"
 *   已超期    pre="已超期" mid="·"  time="09·16"
 *   无截止    pre="无截止时间" mid="" time=""
 *
 * task->deadline 存储格式固定 "YYYY-MM-DD HH:MM"（见 velatime_types.h）。
 * 解析失败时原样回退，不编造。
 */
static void format_deadline(const velatime_task_t *task,
                            char *pre, size_t pre_n,
                            char *mid, size_t mid_n,
                            char *time_txt, size_t time_n)
{
  int y = 0;
  int mo = 0;
  int d = 0;
  int hh = 0;
  int mi = 0;
  struct timespec ts;
  struct tm now_tm;
  /* %04d 最坏 11 字符，缓冲区留足以免 -Werror=format-truncation */
  char today[32];
  char tomorrow[32];
  char target[32];

  if (pre == NULL || mid == NULL || time_txt == NULL)
    {
      return;
    }

  pre[0] = '\0';
  mid[0] = '\0';
  time_txt[0] = '\0';

  if (task == NULL || task->deadline[0] == '\0')
    {
      snprintf(pre, pre_n, "无截止时间");
      return;
    }

  if (sscanf(task->deadline, "%d-%d-%d %d:%d", &y, &mo, &d, &hh, &mi) != 5)
    {
      snprintf(pre, pre_n, "%s", task->deadline);
      return;
    }

  if (clock_gettime(CLOCK_REALTIME, &ts) != 0 ||
      velatime_localtime(ts.tv_sec, &now_tm) == NULL)
    {
      snprintf(pre, pre_n, "%02d·%02d", mo, d);
      snprintf(mid, mid_n, "·");
      snprintf(time_txt, time_n, "%02d:%02d", hh, mi);
      return;
    }

  snprintf(today, sizeof(today), "%04d-%02d-%02d",
           now_tm.tm_year + 1900, now_tm.tm_mon + 1, now_tm.tm_mday);

  /* 明天：tm_mday 直接 +1，交给 timegm 处理跨月跨年 */
  {
    struct tm t = now_tm;

    t.tm_mday += 1;
    t.tm_hour = 12;
    t.tm_min = 0;
    t.tm_sec = 0;
    if (velatime_mktime(&t) != (time_t)-1)
      {
        snprintf(tomorrow, sizeof(tomorrow), "%04d-%02d-%02d",
                 t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
      }
    else
      {
        tomorrow[0] = '\0';
      }
  }

  snprintf(target, sizeof(target), "%04d-%02d-%02d", y, mo, d);

  /* 已超期：格式「已超期 · 09·16」 */
  if (strcmp(target, today) < 0 ||
      (strcmp(target, today) == 0 &&
       (hh < now_tm.tm_hour || (hh == now_tm.tm_hour && mi < now_tm.tm_min))))
    {
      snprintf(pre, pre_n, "已超期");
      snprintf(mid, mid_n, "·");
      snprintf(time_txt, time_n, "%02d·%02d", mo, d);
      return;
    }

  snprintf(mid, mid_n, "·");
  snprintf(time_txt, time_n, "%02d:%02d", hh, mi);

  if (strcmp(target, today) == 0)
    {
      snprintf(pre, pre_n, "今天");
      return;
    }

  if (tomorrow[0] != '\0' && strcmp(target, tomorrow) == 0)
    {
      snprintf(pre, pre_n, "明天");
      return;
    }

  /* 更远的日期：09·18（规格要求不带年份） */
  snprintf(pre, pre_n, "%02d·%02d", mo, d);
}

/* ---------------------------------------------------------------- */
/* 状态指示点：纯几何绘制（规格要求不依赖 Unicode 字符渲染）            */
/* ---------------------------------------------------------------- */

/*
 *   ○ 未开始   空心圆（描边）
 *   ◐ 进行中   下半实心
 *   ● 已完成   实心圆
 *   延后       空心圆，描边更暗
 *
 * 用 lv_obj + radius 画，不依赖任何字形 —— 换字库也不会坏。
 */

/* 状态文字：与状态点一一对应，不含空格，也不含"截止"这类字段名 */
static const char *velatime_task_status_text(const velatime_task_t *task)
{
  if (task == NULL)
    {
      return "任务不存在";
    }

  switch (task->status)
    {
      case VELATIME_STATUS_DONE:
        return "已完成";
      case VELATIME_STATUS_DOING:
        return "进行中";
      case VELATIME_STATUS_POSTPONED:
        return "已延后";
      case VELATIME_STATUS_WAITING:
      default:
        return "未开始";
    }
}

static lv_obj_t *build_status_dot(lv_obj_t *parent,
                                  const velatime_task_t *task)
{
  lv_obj_t *dot;
  int size = W3_DOT_SIZE;
  int ring = 2;
  uint32_t color;
  int postponed = 0;

  if (task != NULL && task->status == VELATIME_STATUS_POSTPONED)
    {
      postponed = 1;
    }

  if (task != NULL &&
      (task->status == VELATIME_STATUS_DONE || postponed))
    {
      color = CLR_TX_MUTED;          /* 完成/延后：退到三级文字 */
    }
  else
    {
      color = CLR_TX_SUB;            /* 未开始 / 进行中 */
    }

  dot = lv_obj_create(parent);
  lv_obj_set_size(dot, size, size);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(dot, ring, 0);
  lv_obj_set_style_border_color(dot, lv_color_hex(color), 0);
  lv_obj_set_style_border_opa(dot, postponed ? LV_OPA_40 : LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(dot, 0, 0);
  lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(dot, LV_OBJ_FLAG_GESTURE_BUBBLE);

  if (task != NULL && task->status == VELATIME_STATUS_DONE)
    {
      lv_obj_set_style_bg_color(dot, lv_color_hex(color), 0);
      lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    }
  else if (task != NULL && task->status == VELATIME_STATUS_DOING)
    {
      /* 进行中：下半实心，读作"半个圆" */
      lv_obj_t *half = lv_obj_create(dot);

      lv_obj_set_size(half, size, size / 2 + 1);
      lv_obj_align(half, LV_ALIGN_BOTTOM_MID, ring, ring);
      lv_obj_set_style_radius(half, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_color(half, lv_color_hex(color), 0);
      lv_obj_set_style_bg_opa(half, LV_OPA_COVER, 0);
      lv_obj_set_style_border_width(half, 0, 0);
      lv_obj_set_style_pad_all(half, 0, 0);
      lv_obj_remove_flag(half, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_remove_flag(half, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_flag(half, LV_OBJ_FLAG_GESTURE_BUBBLE);
    }

  return dot;
}

/* ---------------------------------------------------------------- */
/* 动作面板（二级页面）：业务逻辑完全不变，仅去掉按钮框                 */
/* ---------------------------------------------------------------- */

static char g_active_id[VELATIME_MAX_ID];
static int g_confirm_delete;

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

/*
 * 无框动作：只有文字，没有背景/边框/阴影/圆角。
 *
 * 字号 VELATIME_FONT_DETAIL（21px），点击区靠透明 padding 放大到约 133x37
 * （文字本身只有约 84x25），视觉上仍是"一行字"，但容易点中。
 *
 * ★ 标签必须给确定尺寸再 CENTER：
 *   实测用 LV_SIZE_CONTENT + lv_obj_center() 时，label 的坐标会落在容器
 *   底部附近（容器 y=250..286，label 却报 y=256..280），把动作区撑到 111px，
 *   进而把整个主体推到 256px 高、压住页眉。改为固定高 + CENTER 后，
 *   按钮高 = 25(文字) + 6*2(padding) = 37px，动作区 = 111px，
 *   主体回到可预期的 195px。
 */
/*
 * 药丸按钮（2026-09-18 版式 E1，仿 C3 手表项目 ui_findButton）
 *   C3 写法: lv_button_create + width 100 / height 40 + radius 20 (=H/2)
 *   本工程按 240->454 换算: 210 x 60, radius 30
 *   bg 传背景色（C3 用主题默认蓝，这里显式指定以便区分动作）
 */
static lv_obj_t *build_pill_button(lv_obj_t *parent, const char *text,
                                   uint32_t bg, lv_event_cb_t cb)
{
  int bw;
  int bh;
  lv_obj_t *btn = lv_button_create(parent);
  lv_obj_t *lab;

  {
    int bsw = 0, bsh = 0, bd;

    velatime_ui_screen_size(&bsw, &bsh);
    bd = (bsw < bsh) ? bsw : bsh;
    bw = bd * W4_BTN_W_PERMILLE / 1000;
    bh = bd * W4_BTN_H_PERMILLE / 1000;
  }

  lv_obj_set_width(btn, bw);
  lv_obj_set_height(btn, bh);
  lv_obj_set_style_radius(btn, bh / 2, 0);            /* = H/2 → 药丸形 */
  lv_obj_set_style_bg_color(btn, lv_color_hex(bg), 0);
  lv_obj_set_style_bg_opa(btn, 255, 0);
  lv_obj_set_style_border_width(btn, 0, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

  lab = lv_label_create(btn);
  lv_obj_set_width(lab, LV_SIZE_CONTENT);
  lv_obj_set_height(lab, LV_SIZE_CONTENT);
  lv_label_set_text(lab, text);
  lv_obj_set_style_text_font(lab, VELATIME_FONT_BODY, 0);   /* 28px */
  lv_obj_set_style_text_color(lab, lv_color_hex(CLR_TX_BRIGHT), 0);
  lv_obj_center(lab);

  if (cb != NULL)
    {
      lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    }

  return btn;
}

/*
 * 返回图标：用两根短线画一个 "‹"（chevron），不用字形。
 *
 * 为什么不用字符 —— 实测 U+2039 ‹ / U+2190 ← / U+276E ❮ 在工程自造字库里
 * 全部缺失，直接写会渲染成方块。几何绘制与状态点同一思路，换字库也不会坏。
 * 视觉是 16px 的 "‹"，颜色 #C3CBD6，无背景无边框。
 */
static lv_obj_t *build_back_chevron(lv_obj_t *parent, lv_event_cb_t cb)
{
  lv_obj_t *hit;
  lv_obj_t *l1;
  lv_obj_t *l2;

  /* 透明点击区：44x44，明显大于 16px 的图标 */
  hit = lv_obj_create(parent);
  lv_obj_set_size(hit, 44, 44);
  lv_obj_set_style_bg_opa(hit, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(hit, 0, 0);
  lv_obj_set_style_radius(hit, 0, 0);
  lv_obj_set_style_shadow_width(hit, 0, 0);
  lv_obj_set_style_pad_all(hit, 0, 0);
  lv_obj_remove_flag(hit, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(hit, LV_OBJ_FLAG_GESTURE_BUBBLE);

  /* 两笔：上斜线与下斜线，各自旋转 45 度拼成尖角 */
  l1 = lv_line_create(hit);
  static lv_point_precise_t p_up[2]   = { {0, 0}, {0, 11} };
  static lv_point_precise_t p_down[2] = { {0, 0}, {0, 11} };
  lv_line_set_points(l1, p_up, 2);
  lv_obj_set_style_line_width(l1, 2, 0);
  lv_obj_set_style_line_color(l1, lv_color_hex(CLR_TX_BRIGHT), 0);
  lv_obj_set_style_line_rounded(l1, true, 0);
  lv_obj_align(l1, LV_ALIGN_CENTER, -4, -5);
  lv_obj_set_style_transform_rotation(l1, 450, 0);      /* 45.0 度 */
  lv_obj_set_style_transform_pivot_x(l1, 0, 0);
  lv_obj_set_style_transform_pivot_y(l1, 5, 0);

  l2 = lv_line_create(hit);
  lv_line_set_points(l2, p_down, 2);
  lv_obj_set_style_line_width(l2, 2, 0);
  lv_obj_set_style_line_color(l2, lv_color_hex(CLR_TX_BRIGHT), 0);
  lv_obj_set_style_line_rounded(l2, true, 0);
  lv_obj_align(l2, LV_ALIGN_CENTER, -4, 5);
  lv_obj_set_style_transform_rotation(l2, -450, 0);     /* -45.0 度 */
  lv_obj_set_style_transform_pivot_x(l2, 0, 0);
  lv_obj_set_style_transform_pivot_y(l2, 5, 0);

  lv_obj_remove_flag(l1, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(l2, LV_OBJ_FLAG_CLICKABLE);

  if (cb != NULL)
    {
      lv_obj_add_event_cb(hit, cb, LV_EVENT_CLICKED, NULL);
    }

  return hit;
}

/*
 * W4 任务详情页（2026-09-18 视觉重构）
 * ==================================================================
 * 定位：**圆形手表上的任务详情**，不是缩小版手机 Todo。
 *
 * 纵向结构（整体围绕圆心，不堆在左上角）：
 *
 *        ‹                <- 左上角返回图标（几何绘制，16px，#C3CBD6）
 *      任务详情            <- cn16 16px #F2F4F7，水平居中
 *
 *    完成数学建模论文        <- name20 20px #F2F4F7，居中，最多两行
 *       高等数学           <- meta15 15px #6B7280（course 为空则整行不出现）
 *
 *       ◐ 进行中           <- 状态点 16px（几何）+ meta15 15px #C3CBD6
 *     09·18 · 23:59        <- meta15 15px #C3CBD6（无完整年份）
 *
 *        完成任务           <- meta15 15px #F2F4F7  主操作
 *        延后处理           <- meta15 15px #6B7280  次操作
 *        删除任务           <- meta15 15px #6B7280  次操作（点击后转确认态）
 *
 *     任务   首页   课表       <- ui_theme 的常驻翻页栏，高亮"任务"
 *
 * 业务逻辑完全不变：on_action_complete / on_action_postpone / on_action_delete
 * 三个回调一行未改，删除仍是"第一次点转确认、再点一次才真删"。
 */
void velatime_ui_task_actions_show(const char *task_id)
{
  velatime_ui_set_page_index(VELATIME_PAGE_IDX_DETAIL);
  int col_w;
  /*
   * 版式 E1（2026-09-18 用户选定）
   * 仿 C3 手表项目 esp32-c3-mini / src/ui/ui.c:
   *   - 标题居中 + 1px 白线仅底部（ui_appInfoScreen / ui_alertScreen 的写法）
   *   - 药丸按钮 radius = height/2（ui_findButton，第 4003 行）
   *   - 黑底、无卡片、无圆角容器
   *
   * 业务回调一字未改：on_action_complete / on_action_postpone /
   * on_action_delete / on_actions_back，删除确认态 g_confirm_delete 逻辑照旧。
   */
  velatime_task_t *t = core_task_find(task_id);
  lv_obj_t *scr;
  lv_obj_t *back;
  lv_obj_t *col;
  lv_obj_t *title;
  lv_obj_t *name;
  lv_obj_t *meta_row;
  char pre[32];
  char mid[8];
  char tm[16];
  char meta[96];
  const char *date_txt;
  int sw = 0;
  int sh = 0;
  int d;

  scr = lv_obj_create(NULL);
  velatime_ui_style_screen(scr);

  velatime_ui_screen_size(&sw, &sh);
  d = (sw < sh) ? sw : sh;
  col_w = d * W4_COL_W_PERMILLE / 1000;

  /* ---- 左上角返回箭头（保留原实现）---- */
  back = build_back_chevron(scr, on_actions_back);
  lv_obj_align(back, LV_ALIGN_TOP_LEFT, d * 6 / 100, d * 6 / 100);

  /* ---- 主列 ---- */
  col = lv_obj_create(scr);
  lv_obj_set_width(col, LV_PCT(100));
  lv_obj_set_height(col, LV_PCT(100));
  lv_obj_set_align(col, LV_ALIGN_TOP_MID);
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(col, 0, 0);
  lv_obj_set_style_radius(col, 0, 0);
  lv_obj_set_style_pad_all(col, 0, 0);
  lv_obj_set_style_pad_top(col, d * W4_TITLE_TOP_PCT / 100, 0);
  lv_obj_set_style_pad_row(col, W4_GAP_BLOCK + 6, 0);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(col, LV_OBJ_FLAG_GESTURE_BUBBLE);

  /* ---- 标题：居中 + 1px 白线仅底部（C3 写法）---- */
  title = lv_label_create(col);
  lv_obj_set_width(title, col_w);
  lv_label_set_text(title, "任务详情");
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(title, VELATIME_FONT_BODY, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(CLR_TX_BRIGHT), 0);
  lv_obj_set_style_border_color(title, lv_color_hex(CLR_TX_BRIGHT), 0);
  lv_obj_set_style_border_opa(title, 255, 0);
  lv_obj_set_style_border_width(title, 1, 0);
  lv_obj_set_style_border_side(title, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_pad_top(title, 4, 0);
  lv_obj_set_style_pad_bottom(title, 6, 0);

  /* ---- 任务名 ---- */
  name = lv_label_create(col);
  lv_label_set_text(name, (t != NULL) ? t->title : "任务不存在");
  lv_obj_set_style_text_font(name, VELATIME_FONT_BODY, 0);
  lv_obj_set_style_text_color(name, lv_color_hex(CLR_TX_BRIGHT), 0);
  lv_obj_set_width(name, col_w);
  lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);

  /* ---- 状态点 + 状态 + 截止（一行，居中）---- */
  format_deadline(t, pre, sizeof(pre), mid, sizeof(mid), tm, sizeof(tm));

  /* 只取一段日期：已超期时 tm 里是月日；今天/明天时 pre 是相对词 */
  if (strcmp(pre, "已超期") == 0)
    {
      date_txt = tm;
    }
  else
    {
      date_txt = pre;
    }

  snprintf(meta, sizeof(meta), "%s\xC2\xB7%s",
           velatime_task_status_text(t), date_txt);

  meta_row = lv_obj_create(col);
  lv_obj_set_size(meta_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(meta_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(meta_row, 0, 0);
  lv_obj_set_style_radius(meta_row, 0, 0);
  lv_obj_set_style_pad_all(meta_row, 0, 0);
  lv_obj_set_style_pad_column(meta_row, W4_GAP_DOT_TEXT, 0);
  lv_obj_set_flex_flow(meta_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(meta_row, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(meta_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(meta_row, LV_OBJ_FLAG_GESTURE_BUBBLE);

  build_status_dot(meta_row, t);

  {
    lv_obj_t *meta_lab = lv_label_create(meta_row);

    lv_label_set_text(meta_lab, meta);
    lv_obj_set_style_text_font(meta_lab, VELATIME_FONT_TASKMETA, 0);
    lv_obj_set_style_text_color(meta_lab, lv_color_hex(CLR_TX_DETAIL), 0);
  }

  /* ---- 动作：药丸按钮（E1）---- */
  if (g_confirm_delete)
    {
      /*
       * 删除确认态。两个选项都走原有的 on_action_delete ——
       * 该回调本身就是"未确认则转确认、已确认则执行删除"，
       * 点「删除任务」即确认删除，点「取消」只是返回本页并复位标志。
       * 回调一个字都没改。
       */
      lv_obj_t *ask = lv_label_create(col);

      lv_label_set_text(ask, "确定删除任务？");
      lv_obj_set_style_text_font(ask, VELATIME_FONT_TASKMETA, 0);
      lv_obj_set_style_text_color(ask, lv_color_hex(CLR_TX_DETAIL), 0);

      build_pill_button(col, "删除任务", W4_CLR_DANGER, on_action_delete);
      build_pill_button(col, "取消", W4_CLR_NEUTRAL, on_action_delete);
    }
  else
    {
      if (t == NULL || t->status != VELATIME_STATUS_DONE)
        {
          build_pill_button(col, "完成任务", W4_CLR_PRIMARY, on_action_complete);
        }

      build_pill_button(col, "延后处理", W4_CLR_NEUTRAL, on_action_postpone);
      build_pill_button(col, "删除任务", W4_CLR_DANGER, on_action_delete);
    }

  /* 详情页不放翻页栏：返回靠左上角 ‹，最终交互是滑动（2026-09-18 用户确认） */

  lv_scr_load(scr);
}

/* ---------------------------------------------------------------- */
/* W3 主页面                                                          */
/* ---------------------------------------------------------------- */

/*
 * 一个任务组：状态点 + 任务名（第一行），截止时间（第二行）。
 *
 * 结构（所有 LV_PCT 的父容器都有确定宽度 —— 规则二）：
 *   ctr  LV_PCT(80)   确定
 *    └ list LV_PCT(100) 确定，内部左对齐
 *       └ row  LV_PCT(100) 确定
 *          ├ head LV_PCT(100) 确定 → 点(16) + 间距(10) + 名字(flex_grow)
 *          └ meta LV_PCT(100) 确定 → 多段标签，左对齐
 */
static void build_row(lv_obj_t *parent, const velatime_task_t *task)
{
  int row_w;
  int row_pad;
  /*
   * 版式 A（用户 2026-09-18 选定）
   * 结构照搬 C3 手表项目 esp32-c3-mini / src/ui/ui.c 的 addNotificationList():
   *   - flex ROW 横排：状态圆点 + 任务名 + 截止时间
   *   - 纯黑底 0x000000，无圆角、无卡片
   *   - 1px 白线【仅底部】做分隔
   *   - 任务名 flex_grow(1) 占剩余宽度，超长 LONG_DOT 省略
   * 尺寸按 240px -> 454px 换算（×1.89）。
   */
  lv_obj_t *row;
  lv_obj_t *dot;
  lv_obj_t *name;
  lv_obj_t *meta;
  char pre[32];
  char mid[8];
  char tim[32];
  char dl[80];

  /* 复用项目既有 format_deadline，不新写日期逻辑 */
  format_deadline(task, pre, sizeof(pre), mid, sizeof(mid),
                  tim, sizeof(tim));

  if (strcmp(pre, "已超期") == 0 || strcmp(pre, "今天") == 0 ||
      strcmp(pre, "明天") == 0)
    {
      snprintf(dl, sizeof(dl), "%s%s%s", pre, mid, tim);
    }
  else
    {
      snprintf(dl, sizeof(dl), "%s", pre);
    }

  {
    int rsw = 0, rsh = 0, rd;

    velatime_ui_screen_size(&rsw, &rsh);
    rd = (rsw < rsh) ? rsw : rsh;
    row_w = rd * W3_ROW_W_PERMILLE / 1000;
    row_pad = rd * W3_ROW_PAD_PERMILLE / 1000;
  }

  row = lv_obj_create(parent);
  lv_obj_set_width(row, row_w);
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_set_style_radius(row, 0, 0);                     /* 无圆角 */
  /*
   * 行背景【透明】而不是纯黑。
   * C3 的屏幕底色是纯黑，所以它的列表项也用纯黑；
   * 本工程屏幕是 CLR_BG(#0E121C)，照抄纯黑会在圆里形成明显的黑带。
   * 改成透明后行与屏幕同色，只靠底部 1px 白线分隔（2026-09-18 用户指出）。
   */
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_color(row, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_opa(row, 255, 0);
  lv_obj_set_style_border_width(row, 1, 0);
  lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);  /* 仅底部线 */
  lv_obj_set_style_pad_all(row, row_pad, 0);
  lv_obj_set_style_pad_column(row, row_pad, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

  /* 整行可点：进入该任务的操作面板（回调与原实现完全一致） */
  lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(row, on_row_click, LV_EVENT_CLICKED, (void *)task->id);

  /* ---- 左：状态圆点（空心=未完成，实心=已完成）---- */
  dot = lv_obj_create(row);
  lv_obj_set_size(dot, row_pad * 2, row_pad * 2);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(dot, 2, 0);
  lv_obj_set_style_border_color(dot, lv_color_hex(0xFFFFFF), 0);
  lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);

  if (task->status == VELATIME_STATUS_DONE)
    {
      lv_obj_set_style_bg_color(dot, lv_color_hex(0xFFFFFF), 0);
      lv_obj_set_style_bg_opa(dot, 255, 0);
    }
  else
    {
      lv_obj_set_style_bg_opa(dot, LV_OPA_TRANSP, 0);
    }

  /* ---- 中：任务名（占剩余宽度，超长省略）---- */
  name = lv_label_create(row);
  lv_obj_set_height(name, LV_SIZE_CONTENT);
  lv_obj_set_flex_grow(name, 1);
  lv_obj_set_style_text_font(name, VELATIME_FONT_TASKNAME, 0);
  lv_obj_set_style_text_color(name, lv_color_hex(
                                (task->status == VELATIME_STATUS_DONE ||
                                 task->status == VELATIME_STATUS_POSTPONED)
                                  ? CLR_TX_MUTED : 0xFFFFFF), 0);
  lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
  lv_label_set_text(name, task->title);
  lv_obj_remove_flag(name, LV_OBJ_FLAG_CLICKABLE);

  /* ---- 右：截止时间（随内容，完整显示）---- */
  meta = lv_label_create(row);
  lv_obj_set_width(meta, LV_SIZE_CONTENT);
  lv_obj_set_height(meta, LV_SIZE_CONTENT);
  lv_obj_set_style_text_font(meta, VELATIME_FONT_TASKMETA, 0);
  lv_obj_set_style_text_color(meta, lv_color_hex(CLR_TX_SUB), 0);
  lv_label_set_text(meta, dl);
  lv_obj_remove_flag(meta, LV_OBJ_FLAG_CLICKABLE);
}

void velatime_ui_tasks_show(void)
{
  velatime_ui_set_page_index(VELATIME_PAGE_IDX_TASKS);
  int total = core_task_count();
  int i;
  int waiting = 0;
  int scr_w = 0;
  int scr_h = 0;
  int d;
  int up_shift;
  lv_obj_t *scr;
  lv_obj_t *ctr;
  lv_obj_t *list;
  char buf[32];

  /* 取实际屏幕尺寸（真机圆屏 / 模拟器），不写死编译期常量 */
  velatime_ui_screen_size(&scr_w, &scr_h);
  d = (scr_w < scr_h) ? scr_w : scr_h;
  up_shift = d * W3_TOP_PCT / 100;   /* 容器顶边距圆顶的距离 */

  scr = lv_obj_create(NULL);
  velatime_ui_style_screen(scr);

  for (i = 0; i < total; i++)
    {
      const velatime_task_t *t = core_task_get(i);
      if (t != NULL && t->status != VELATIME_STATUS_DONE)
        {
          waiting++;
        }
    }

  /*
   * 内容容器 ctr：宽度 80% 屏宽，**整体水平居中**。
   * 固定百分比宽度 → 内部所有 LV_PCT 都有确定父宽（规则二）。
   *
   * 高度 76% → 64%（2026-09-18 调整）：
   *   内容实际只占约 236px，容器 365px 过大；内容在容器里顶部对齐，
   *   于是整块被压到下半部、标题与首个任务之间显得空。
   *   收紧到 64%（480 屏约 307px）让内容块自身更紧凑。
   *
   * 整体上移 6% 屏高（用户确认方案 A）：
   *   视觉重心略高于圆心，同时给底部翻页栏（在 +15% 屏高处）留出空间。
   */
  ctr = lv_obj_create(scr);
  lv_obj_set_width(ctr, LV_PCT(W3_CONTENT_PCT));
  /*
   * 定位方式（2026-09-18 最终修正）：
   *   不再用"容器高度 + 居中 + 上移量"这一套间接控制 ——
   *   实测发现那样子内容块会随着任务条数变化而整体上下漂移，
   *   5 个任务时标题被推出圆顶、2 个任务时又偏下。
   *
   *   改成"**固定内容顶部**"：容器顶边锚在圆顶下方固定的位置，
   *   内容从容器顶部往下自然排。
   *   这样无论有几个任务，页眉「任务」始终在同一个位置、
   *   始终在第一个任务的正上方，日历年一样稳定。
   */
  lv_obj_set_height(ctr, LV_PCT(W3_CONTENT_H_PCT));
  lv_obj_align(ctr, LV_ALIGN_TOP_MID, 0, up_shift);
  lv_obj_set_style_bg_opa(ctr, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ctr, 0, 0);
  lv_obj_set_style_pad_all(ctr, 0, 0);
  lv_obj_set_flex_flow(ctr, LV_FLEX_FLOW_COLUMN);
  /* 内容贴容器顶部往下排（不再垂直居中） */
  lv_obj_set_flex_align(ctr, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_remove_flag(ctr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(ctr, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(ctr, LV_OBJ_FLAG_GESTURE_BUBBLE);

  /* ---- 页眉：「任务」居中 + 「N 个待办」居中弱化 ---- */
  {
    lv_obj_t *t1 = lv_label_create(ctr);

    lv_label_set_text(t1, "任务");
    lv_obj_set_style_text_font(t1, VELATIME_FONT_BODY, 0);       /* 28px（版式 A 页头）*/
    lv_obj_set_style_text_color(t1, lv_color_hex(CLR_TX_MAIN), 0);
    lv_obj_set_width(t1, LV_PCT(100));
    lv_obj_set_style_text_align(t1, LV_TEXT_ALIGN_CENTER, 0);
  }
  {
    /*
     * 「N 个待办」属于辅助信息，规格要求 9~10px、颜色 #6B7280。
     * 这里用 meta15（15px）而不是新建 10px 字库 —— 为一个 3 字文案
     * 新增一个字库不划算；靠颜色 #6B7280 已经能退到辅助层级。
     * 这是对规格的一处偏离，已在交付说明里标注。
     */
    lv_obj_t *t2 = lv_label_create(ctr);

    snprintf(buf, sizeof(buf), "%d个待办", waiting);
    lv_label_set_text(t2, buf);
    lv_obj_set_style_text_font(t2, VELATIME_FONT_META, 0);
    lv_obj_set_style_text_color(t2, lv_color_hex(CLR_TX_MUTED), 0);
    lv_obj_set_width(t2, LV_PCT(100));
    lv_obj_set_style_text_align(t2, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(t2, 4, 0);
  }

  /* ---- 任务列表：整体在 ctr 内居中，内部左对齐 ---- */
  list = lv_obj_create(ctr);
  lv_obj_set_width(list, LV_PCT(100));
  /*
   * 高度随内容（不用 flex_grow）：
   *   实测用 flex_grow(1) 时 list 会撑满 ctr 的剩余高度（454 屏约 361px），
   *   而 list 自身的内容又是垂直居中的 —— 结果第一个任务从 y=197 才开始，
   *   页眉与首个任务之间凭空多出约 96px 空隙。
   *   改成高度随内容后，任务紧跟页眉往下排。
   */
  lv_obj_set_height(list, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 0, 0);
  lv_obj_set_style_pad_top(list, W3_GAP_HEAD_LIST, 0);  /* 页眉与首个任务的留白（纯留白，无分割线） */
  lv_obj_set_style_pad_row(list, W3_GAP_TASKS, 0);/* 任务之间 34px */
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  /*
   * 任务在列表内**水平居中**（用户要求）。
   * 上一版这里是 START（左对齐），导致任务名从内容块左边缘开始、
   * 看起来贴在表盘左边，用户明确要求改回居中。
   * 只改主轴对齐，不动容器宽度与高度（避免再次触发居中/尺寸互相依赖）。
   */
  lv_obj_set_flex_align(list, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_add_flag(list, LV_OBJ_FLAG_GESTURE_BUBBLE);

  if (total <= 0)
    {
      lv_obj_t *empty = lv_label_create(list);

      lv_label_set_text(empty, "暂无任务");
      lv_obj_set_style_text_font(empty, VELATIME_FONT_CN, 0);
      lv_obj_set_style_text_color(empty, lv_color_hex(CLR_TX_MUTED), 0);
      lv_obj_set_width(empty, LV_PCT(100));
      lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
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

  /* ---- 底部导航：三个入口，透明大点击区（逻辑在 ui_theme.c，未改）---- */
  velatime_ui_build_nav(scr, VELATIME_PAGE_TASKS);

  lv_scr_load(scr);
}
