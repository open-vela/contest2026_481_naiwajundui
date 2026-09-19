# VelaTime 开发时间线（AI Coding 日志）

> 记录方式：每完成一个阶段就补一条，包含"做了什么 / 遇到什么 / 怎么验证"。
> 时间跨度：2026-08-31 ~ 2026-09-14

## 阶段 0：选题与报名（8/31 ~ 9/1）

- 目标：从零选一个适合初学者、又符合"AI 硬件产品创新"赛道的项目
- 过程：对比医疗健康等方向（数据来源与专业性风险高），最终定为
  **VelaTime —— 主动式 AI 学生时间管家**（面向高校学生，Agent 规划 + 设备触达）
- 结论：报名表四项（定位 / 开发板 / 功能与硬件能力 / 技术方案）定稿；
  开发板改为 **BES 2800BP**（原选 Gemini-S1 已发放完毕导致首次被拒）

## 阶段 1：环境搭建（9/4 ~ 9/5）

- 做了什么：VMware Ubuntu 22.04 → openvela 全量 `repo sync` → 交叉编译 → 模拟器启动
- 遇到的问题与解决：
  1. `repo sync` 反复中断、只同步了部分仓库 → 补装 `git-lfs` 后完整同步
  2. 链接报 `libgui_wrapper.a: file format not recognized` → 该文件是 Git LFS 指针，
     `git lfs pull` 取回真实 189MB 静态库
  3. 模拟器黑屏 → 排查确认内核/服务正常，根因是**当前 defconfig 没有默认 Launcher**；
     `lvgldemo` 可显示即证明图形栈正常
- 验证：`#### build completed successfully ####` + 模拟器出现小米窗口并能跑 `lvgldemo`

## 阶段 2：AI Agent 打通（9/5）

- 做了什么：配置 MiMo Token Plan、通过代理访问、验证 `ai_agent` 对话
- 遇到的问题与解决：
  1. `401 Invalid API Key` → 三个原因叠加：`tp-` 开头的是 **Token Plan 专属 Key**，
     必须用 `token-plan-cn.xiaomimimo.com/v1`；模拟器请求**没走代理**；
     以及一次命令过长导致 Key 截断
  2. 最终配置：`set_proxy 10.0.2.2 7897`（虚拟机内 Clash）+ `set_llm <token-plan 端点>`
- 验证：串口日志出现 `TLS handshake OK ... via proxy` 与 `[Agent]: OK`

## 阶段 3：VelaTime 应用与核心逻辑（9/5 ~ 9/6）

- 做了什么：
  - LVGL 应用骨架（`nuttx_add_application` + manifest linkfile）
  - `core_task`（任务存储）、`core_schedule`（课程与空闲窗口）、`core_recommend`（评分推荐）
  - 首页推荐卡片、Start/Delay 状态机
- 遇到的问题与解决（编译期全部被 `-Werror` 拦下，逐条修掉）：
  1. `INCDIR` 在本工程不生效 → 所有 include 改**相对路径**
  2. 缺 `<stdio.h>` → `snprintf` 隐式声明
  3. 静态函数先调用后定义 → 调整顺序
  4. `format-truncation` → `VELATIME_MAX_ID` 16 → 24
  5. 新增 `.c` 未加入 `CMakeLists.txt` → SRCS 手工登记
- 验证：模拟器显示推荐卡片，点按钮出现 `Started` / `Postponed`

## 阶段 4：与 Agent 打通（9/6 ~ 9/7）

- 做了什么：
  - 自定义 Skill `student-task-planner`，安装为 `/data/ai_agent/skills/task-manager.md`
  - Agent 自然语言建任务 → 写入 `/data/ai_agent/TASKS.md`
  - VelaTime 读取该文件并导入任务
- 遇到的问题与解决：
  1. `agent_loop.c` 的"单轮单工具捷径"让"先读后写"流程中断 → 修改为
     `read_file`/`edit_file` 必须回到模型（该文件属公共源码，比赛仓不含此补丁，
     故 Skill 要求"同轮并行调用 `get_current_time` + `read_file`"以自保）
  2. 两个相似 Skill 竞争（模型总选内置的）→ 改为**覆盖写入内置 `task-manager.md`**
  3. `/data` 数据"丢失" → 真因是构建脚本每次 `mkfs.fat` 重建
     `cmake_out/.../vela_data.bin`；改用独立数据盘
     `-data /home/yy/openvela-persistent/vela_data.bin` 后彻底解决
- 验证：Agent 回复 `OK: wrote ... TASKS.md`；VelaTime 日志
  `parsed N task(s)` / `imported N task(s)`，界面显示任务标题

## 阶段 5：运行时自动同步与紧迫度（9/10、9/14）

