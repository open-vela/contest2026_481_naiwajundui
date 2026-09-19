#include "velatime_ui.h"
#include "../include/velatime_time.h"
#include "../core/core_recommend.h"
#include "../core/core_schedule.h"
#include "../core/core_task.h"
#include "../core/core_agent_sync.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/*
 * W1 = 真正的 Watch Face（圆形表盘），不是"放在圆里的网页 UI"。
 *
 * 视觉层级（严格按用户 2026-09-16 评审第二轮方案）：
 *   Level 1  时间       —— 整屏最亮最大，绝对视觉中心
 *   Level 2  日期、信封  —— 明显弱于时间，但仍清晰可读
 *   Level 3  12/3/6/9   —— 极弱，只用来建立"这是圆表"的方向感
 *
 * 构图（以圆心为锚，全部按半径比例推导，不套用矩形安全区）：
 *
 *          12
 *     9            3      <- 贴圆周，低对比度
 *         15:15           <- 时间：Montserrat 44，最亮
 *         Sep 16          <- 日期：约时间的 36% 大小
 *           ✉︎ ●           <- 信封 + 红点，红点在右上角
 *     6            8
 *          6
 *       任务 首页 课表      <- 翻页入口（功能需要，视觉压到最弱）
 *
 * 为什么不用矩形布局：方形 UI 塞进圆屏，正是"廉价 AI 手表 UI"的根源。
 * 所有位置都写成「圆心 + 半径比例」，天然贴合圆屏。
 */

/*
 * 颜色层级（精修版，严格按规范的四级关系）
 *
 *   第一层 时间      -> CLR_TEXT   接近纯白，最高可读性
 *   第二层 日期      -> CLR_DATE   提亮到明显可读，但不抢时间
 *   第三层 小时数字  -> CLR_HOUR   低饱和冷青灰，明显弱于时间
 *   第四层 翻页入口  -> 由 ui_theme.c 单独控制，权重最低
 *
 * 背景（月球）在时间与小时数字之下，通过降亮度/降对比"退后"，
 * 不用给文字加描边来解决可读性 —— 那是廉价做法。
 */
#define CLR_TEXT     0xFFFFFF    /* 时间：接近纯白 */
#define CLR_DATE     0xC3CBD6    /* 日期：提亮（原 #9AA3B4 偏弱） */
#define CLR_HOUR     0x8FA0AC    /* 小时数字：低饱和冷青灰，不用纯白 */
#define CLR_RED      0xFF3B30

/*
 * 字库全部由 velatime_ui.h 统一声明，严格按设计规范：
 *   时间        Roboto Regular 400，64px（VELATIME_FONT_TIME）
 *   12 小时数字  Roboto Light   300，56px（VELATIME_FONT_HOUR）
 *   日期/辅助    Roboto Regular 400，16px（VELATIME_FONT_UI）
 * 字号依据：拆解真实成品表盘（WFF，450×450）得知其时间用 size=70，
 * 即屏宽的 15.6%；圆周数字约 1/8 屏宽。换算到 454 圆屏分别取 72 / 56。
 */

static lv_obj_t *g_date_label = NULL;
static lv_obj_t *g_weekday_label = NULL;   /* 「星期四」：与日期同一行的独立标签 */
static lv_obj_t *g_envelope = NULL;
static lv_obj_t *g_badge = NULL;
static lv_timer_t *g_clock_timer = NULL;

/* 滑动切页用（见 on_indev_swipe 注释）—— 必须声明在所有使用者之前 */
static lv_obj_t *g_home_scr = NULL;      /* 首页屏幕，用来判断当前在不在表盘 */
static lv_point_t g_swipe_from;
static int        g_swipe_tracking = 0;

static char g_reminder[192] = "";

/* ---------------------------------------------------------------- */
/* 时间 / 日期                                                        */
/* ---------------------------------------------------------------- */

/*
 * 时间的三个标签：小时 / 冒号 / 分钟 分开建。
 *
 * 为什么拆开（2026-09-17 用户第二轮反馈"中间有点拥挤，放开一些"）：
 *   实测 Comfortaa 的字间距，四个字符之间的空白列数是 [14, 11, 8, 8, 11]，
 *   冒号两侧只有 8 列，是整串里最窄的地方。字号越大这个比例越明显，
 *   所以"挤"的感觉集中在冒号周围。
 *   LVGL 9 没有 letter-spacing 样式，因此改用三个标签 + 列间距，
 *   直接控制冒号两侧的留白 —— 这样无论以后换多大字号都能调。
 */
