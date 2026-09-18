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
#define CLR_DIAL     CLR_BG      /* 表盘底色：设计系统底色 #0E121C */
#define CLR_RIM      0x3A4356    /* 盘外圈细边（唯一保留的结构线，30% 不透明） */
#define CLR_NAV_ON   CLR_TX_SUB  /* 翻页栏：当前页文字 */
#define CLR_NAV_OFF  CLR_TX_MUTED/* 翻页栏：非当前页文字/圆点 */

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
 * 再留余量，最终取 66%（0.66 < 0.707，仍然安全）。
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
  /* 边界再弱化：目标是"感受到圆形表盘"，而不是"看到画了一个圆" */
  lv_obj_set_style_border_opa(dial, LV_OPA_30, 0);
  lv_obj_set_style_pad_all(dial, 0, 0);
  lv_obj_remove_flag(dial, LV_OBJ_FLAG_SCROLLABLE);
  /* 让圆盘也能把手势冒泡给屏幕，否则在盘面上滑动收不到 */
  lv_obj_add_flag(dial, LV_OBJ_FLAG_GESTURE_BUBBLE);

  return dial;
}

/*
 * 表盘几何：所有布局都以 (cx, cy, R_face) 为基准。
 * 按规范要求建立统一的几何参数，避免散落的魔法数字，
 * 未来换屏幕尺寸时整体结构仍然成立。
 */
void velatime_ui_face_metrics(int *cx, int *cy, int *r_face)
{
  int sw;
  int sh;
  int d;

  velatime_ui_screen_size(&sw, &sh);
  d = (sw < sh) ? sw : sh;

  if (cx != NULL)
    {
      *cx = sw / 2;
    }
  if (cy != NULL)
    {
      *cy = sh / 2;
    }
  if (r_face != NULL)
    {
      *r_face = d / 2;
    }
}

/*
 * 表盘背景（深空星空）
 * ==================================================================
 * 放在屏幕最底层、位于表盘圆之下，因此：
 *   - 表盘圆用半透明底盘（见 velatime_ui_set_dial_bg_opa），星空透出来；
 *   - 圆外的四角仍是屏幕纯黑底（表框），不会露出任何矩形边界。
 *
 * 素材：用户提供的深空星空图（1254x1254），特征为
 *   - 圆外纯黑（四角与四边中点均为 rgb(0,0,0)）
 *   - 圆内为深蓝星空，主色 #020710 ~ #0c1a2b，带星点
 *   - 中心略亮（暗蓝晕），平均亮度仅 7.1（比之前的月球暗约 6 倍）
 * 处理：缩放到 448x448（最接近真机 454），并把 96% 半径外强制压成纯黑，
 *  保留原图的"黑底 + 圆形星空"构图，四角不会露边。
 * 转成 RGB565 C 数组（约 392KB 固件）。
 *
 * 为什么用 C 数组而不是运行时读文件：
 *   先试过放 9p 共享目录（guest /share）和 FAT 持久盘（/data），
 *   分别遇到"V9FS 挂载未生效"和"宿主机缺 mtools 无法注入"的问题；
 *   C 数组零依赖、模拟器与真机都能用，且体积可接受（固件 14MB 级）。
 * 源图为 448x448，运行时按表盘直径等比缩放，所以模拟器(720)与真机(454)共用。
 */
#define VELATIME_BG_W 448

/* 由 velatime_bg_moon.c 提供（符号名沿用，内容已换成星空） */
LV_IMAGE_DECLARE(velatime_bg_moon);