- 做了什么：
  - `core_task_replace_all` 安全批量替换 + 内容哈希"连续两次稳定才应用"
  - LVGL 1 秒定时器驱动同步，界面原地刷新
  - 修复截止日期解析：支持 `YYYY-MM-DD` 与 `YYYY-MM-DD HH:MM`；
    紧迫度按日历天分层（逾期/今天/明天/3天内/一周内/更远），
    逾期超一天自动降级；推荐理由改为可解释文案
- 遇到的问题与解决：
  - **推荐分数曾经全部相同**：Agent 写 `[YYYY-MM-DD]`，旧解析要求带时分 →
    解析失败走兜底分，推荐顺序失真
  - 按"小时差"判断导致 22:00 时把明早 08:30 说成"今天" → 改为按日历天判断
- 验证：宿主机 gcc 编译真实源码跑单元测试，15 项全部通过（见 `verification.md`）

## 阶段 6：中文字库（9/14）

- 问题：中文显示为方块。内置 `lv_font_simsun_16_cjk` 是精简字库（连"业"字都没有），
  `font_multilang_small` 仅约 103 个汉字
- 方案：用 `lv_font_conv` 生成 **GB2312 一级汉字 3755 字 + ASCII + 标点**的
  16px/4bpp 字库（3886 字形，3.10MB），并在根对象设置字体让子控件继承
- 遇到的问题与解决：
  - lv_font_conv 要求 Node ≥ 14，而 Ubuntu 22.04 只有 Node 12 →
    改为在 Windows 侧生成字库，虚拟机只需拷 `.c` 文件
  - NuttX 的 `.config` 手改不生效 → 需要重新配置以重写 `nuttx/config.h`
- 验证：界面中文正常（含"业"字），推荐卡片无溢出

## 阶段 7：主动提醒（9/14）

- 需求：赛道要求"至少 1 个主动 + 执行场景"
- 实现：
  - VelaTime 启动时写 `/data/ai_agent/HEARTBEAT.md`（含待办）并加载到 agent 目录
  - Agent 的 heartbeat 线程（`agent_config.h` 默认 30 分钟）读到有待办，
    **无需用户输入**即发起一次思考：读 `TASKS.md`、选最紧急任务、
    写一行到 `/data/ai_agent/REMINDER.txt`
  - VelaTime 每秒轮询该文件，出现新内容即显示到界面卡片
  - 额外：端侧也能**立即**生成一次提醒（不依赖网络/模型），保证离线可演示
- 验证：
  - 单元测试 13 项全部通过（publish/check/去重/模板过滤/无待办不打扰）
  - 固件符号检查：`heartbeat armed for %d pending task(s)`、
    `VelaTime: proactive reminder: %s`、`REMINDER.txt` 均在固件中

## 阶段 8：交付（9/14）

- README（项目说明、架构、构建运行、避坑、赛道要求对照）
- 本 `logs/` 目录（时间线、会话摘要、提示词、验证记录）
- 代码按功能分组提交并推送 fork，后续合并回专属仓

## 阶段 9：真机打通（9/18 ~ 9/19）

**目标**：把 VelaTime 从模拟器搬到 BES2800BP 真板子（454 圆屏）上跑起来。

- 做了什么：
  - 纠正一个此前的错误估算：字库的 `.c` 源文件 31MB 是十六进制文本，
    编译后只有 bitmap 数据 **4.93MB**。官方 AP 镜像 1.61MB + 字库 4.93MB
    = 6.54MB < 分区 9.50MB，**装得下**（之前判断"装不下"是拿源文件大小
    当二进制体积）
  - 板级 defconfig 启用 `CONFIG_EXAMPLES_VELATIME` 与
    `CONFIG_NETUTILS_WEBCLIENT` / `CONFIG_NETUTILS_CJSON`
  - 编出 AP 镜像 `nuttx_ap.bin`（6.09MB，全量约 13 分钟）
  - 用 `dldtool.exe` 烧录成功
- 遇到的问题与解决：
  1. **烧录成功但屏幕无变化** —— 串口 `ps` 发现官方 `lvgldemo widgets`
     还在跑占着显示驱动；且 `velatime_main` 开头有
     `if (lv_is_initialized()) return -1;`，两个应用不能共存。
     改 `rcS.ap`：`lvgldemo widgets &` → `velatime &`
  2. **改了 rcS.ap 但镜像 MD5 一模一样** —— `add_board_rcsrcs()` 是在
     CMake **配置阶段**把 `#include "rcS.ap"` 展开成最终 `rcS` 的，
     只跑 ninja 不会重新展开。必须 `touch` 板级 `CMakeLists.txt` 并删除
     生成的 `rcS`/`romfs.img` 强制重配。
     **判断方法**：ninja 目标数只有 ~18 就是没重配，2000+ 才对
  3. 显示设备是 `/dev/fb0`（不是 `/dev/lcd0`）；
     `CONFIG_LV_USE_NUTTX_LCD` 未设置，`lv_nuttx_dsc_init()` 默认就给
     `/dev/fb0`，正好对上