static lv_obj_t *g_time_hh = NULL;
static lv_obj_t *g_time_colon = NULL;
static lv_obj_t *g_time_mm = NULL;

/*
 * 冒号两侧的留白（像素）。
 *
 * 取值依据（112px Comfortaa 实测）：
 *   原始 "14:14" 的内部空隙为 [24, 12, 8, 9, 12] 列，
 *   冒号左右只有 12 / 9 列，是整串最窄处 —— 这就是"中间拥挤"的来源。
 *   取 GAP = 24 时，冒号两侧视觉留白约 24+2+2 = 28px，
 *   已明显宽于数字之间的天然空隙（24 列），冒号因此"透得过气"。
 *   再大（28/32）留白到 32/36px，会显得冒号与数字脱节，故取 24。
 *   此时时间总宽约 240px，半宽 120 < 3/9 点内沿 154，不会碰到小时数字。
 */
#define VELATIME_TIME_GAP 14


/* 数字文本缓冲：lv_label_set_text 不会复制字符串，
   所以这些缓冲必须是静态的，不能是栈上的临时数组 */
static char g_hh_buf[8] = "00";
static char g_mm_buf[8] = "00";

/*
 * 星期几的中文写法（2026-09-17 用户第四轮指令）。
 * tm_wday: 0=周日 ... 6=周六，与下面的表一一对应。
 * 用中文"星期X"而不是 "Thu"，与"9月17日"的中文日期风格保持一致。
 */
static const char *const g_weekday_cn[7] =
{
  "星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"
};

static void update_clock_inner(void)
{
  struct timespec ts;
  struct tm tm_now;
  char date_buf[32];

  /*
   * 2026-09-20: 不要再依赖 localtime_r / TZ。
   *
   * 板子上系统时钟是 UTC；NuttX 的 localtime_r 遇到 POSIX TZ 串
   * （如 "CST-8"）会去找 zoneinfo 文件，找不到就退回 UTC，
   * 于是表盘比北京时间慢 8 小时。
   * 这里和 ai_agent 保持一致：手工 +8 小时，再用 gmtime_r 拆字段，
   * 无论 TZ 是什么都显示北京时间。
   */
  if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
    {
      return;
    }

  if (velatime_localtime(ts.tv_sec, &tm_now) == NULL)
    {
      return;
    }

  /*
   * 小时与分钟分开写入各自的静态缓冲。
   * 不再拼成 "HH:MM" 一次性设置 —— 冒号是独立标签。
   */
  snprintf(g_hh_buf, sizeof(g_hh_buf), "%02d", tm_now.tm_hour);
  snprintf(g_mm_buf, sizeof(g_mm_buf), "%02d", tm_now.tm_min);

  if (g_time_hh != NULL)
    {
      lv_label_set_text(g_time_hh, g_hh_buf);
    }

  if (g_time_mm != NULL)
    {
      lv_label_set_text(g_time_mm, g_mm_buf);
    }

  if (g_date_label != NULL)
    {
      /*
       * 2026-09-17 用户第四轮指令：日期改用中文形式「9月17日」。
       * 原来用 "Sep 17"，中文日期与同一行的「星期四」风格统一。
       */
      snprintf(date_buf, sizeof(date_buf), "%d月%d日",
               tm_now.tm_mon + 1, tm_now.tm_mday);
      lv_label_set_text(g_date_label, date_buf);
    }

  if (g_weekday_label != NULL)
    {
      int wd = tm_now.tm_wday;      /* 0=周日 ... 6=周六 */

      if (wd < 0 || wd > 6)
        {
          wd = 0;
        }
      lv_label_set_text(g_weekday_label, g_weekday_cn[wd]);
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
  update_badge();
}

void velatime_ui_home_refresh(void)
{
  update_badge();
}

static void update_clock(void)
{
  /*
   * 野指针保护（2026-09-18）：
   * 页面切换用 auto_del=true 删除旧屏幕后，这些静态指针可能已被置空。
   * on_home_screen_unloaded 会停表，这里再加一道防护。
   */
  if (g_time_hh == NULL || g_time_mm == NULL)
    {
      return;
    }

  update_clock_inner();
}

static void clock_timer_cb(lv_timer_t *timer)
{
  (void)timer;
  update_clock();
}

/*
 * 屏幕即将被删除时调用（2026-09-18 新增）
 * ------------------------------------------------------------------
 * 页面切换现在用 lv_screen_load_anim(..., auto_del=true)，旧屏幕会在
 * 动画结束后被 lv_obj_delete 释放。
 * 但本页的时钟定时器是独立对象、不随屏幕释放，而且 g_time_* 等静态
 * 指针会变成野指针 —— 定时器下一次触发就访问已释放内存，直接段错误
 * （现象：点底部翻页栏立刻闪退）。
 *
 * LVGL 在删除屏幕前会先发 LV_EVENT_SCREEN_UNLOADED，
 * 因此在这里停表 + 清指针；再次进入本页时会重新建表。
 */
static void on_home_screen_unloaded(lv_event_t *e)
{
  (void)e;

  if (g_clock_timer != NULL)
    {
      lv_timer_delete(g_clock_timer);
      g_clock_timer = NULL;
    }

  g_home_scr       = NULL;
  g_swipe_tracking = 0;

  g_time_hh        = NULL;
  g_time_colon     = NULL;
  g_time_mm        = NULL;
  g_date_label     = NULL;
  g_weekday_label  = NULL;
  g_envelope       = NULL;
  g_badge          = NULL;
}

/* ---------------------------------------------------------------- */
/* 手势（LVGL 原生）—— 逻辑不变                                        */
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

  switch (dir)
    {
      case LV_DIR_TOP:    velatime_ui_popup_show();    break;
      case LV_DIR_LEFT:   velatime_ui_tasks_show();    break;
      case LV_DIR_RIGHT:  velatime_ui_schedule_show(); break;
      default: break;
    }
}