lv_obj_t *velatime_ui_build_background(lv_obj_t *scr, int d)
{
  lv_obj_t *img;

  if (scr == NULL || d <= 0)
    {
      return NULL;
    }

  img = lv_image_create(scr);
  lv_image_set_src(img, &velatime_bg_moon);
  /* 源图 448x448，按表盘直径等比缩放到铺满 */
  lv_image_set_scale(img, (uint32_t)((uint32_t)d * 256U / VELATIME_BG_W));
  lv_obj_center(img);
  lv_obj_remove_flag(img, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(img, LV_OBJ_FLAG_GESTURE_BUBBLE);
  return img;
}

/* 表盘圆底盘的不透明度：有背景图时调低让它透出来，没背景则保持不透明 */
void velatime_ui_set_dial_bg_opa(lv_obj_t *scr, lv_opa_t opa)
{
  lv_obj_t *dial;
  int n;

  if (scr == NULL)
    {
      return;
    }

  /*
   * 表盘圆是 velatime_ui_style_screen() 里最后创建的对象，
   * 也就是屏幕当前最后一个子对象（背景图在它之前建，因此不能取第 0 个）。
   */
  n = lv_obj_get_child_count(scr);
  if (n <= 0)
    {
      return;
    }

  dial = lv_obj_get_child(scr, n - 1);
  if (dial != NULL)
    {
      lv_obj_set_style_bg_opa(dial, opa, 0);
    }
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



/* 当前页序（滑动切页用）：-1 表示不在三页之内（如详情页/通知页） */
static int g_page_index = VELATIME_PAGE_IDX_OTHER;

void velatime_ui_set_page_index(int idx)
{
  g_page_index = idx;
}

int velatime_ui_get_page_index(void)
{
  return g_page_index;
}

/* ---- 翻页栏的三个跳转 ---- */

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

/*
 * 常驻翻页栏 —— 「模拟器专用页面切换控件」。
 *
 * 为什么必须存在：真机靠滑动切页，但模拟器里手势不生效，
 * 没有它用户会卡在单页出不来。
 *
 * 为什么不能做成普通底部导航：W1 是 Watch Face，一条横向的
 * "任务/首页/课表"按钮条会立刻变成手机 App 观感，破坏表盘感。
 *
 * 视觉规格（2026-09-17 用户逐条指定，不得自由发挥）：
 *   - 三个文字严格水平对齐，不排成弧线、不左右倾斜
 *   - 位于圆盘下方区域，但在数字 6 的上方，绝不覆盖 6
 *   - 文字 12px、无背景、无边框、无卡片、无分割线、无阴影、无图标
 *   - 非当前页 #6B7280；当前页 #C3CBD6（稍亮）
 *   - 当前页文字下方一个小圆点（约 5px），低调冷白，无发光无动画
 *   - 视觉元素很小，但每一项都有一个透明的宽触摸区（约 19% 直径），
 *     保证模拟器上点得到；触摸区不绘制任何背景
 *
 * 位置推导（以圆盘直径 d 为基准，圆心为原点，y 向下为正）：
 *   导航整行中心  y = 15%d
 *   三项中心 x    -12.5%d / 0 / +12.5%d
 *   触摸区        item_w = 12.4%d，item_h = 13.5%d
 *
 * 与数字 6 的避让校验（454 圆屏，d=454，r=227）：
 *   小时数字环半径 = r*68% = 154，数字 56px 半高 28
 *   → 数字 6 的字形占 y_offset 126..182（屏内 353..409）
 *
 *   导航项中心 y_offset = 0.15*454 = 68，项高 61
 *   → 项占 37..99；容器内"文字(14) + 间隙(3) + 圆点(5)"= 22，
 *     垂直居中后文字中心 ≈ 68 - 11 + 7 = 64，即屏内 y≈291
 *   → 文字占 284..298，圆点占 301..306
 *   导航最低点 306 < 数字 6 字顶 353，留 47px 空隙，不重叠 ✓
 *
 *   与中心区（日期/信封）的关系：中心区在 y_offset +25，占 245..261（屏内），
 *   导航文字顶 284，留 23px 空隙，不挤压 ✓
 *
 *   水平：三项中心 x_offset = ±57，文字"课表"约 24px 宽半宽 12
 *   → 外沿 69；该高度圆半宽 = sqrt(227²-68²) ≈ 217，69 < 217，在圆内 ✓
 *   触摸区半宽 43 → 100 < 217，同样不出圆 ✓
 */
void velatime_ui_build_nav(lv_obj_t *scr, velatime_ui_page_t current)
{

  /* 顺序：左 任务(W3) · 中 首页(W1) · 右 课表(W2) */
  static const char *labels[3] = { "任务", "首页", "课表" };
  static void (*const handlers[3])(lv_event_t *) =
  {
    nav_to_tasks, nav_to_home, nav_to_schedule
  };
  static const velatime_ui_page_t pages[3] =
  {
    VELATIME_PAGE_TASKS, VELATIME_PAGE_HOME, VELATIME_PAGE_SCHEDULE
  };

  int sw;
  int sh;
  int d;
  int item_w;
  int item_h;
  int dot;
  int i;

  if (scr == NULL)
    {
      return;
    }

  velatime_ui_screen_size(&sw, &sh);
  d = (sw < sh) ? sw : sh;

  /*
   * 尺寸与位置（全部相对圆盘直径 d，不用屏幕高宽 ——
   * 模拟器屏幕是 720x800、圆盘只有 720，用屏幕尺寸会算到圆外）：
   *
   *   触摸区宽 item_w = 12.4%d  高 item_h = 13.5%d
   *     —— 12.4% 是"不重叠"的取值：三项中心间距 12.5%d，
   *        触摸区宽必须 < 12.5%d 才不重叠，取 12.4% 留约 0.5px 余量。
   *        454 屏 → 56 x 61 px；720 屏 → 89 x 97 px。
   *   圆点     dot     = 5px（规格 4~6px，不做成大圆）
   *   水平间距 ±12.5%d          （三项严格水平对齐，无弧线）
   *   纵向整行中心 +15%d         （位于数字 6 上方，不覆盖 6）
   *
   * 详细避让校验见函数头注释。
   */
  item_w = d * 124 / 1000;
  item_h = d * 135 / 1000;
  dot = 5;                       /* 规格：约 4~6px，不做成大圆 */
  if (item_w < 48)
    {
      item_w = 48;
    }
  if (item_h < 30)
    {
      item_h = 30;
    }

  for (i = 0; i < 3; i++)
    {
      /* 严格水平对齐：三项同一 y，且不排成弧线 */
      int xoff = (i - 1) * (d * 125 / 1000);   /* ±12.5% d */
      int yoff = d * 15 / 100;                 /* 整行中心 +15% d */
      int active = ((int)pages[i] == (int)current);
      lv_obj_t *item = lv_obj_create(scr);
      lv_obj_t *lab;
      lv_obj_t *ind;

      lv_obj_set_size(item, item_w, item_h);
      lv_obj_align(item, LV_ALIGN_CENTER, xoff, yoff);
      lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);   /* 透明触摸区 */
      lv_obj_set_style_border_width(item, 0, 0);
      lv_obj_set_style_pad_all(item, 0, 0);
      lv_obj_set_style_pad_row(item, 3, 0);              /* 文字与点的间隙 */
      lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
      lv_obj_set_flex_align(item, LV_FLEX_ALIGN_CENTER,
                            LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
      lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_add_flag(item, LV_OBJ_FLAG_GESTURE_BUBBLE);

      /* 文字：Noto Sans SC Regular 400 / 12px；非当前页冷灰，当前页稍亮 */
      lab = lv_label_create(item);
      lv_label_set_text(lab, labels[i]);
      lv_obj_set_style_text_font(lab, VELATIME_FONT_NAV, 0);
      lv_obj_set_style_text_color(lab,
                                  lv_color_hex(active ? CLR_NAV_ON
                                                      : CLR_NAV_OFF), 0);
      /* 非当前页再压一档不透明度，让当前页明显但不刺眼 */
      lv_obj_set_style_text_opa(lab, active ? LV_OPA_COVER : LV_OPA_70, 0);
      lv_obj_add_flag(lab, LV_OBJ_FLAG_GESTURE_BUBBLE);

      /*
       * 当前页指示圆点：只在当前页显示。
       * 非当前页保持完全透明（但仍占位），这样三项文字基线始终一致，
       * 不会因为有没有点而导致文字位置跳动。
       * 指示点用冷白 CLR_NAV_ON —— 不用强调红（红只留给通知红点）。
       */
      ind = lv_obj_create(item);
      lv_obj_set_size(ind, dot, dot);
      lv_obj_set_style_radius(ind, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_border_width(ind, 0, 0);
      lv_obj_set_style_bg_color(ind, lv_color_hex(CLR_NAV_ON), 0);
      lv_obj_set_style_bg_opa(ind, active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
      lv_obj_remove_flag(ind, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_remove_flag(ind, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_flag(ind, LV_OBJ_FLAG_GESTURE_BUBBLE);

      /* 当前页不加点击事件（点了也是原地），其余两项可点切页 */
      if (!active)
        {
          lv_obj_add_event_cb(item, handlers[i], LV_EVENT_CLICKED, NULL);
        }
    }
}

/*
 * 居中内容列：所有页面（W1–W5）的标题、列表、按钮都放进来，保证：
 *   - 内容在屏幕上居中，且各页面停靠方式完全一致；
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