- 验证：`ps` 显示 `PID 11 COMMAND: velatime`，应用开机自启

## 阶段 10：滑动切页（9/19）

- 需求：真机上手势切页不可用（只有底部按钮能切）
- 排查过程（关键证据都来自串口日志）：
  1. 先确认**点击是好的** —— 串口能看到 `VelaTime: task postponed`，
     说明按钮回调有触发，问题只在手势
  2. 怀疑 LVGL 手势依赖 `PRESSING` 阶段累加的 `gesture_sum`，
     而板子触摸驱动可能只上报按下/抬起
  3. 加触摸调试输出并烧录，用 120 秒串口监听抓真实数据 ——
     **INDEV 层（输入设备）收到 200+ 次事件，屏幕层只收到 6 次**：
     LVGL 的事件冒泡在真机上成功率仅约 **3%**
- 最终方案（三次迭代）：
  - v1 处理器挂屏幕层（依赖冒泡）→ 基本收不到，无效
  - v2 挪到**输入设备层** `lv_indev_add_event_cb`（不经过冒泡）→
     能收到了，但守卫写成"只在表盘生效"，切到别的页后滑动全被忽略
  - v3 横向滑动放宽到**任意页面**（要求 `|dx| > |dy|`，所以列表内的
     上下滚动不受影响），上滑仍只在表盘
  - 方向改为**胶片模型**：左滑露出右边那页、右滑露出左边那页；
     页序 任务(0) 首页(1) 课表(2)，到两端不动
  - 详情页/通知页加**滑动返回**（详情页没有底部导航，原来进去出不来）
- 验证：串口日志显示 24 次滑动全部正确识别（left/right 交替），
  实际来回切页正常

## 阶段 11：视觉收尾与联网（9/19）

- **中心时间缩字号**：dump 首页控件坐标确认「02:42」总宽 313px，
  压住了小时数字 10(内沿124)、9(88)、2(345)、3(366)。
  生成 96/88/80/72px 四个候选各渲染对比图，用户选 **72px**；
  冒号留白 24→14px，时间总宽 313→约 206px
- **尺寸自适应**：把写死的像素值改成按屏幕直径千分比
  （原来只在 454 真机正确，模拟器 1280x800 下行宽只剩一半）
- **背景统一**：W3 行背景改透明、W5 屏幕与面板改 `CLR_BG(#0E121C)`
  （参考工程屏幕是纯黑所以它的面板也用纯黑，本工程是深蓝黑，照抄会成黑带）
- **修 use-after-free**：页面切换的 `auto_del` 会删除旧屏幕，
  而 `ui_home` 的时钟定时器仍每秒访问已释放的 label（现象：点翻页栏崩溃）。
  在 `LV_EVENT_SCREEN_UNLOADED` 时停表 + 清空静态指针
- **WiFi 联网**（详见 `board/bes2800bp/README.md`）：
  - `wapi` 的 flag 必须是字符串（`WAPI_ESSID_ON`），写数字会被驱动拒绝
    `bes_wl_set_ssid ret=-22 (EINVAL)`
  - 密码先设(`psk`)、再由 `essid` 触发连接
  - 开机自动连要等 WiFi 驱动就绪：rcS 在 ~1.75s 执行，驱动 ~1.98s 才 bind
  - 命令顺序不能省：`ifup → mode MANAGED → psk → essid DELAY_ON → essid ON`
  - 实测开机后自动拿到 IP 并 ping 通网页控制台主机
- **网站并入**：队友的手机网页控制台原本只在 fork 的
  `dev-ai-contest-2026` 分支（不在 PR 分支上），并入主分支并整理

## 阶段 12：AI Agent 上真机与 API 打通（9/19 夜）

**目标**：把 ai_agent 编进真机，接通小米 MiMo，实现"说一句话就建任务"。

- 做了什么：
  - 板级 defconfig 启用 `CONFIG_EXAMPLES_AI_AGENT_VELA=y` 并重编
    （镜像 6.38 → 6.49MB，只多 0.11MB，分区余量仍有 3MB）
  - 烧录后 `ps` 确认 velatime 仍在跑、`free` 显示空闲 53MB、WiFi 自动连上
  - 启动 `ai_agent`，所有子系统 `rc=0`；用 `set_llm` 配置 Token Plan 端点
  - `net_test` 通过：TLS 握手成功（TLSv1.2 / ECDHE-RSA-AES128-GCM-SHA256），HTTP 200
  - `ask 只回复OK` 成功（7.5 秒拿到回复）