/*
 * 滑动切页（2026-09-19 v2：改用输入设备层）
 * ------------------------------------------------------------------
 * v1 把处理器挂在屏幕上，靠 LVGL 的事件冒泡 —— 实测真机上冒泡极不可靠：
 * 输入设备层收到 200+ 次事件，屏幕层只收到 6 次（约 3%），所以滑动基本无效。
 *
 * v2 直接把处理器挂在【输入设备】上，完全不经过冒泡，必定收到。
 * 为避免影响 W3/W2 列表内的上下滚动，只在【当前屏幕 == 首页表盘】时生效。
 */
#define SWIPE_MIN_PERMILLE  120    /* 最小滑动距离 = 屏径 × 120‰（454 屏约 54px） */

static int       g_indev_hooked = 0;     /* indev 回调只挂一次 */

static int swipe_min_len(void)
{
  int w = 0;
  int h = 0;

  velatime_ui_screen_size(&w, &h);

  return ((w < h) ? w : h) * SWIPE_MIN_PERMILLE / 1000;
}

static void on_indev_swipe(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  lv_indev_t *indev = lv_indev_active();
  lv_point_t now;
  int dx;
  int dy;
  int ax;
  int ay;
  int lim;

  /*
   * v3：横向滑动【不限制页面】—— v2 限定只在表盘生效，
   * 结果一旦切到课表/任务页，再滑就完全没反应（用户体感：滑动没用）。
   * 因为下面要求 |dx| > |dy| 才算横向，列表内的上下滚动不会受影响。
   */
  (void)0;

  if (indev == NULL)
    {
      return;
    }

  lv_indev_get_point(indev, &now);

  if (code == LV_EVENT_PRESSED)
    {
      g_swipe_from = now;
      g_swipe_tracking = 1;
      return;
    }

  if (code != LV_EVENT_RELEASED)
    {
      return;
    }

  if (!g_swipe_tracking)
    {
      return;
    }

  g_swipe_tracking = 0;

  dx = now.x - g_swipe_from.x;
  dy = now.y - g_swipe_from.y;
  ax = (dx < 0) ? -dx : dx;
  ay = (dy < 0) ? -dy : dy;
  lim = swipe_min_len();

  if (ax < lim && ay < lim)
    {
      return;                      /* 位移太小 → 当点击，不切页 */
    }

  if (ax >= ay)
    {
      /*
       * 横向滑动 —— 胶片模型（2026-09-19 修正）
       * ------------------------------------------------------------
       * 导航栏布局：  任务(0) | 首页(1) | 课表(2)
       *   手指往左滑 = 内容往左移 = 露出【右边】那一页 → 页序 +1
       *   手指往右滑 = 内容往右移 = 露出【左边】那一页 → 页序 -1
       * 到两端就不动（不回绕）。
       * 原来映射是反的：左滑→任务、右滑→课表，
       * 导致在任务列表左滑还是去任务，看起来"回不到首页"。
       */
      int cur = velatime_ui_get_page_index();

      /*
       * 详情页 / 通知页的滑动返回（2026-09-19）
       * W4 没有底部导航，只剩左上角小箭头，圆屏上不好点 ——
       * 用户反馈"进了详情页出不来"。这里让横向滑动直接返回。
       */
      if (cur == VELATIME_PAGE_IDX_DETAIL)
        {
          printf("VelaTime: swipe back from detail -> tasks (d=%d,%d)\n", dx, dy);
          fflush(stdout);
          velatime_ui_tasks_show();
          return;
        }

      if (cur == VELATIME_PAGE_IDX_NOTIFY)
        {
          printf("VelaTime: swipe dismiss notify -> home (d=%d,%d)\n", dx, dy);
          fflush(stdout);
          velatime_ui_home_show();
          return;
        }

      if (dx < 0)
        {
          if (cur == VELATIME_PAGE_IDX_TASKS)
            {
              printf("VelaTime: swipe left -> home (d=%d,%d)\n", dx, dy);
              fflush(stdout);
              velatime_ui_home_show();
            }
          else if (cur == VELATIME_PAGE_IDX_HOME)
            {
              printf("VelaTime: swipe left -> schedule (d=%d,%d)\n", dx, dy);
              fflush(stdout);
              velatime_ui_schedule_show();
            }
          else
            {
              printf("VelaTime: swipe left ignored (page=%d)\n", cur);
              fflush(stdout);
            }
        }
      else
        {
          if (cur == VELATIME_PAGE_IDX_SCHEDULE)
            {
              printf("VelaTime: swipe right -> home (d=%d,%d)\n", dx, dy);
              fflush(stdout);
              velatime_ui_home_show();
            }
          else if (cur == VELATIME_PAGE_IDX_HOME)
            {
              printf("VelaTime: swipe right -> tasks (d=%d,%d)\n", dx, dy);
              fflush(stdout);
              velatime_ui_tasks_show();
            }
          else
            {
              printf("VelaTime: swipe right ignored (page=%d)\n", cur);
              fflush(stdout);
            }
        }
    }
  else if (dy < 0 && lv_screen_active() == g_home_scr)
    {
      /* 上滑只在表盘生效：列表页的上滑是滚动，不能抢 */
      printf("VelaTime: swipe up -> notify (d=%d,%d)\n", dx, dy);
      fflush(stdout);
      velatime_ui_popup_show();              /* 上滑 → 通知中心 */
    }
}

/* 把滑动处理器挂到所有输入设备上（全局只挂一次） */
static void hook_indev_swipe(void)
{
  lv_indev_t *indev;

  if (g_indev_hooked)
    {
      return;
    }

  indev = lv_indev_get_next(NULL);

  while (indev != NULL)
    {
      lv_indev_add_event_cb(indev, on_indev_swipe, LV_EVENT_PRESSED, NULL);
      lv_indev_add_event_cb(indev, on_indev_swipe, LV_EVENT_RELEASED, NULL);
      indev = lv_indev_get_next(indev);
    }

  g_indev_hooked = 1;
}


static void on_envelope_click(lv_event_t *e)
{
  (void)e;
  velatime_ui_popup_show();
}

/* ---------------------------------------------------------------- */
/* 构建                                                              */
/* ---------------------------------------------------------------- */

void velatime_ui_init(void)
{
}

/*
 * 完整 12 小时数字圆周系统。
 *
 * 参考真实智能手表表盘后的结论：**数字不该只放在四个方向，
 * 而应沿圆周均匀铺满 12 个位置**，这样整块表才"立得住"，
 * 而不是"几个元素飘在一个圆里"。
 *
 * 做法要点：
 *   1. 12 个位置用查表给出（不用三角函数，避免浮点/额外依赖）；
 *      表值 = 圆周上单位圆坐标 × 1000，按圆心居中定位。
 *   2. **半径是反推出来的**，不是写死：取所有数字都能完整落进圆内的最大半径。
 *      推导：数字半高约 h/2，"1点/11点"这类斜角位置最靠外，
 *      需满足 r + (h/2)·√2·(x/r) 之类的约束；保守取 r = 0.66d。
 *      454 屏 → r≈150px，圆周恰好落在盘内，且给中心信息留出足够空间。
 *   3. **字号分两级**（借 SimpleWatchFace3 的比例）：
 *      12/3/6/9 用大一号（1/8 屏宽），其余用 1/12 屏宽。
 *      两者都"看得见但不抢时间"——时间仍是唯一焦点。
 *   4. 极细观感靠**低不透明度 + 中等字号**实现（本字库无 Light 字重）。
 *
 * 表值说明（x, y 均为相对半径的千分比，圆心为原点，y 向下为正）：
 *   12点(0,-1000)  1点(500,-866)   2点(866,-500)   3点(1000,0)
 *    4点(866,500)  5点(500,866)    6点(0,1000)     7点(-500,866)
 *    8点(-866,500) 9点(-1000,0)   10点(-866,-500) 11点(-500,-866)
 * 实际使用 66% 的收敛系数（由 r 本身携带），故表值直接用单位圆。
 */