- 遇到的问题与解决（三处时序缺陷，逐个定位）：
  1. **带工具的请求永久卡死**（>3 分钟无输出）
     - 读 `vela_tls.c` 找到三个叠加因素：
       a. WiFi 省电挂起后 keep-alive 连接实际已失效，但复用前的排空探测检测不到
       b. `AGENT_LLM_SOCKET_TIMEOUT_SEC = 120`，一次读要等 120 秒
       c. `tls_read_response` 的循环里 `ret == MBEDTLS_ERR_SSL_WANT_READ`
          时既不 break 也不计数 → 无限重试
     - 修法：`pool_acquire` 不再复用旧连接（每次重新握手，多约 1 秒）；
       `WANT_READ` 加 30 次上限
  2. **watchdog 误判超时**（`call took 2748555189 ms`）
     - 时钟只在首次 TLS 握手时才从 1970 拨到 2026，而 watchdog 在请求开始取
       `t0=1970`、结束取 `t1=2026`，差值 56 年被 `uint32` 截断后仍是天文数字；
       `calc_elapsed_ms` 只防了时钟倒退，没防前跳
     - 修法：①启动早期（P0 阶段）就拨正时钟；②`calc_elapsed_ms` 增加前跳保护
       （单次差 >600 秒按时钟跳变处理）；③超时 60→150 秒、socket 120→180 秒
     - 结果：工具调用链跑通（read_file 4.5 秒 → get_current_time → 第三次请求）
  3. **整条链路累计超时导致 AP 看门狗复位**
     - 单次 LLM 调用实测 86 秒，多轮叠加后触发 `Crash happened from bes ap wdt`
     - 结论：**API 链路本身完全正常**（TLS / 鉴权 / 模型 / 工具调用全部成功），
       瓶颈是板子看门狗等不了多轮对话；建议换更快的模型
- 验证：`net_test` HTTP 200；`ask` 简单问答 7.5 秒成功；工具调用链跑通

## 阶段 13：真机时间问题（9/19 深夜 ~ 9/20）

**现象**：表盘时间和现实对不上（先是停在 1970，校时后又慢 8 小时）。

- 三个独立问题：
  1. **系统时钟停在 1970**
     - 板子没有带电池的 RTC，上电即 1970；
       NSH 的 `date` 是只读命令（`date -s` 报 too many arguments）
     - 修法：新增 `time_sync` 工具，用**明文 HTTP** 读网站响应头的
       `Date:` 字段校时。
       **为什么必须是 HTTP 而不是 HTTPS**：HTTPS 要过证书校验，而证书校验
       依赖正确的时钟 —— 板子上电是 1970，会形成死循环；明文 HTTP 不需要
       TLS，因此不依赖系统时钟，任何状态下都能取到真实时间。
     - 顺带踩坑：NuttX 的 libc `scanf` 不支持 `%[^,]` 字符类转换，
       最初报 `bad Date header: Sat, 19 Sep 2026 15:05:04 GMT`
       —— 网络已经取到正确时间了，是解析失败。改为先用 `strchr` 跳过
       `"Sat, "` 前缀，再用普通 `%d %7s %d %d:%d:%d` 解析。
  2. **表盘慢 8 小时（时区）**
     - `ui_home.c` 用 `localtime_r()`，它依赖 `TZ` 环境变量；
       NuttX 遇到 POSIX TZ 串（`"CST-8"`）会去找 zoneinfo 文件，
       找不到就**静默退回 UTC** —— 这正是慢 8 小时的原因
     - 修法：新增 `include/velatime_time.h`，手工做 UTC+8 换算，不依赖 TZ
  3. **影响面比表盘更大**
     - 项目里共有 **9 处** 时间换算受影响：
       `ui_home.c`（表盘）、`ui_tasks.c`（今天/明天/已超期）、
       `velatime_main.c`（提醒的星期）、`core_recommend.c`（紧急度与日历天差）
     - 全部统一到 `velatime_localtime()` / `velatime_mktime()`
       （`mktime` 同样依赖 TZ，`timegm` 才不依赖）
- 过程中的一次返工（如实记录）：
  - 第一版改时区的脚本先删掉了 `+8`、第二个脚本又删了常量定义却没换成函数
    调用，结果变成纯 UTC；用户反馈"小时还是不对"后才发现并修正。
    修正后加了全项目扫描自检，确认无漏网的 `localtime_r` / `mktime`。
- 验证（真机）：
  ```
  $ date
  Sat, Sep 19 15:40:17 2026            <- 系统时钟（UTC）

  $ time_sync
  time_sync: www.baidu.com -> 1789832424
  time_sync: clock set to 2026-09-19 23:40:24 CST (UTC+8)
  ```
  同时刻主机北京时间 23:40:24（**分秒不差**），表盘显示 23:40，
  用户确认"没有问题了"。**开机自动校时生效，无需任何手动操作。**