static void build_hour_marks(lv_obj_t *scr, int d, int r)
{
  static const struct
  {
    const char *text;
    int x;       /* 单位圆坐标 ×1000 */
    int y;
    int major;   /* 1 = 12/3/6/9，用大一号字 */
  } marks[12] =
  {
    { "12",     0, -1000, 1 },
    { "1",    500,  -866, 0 },
    { "2",    866,  -500, 0 },
    { "3",   1000,     0, 1 },
    { "4",    866,   500, 0 },
    { "5",    500,   866, 0 },
    { "6",      0,  1000, 1 },
    { "7",   -500,   866, 0 },
    { "8",   -866,   500, 0 },
    { "9",  -1000,     0, 1 },
    { "10",  -866,  -500, 0 },
    { "11",  -500,  -866, 0 }
  };
  int i;

  (void)d;

  for (i = 0; i < 12; i++)
    {
      lv_obj_t *lab = lv_label_create(scr);

      /*
       * 字号依据真实成品表盘：它的圆周数字用屏宽的 1/8 ≈ 12.5%，
       * 我们 454 屏 → 约 57px。这里用本地生成的 56px 数字字库
       * （只含 0-9，仅 22KB，见 ui/velatime_font_digits56.c）。
       * 之前用 20/16px，只有参考值的 1/3，所以显得又小又不明显。
       */
      /* 规范指定：12 个小时数字用 Roboto Light 300 */
      lv_label_set_text(lab, marks[i].text);
      lv_obj_set_style_text_font(lab, VELATIME_FONT_HOUR, 0);
      lv_obj_set_style_text_color(lab, lv_color_hex(CLR_HOUR), 0);
      /* 更轻更克制：不透明度下调，让时间成为绝对焦点 */
      lv_obj_set_style_text_opa(lab, marks[i].major ? LV_OPA_60 : LV_OPA_40, 0);
      lv_obj_align(lab, LV_ALIGN_CENTER,
                   r * marks[i].x / 1000, r * marks[i].y / 1000);
      enable_gesture_bubble(lab);
    }
}

/*
 * 信封图标（outline 风格）+ 右上角红点。
 *
 * 之前用"一个矩形 + 一条横线"，看起来就是个空框、完全不像信封，
 * 所以这里补上真正的折角：用 lv_line 画 V 形封口，
 * 这是最省事又能让人一眼认出的画法。
 *
 * 2026-09-17 用户第四轮指令：信封"适当缩小一些"，并与日期/星期排在同一行
 * （示意图：9月17日  星期四  ✉•）。
 * 因此本函数不再自己决定位置，改为由调用方传入父容器（date_row），
 * 尺寸由 d*7% 缩到 d*5%，文字放大后比例才协调。返回信封按钮对象。
 */
static lv_obj_t *build_envelope(lv_obj_t *parent, int d)
{
  lv_obj_t *body;
  lv_obj_t *flap;
  static lv_point_precise_t flap_pts[3];
  int box = d * 5 / 100;            /* 信封外框边长（原为 d*7%，按要求缩小） */
  int bw;
  int bh;
  int badge;

  if (parent == NULL)
    {
      return NULL;
    }

  if (box < 22)
    {
      box = 22;
    }
  badge = box / 5;
  if (badge < 4)
    {
      badge = 4;
    }
  if (badge > 7)
    {
      badge = 7;
    }
  bw = box;
  bh = box * 70 / 100;

  /* 信封位于"日期 星期"这一行的右端，纵向由 flex 自动居中对齐 */
  g_envelope = lv_button_create(parent);
  lv_obj_set_size(g_envelope, box, box);
  lv_obj_set_style_bg_opa(g_envelope, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(g_envelope, 0, 0);
  lv_obj_set_style_shadow_width(g_envelope, 0, 0);
  lv_obj_set_style_pad_all(g_envelope, 0, 0);
  lv_obj_remove_flag(g_envelope, LV_OBJ_FLAG_SCROLLABLE);
  enable_gesture_bubble(g_envelope);

  /* 信封主体：细边圆角矩形 */
  body = lv_obj_create(g_envelope);
  lv_obj_set_size(body, bw, bh);
  lv_obj_center(body);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(body, 2, 0);
  lv_obj_set_style_border_color(body, lv_color_hex(CLR_DATE), 0);
  lv_obj_set_style_radius(body, 3, 0);
  lv_obj_set_style_pad_all(body, 0, 0);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_CLICKABLE);
  enable_gesture_bubble(body);

  /* 折角：V 形封口，这是"信封"能被一眼认出的关键 */
  flap_pts[0].x = 0;
  flap_pts[0].y = 0;
  flap_pts[1].x = bw / 2;
  flap_pts[1].y = bh * 58 / 100;
  flap_pts[2].x = bw;
  flap_pts[2].y = 0;

  flap = lv_line_create(body);
  lv_line_set_points(flap, flap_pts, 3);
  lv_obj_set_size(flap, bw, bh);
  lv_obj_align(flap, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_line_width(flap, 2, 0);
  lv_obj_set_style_line_color(flap, lv_color_hex(CLR_DATE), 0);
  lv_obj_set_style_line_rounded(flap, true, 0);
  lv_obj_remove_flag(flap, LV_OBJ_FLAG_CLICKABLE);
  enable_gesture_bubble(flap);

  /* 红点：贴在信封右上角，与信封构成一个整体（不再单独漂浮） */
  g_badge = lv_obj_create(g_envelope);
  lv_obj_set_size(g_badge, badge, badge);
  lv_obj_align(g_badge, LV_ALIGN_TOP_RIGHT, 1, -1);
  lv_obj_set_style_radius(g_badge, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(g_badge, lv_color_hex(CLR_RED), 0);
  lv_obj_set_style_bg_opa(g_badge, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(g_badge, 0, 0);
  lv_obj_remove_flag(g_badge, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(g_badge, LV_OBJ_FLAG_CLICKABLE);
  enable_gesture_bubble(g_badge);

  lv_obj_add_event_cb(g_envelope, on_envelope_click, LV_EVENT_CLICKED, NULL);

  return g_envelope;
}

void velatime_ui_home_show(void)
{
  velatime_ui_set_page_index(VELATIME_PAGE_IDX_HOME);
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

  /*
   * 圆周数字的环半径 —— 由几何反推，不是随手写的。
   *
   * 圆盘半径 r = d/2。数字是方块，斜角位置（1/11/5/7 点）最靠外，
   * 需满足：R + (h/2)·(cos45+sin45) ≤ r，h≈20px、d=454、r=227：
   *   R ≤ 227 - 10×1.414 ≈ 213
   * 数字字号提到 56px 后，环半径也要相应外推，
   * 让数字环与中心的时间拉开距离：
   *   r = 227（454 屏），取 R = 0.68r ≈ 154px
   *   数字 56px 半高 34 → 外沿 154+34=188 < 227，不出圆
   *   64px 时间半宽约 80 → 与 3/9 点（x=±154）有 74px 余量
   */
  int ring = r * 68 / 100;

  /*
   * 背景层次（自下而上）：
   *   1. 屏幕纯黑底  -> 圆外四角，形成表框
   *   2. 星空图       -> 编译期内嵌（velatime_bg_moon.c）
   *   3. 表盘圆       -> 底盘半透明，让星空透出来
   *   4. 全部内容     -> 时间/数字/日期/通知/翻页栏
   * 先建背景，再建表盘圆，保证 z 序正确。
   */
  lv_obj_t *bg = velatime_ui_build_background(scr, d);

  /* 共用主题：纯黑底（圆外）+ 表盘圆 + 中文字库 + 关滚动 */
  velatime_ui_style_screen(scr);

  /* 记录首页屏幕（滑动切页只在表盘上生效）并挂输入设备回调 */
  g_home_scr = scr;
  hook_indev_swipe();

  /* 屏幕被 auto_del 删除前，先停掉时钟定时器并清空静态指针（否则野指针崩溃） */
  lv_obj_add_event_cb(scr, on_home_screen_unloaded, LV_EVENT_SCREEN_UNLOADED, NULL);

  /*
   * 星空背景很暗（全图平均亮度 7.1），底盘不透明度必须比月球时期低，
   * 否则星点会被压没。20% 时中心混合结果约为 rgb(9,15,25)，
   * 比圆外纯黑明显亮，星点可见，同时远暗于时间/数字。
   */
  velatime_ui_set_dial_bg_opa(scr, (bg != NULL) ? LV_OPA_20 : LV_OPA_COVER);

  /*
   * ---- Level 1：时间，整屏唯一视觉焦点 ----
   *
   * 2026-09-17 用户指令（第三轮）：
   *   "再大一些；时间的中间有点拥挤，要让中间放开一些、多空一些"
   *   并且明确指出**不要再上移**（上移量保持 r*14% 不动）。
   *
   * 本轮两处改动：
   *   1) 字号  104px -> 112px（相对最初 Roboto 64px 为 +75%）
   *   2) 结构  单个 "HH:MM" 标签 -> 三个标签（小时 / 冒号 / 分钟）
   *            中间用 VELATIME_TIME_GAP 控制留白
   *
   * 为什么要拆结构：
   *   实测 Comfortaa 的字间距，四个字符之间空白列数为 [14, 11, 8, 8, 11]，
   *   冒号两侧只有 8 列，是全串最窄处 —— 字号越大，"挤"越集中在冒号。
   *   LVGL 9 没有 letter-spacing 样式，所以改用三标签 + 列间距直接控制留白，
   *   这样以后无论换多大字号都能独立调开。
   *
   * 位置：仍为屏幕水平居中、上移 r*14%（用户要求不动）。
   *
   * 本轮只动时间，其它元素一律未改：
   *   日期、信封、红点、12 小时数字、翻页栏、背景 全部保持原样。
   */
  {
    lv_obj_t *trow = lv_obj_create(scr);

    /* 容器尺寸自适应内容，无背景无边框，只做横向排列 */
    lv_obj_set_size(trow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(trow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(trow, 0, 0);
    lv_obj_set_style_pad_all(trow, 0, 0);
    /* 冒号两侧的留白就由这里控制 */
    lv_obj_set_style_pad_column(trow, VELATIME_TIME_GAP, 0);
    lv_obj_set_flex_flow(trow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(trow, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(trow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(trow, LV_OBJ_FLAG_CLICKABLE);
    enable_gesture_bubble(trow);

    /* 三块文字共用同一套字体与颜色 */
    g_time_hh = lv_label_create(trow);
    lv_obj_set_style_text_font(g_time_hh, VELATIME_FONT_TIME, 0);
    lv_obj_set_style_text_color(g_time_hh, lv_color_hex(CLR_TEXT), 0);
    lv_label_set_text(g_time_hh, "00");
    enable_gesture_bubble(g_time_hh);

    g_time_colon = lv_label_create(trow);
    lv_obj_set_style_text_font(g_time_colon, VELATIME_FONT_TIME, 0);
    lv_obj_set_style_text_color(g_time_colon, lv_color_hex(CLR_TEXT), 0);
    lv_label_set_text(g_time_colon, ":");
    enable_gesture_bubble(g_time_colon);

    g_time_mm = lv_label_create(trow);
    lv_obj_set_style_text_font(g_time_mm, VELATIME_FONT_TIME, 0);
    lv_obj_set_style_text_color(g_time_mm, lv_color_hex(CLR_TEXT), 0);
    lv_label_set_text(g_time_mm, "00");
    enable_gesture_bubble(g_time_mm);

    /* 整行居中，并按用户要求保持上移 r*14%（本轮不上移） */
    lv_obj_align(trow, LV_ALIGN_CENTER, 0, -(r * 14 / 100));
  }

  /*
   * ---- Level 2：日期 / 星期 / 信封（一整行）----
   *
   * 2026-09-17 用户第四轮指令，按示意图排布：
   *
   *          14:14              <- 时间（上一轮已定稿，本轮不动）
   *
   *      9月17日  星期四  ✉•     <- 本行：日期 星期 信封
   *
   *   要求逐条落实：
   *     1) 日期改为中文形式「9月17日」（原来是 "Sep 17"）
   *     2) 后面接「星期四」，用中文星期
   *     3) 字号稍微调大：16px -> 20px（新建 velatime_font_info20 字库）
   *     4) 信封适当缩小：d*7% -> d*5%
   *     5) 三者排成一行，整行水平居中
   *
   * 纵向位置沿用原来的 r*11%，因此**位于时间下方固定不动**；
   * 三个元素靠 flex 自动垂直居中对齐，不用各自算偏移。
   * 时间那一行本轮完全未改。
   */
  {
    lv_obj_t *drow = lv_obj_create(scr);

    lv_obj_set_size(drow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(drow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(drow, 0, 0);
    lv_obj_set_style_pad_all(drow, 0, 0);
    /* 日期与星期之间的间距，以及星期与信封之间的间距 */
    lv_obj_set_style_pad_column(drow, d * 3 / 100, 0);
    lv_obj_set_flex_flow(drow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(drow, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(drow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(drow, LV_OBJ_FLAG_CLICKABLE);
    enable_gesture_bubble(drow);

    /* 日期：「9月17日」，用新建的 20px 字库 */
    g_date_label = lv_label_create(drow);
    lv_obj_set_style_text_font(g_date_label, VELATIME_FONT_INFO, 0);
    lv_obj_set_style_text_color(g_date_label, lv_color_hex(CLR_DATE), 0);
    lv_label_set_text(g_date_label, "9月17日");
    enable_gesture_bubble(g_date_label);

    /* 星期：「星期四」 */
    g_weekday_label = lv_label_create(drow);
    lv_obj_set_style_text_font(g_weekday_label, VELATIME_FONT_INFO, 0);
    lv_obj_set_style_text_color(g_weekday_label, lv_color_hex(CLR_DATE), 0);
    lv_label_set_text(g_weekday_label, "星期四");
    enable_gesture_bubble(g_weekday_label);

    /* 信封：缩小后接在行尾，整行由 flex 居中 */
    (void)build_envelope(drow, d);

    /* 整行纵向沿用原日期位置 r*11%，即固定在时间下方 */
    lv_obj_align(drow, LV_ALIGN_CENTER, 0, r * 11 / 100);
  }

  /* ---- Level 3：完整的 12 小时数字圆周 ---- */
  build_hour_marks(scr, ring, ring);

  /* 翻页入口：模拟器专用切换控件，保留交互，视觉压到最弱 */
  velatime_ui_build_nav(scr, VELATIME_PAGE_HOME);

  /* 屏幕接收冒泡上来的手势 */
  lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
  enable_gesture_bubble(scr);
  lv_obj_add_event_cb(scr, on_home_gesture, LV_EVENT_GESTURE, NULL);

  /*
   * 手动滑动检测：LVGL 的 gesture 在真机上不触发（见上面注释），
   * 这里用 PRESSED + RELEASED 的两个坐标点自己算方向。
   */

  if (g_clock_timer == NULL)
    {
      g_clock_timer = lv_timer_create(clock_timer_cb, 1000, NULL);
    }

  update_clock();
  update_badge();
  lv_scr_load(scr);
}
