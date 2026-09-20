# VelaTime - 主动式 AI 学生时间管家

> 2026 首届 openvela AI 硬件开发者大赛 · AI 硬件产品创新赛道
> 参赛编号 **481** · 队伍 **naiwajundui**

## 一、项目定位

VelaTime 不是待办清单，也不是问答机器人，而是一个**会主动找你**的时间管家：

- 它知道你什么时候有课、什么时候空着；
- 它知道你有哪些作业、各自什么时候截止；
- 在合适的时间点，它**主动**站出来说"现在这 120 分钟，先写高数作业"。

一句话：**Agent 负责理解与规划，openvela 设备负责触达与执行。**

面向高校学生，解决三个真实痛点：任务容易忘、安排容易乱、碎片时间利用率低。

## 二、硬件与系统

| 项 | 内容 |
|---|---|
| 目标开发板 | 恒玄科技 **BES 2800BP**（已适配 openvela，面向智能手表/手环） |
| 系统 | openvela（NuttX 内核） + LVGL 9.1 + `ai_agent` 框架 |
| 大模型 | 小米 MiMo（Token Plan 专用端点） |
| 开发/验证环境 | VMware Ubuntu 22.04 + goldfish-arm64-v8a-ap 模拟器 |

## 三、系统架构

```
                    ┌──────────────────────────────┐
   自然语言输入 ───▶ │   ai_agent（Agent 运行时）    │
                    │  · Skill: Student Task Planner│
                    │  · 工具: get_current_time /   │
                    │    read_file / write_file /   │
                    │    cron_* / heartbeat         │
                    └──────────────┬───────────────┘
                                   │ 写入
                          /data/ai_agent/TASKS.md
                                   │ 读取（每秒轮询 + 哈希稳定检测）
                    ┌──────────────▼───────────────┐
                    │      VelaTime 应用（LVGL）    │
                    │  · core_task   任务存储        │
                    │  · core_schedule 课程/空闲窗口 │
                    │  · core_recommend 评分推荐     │
                    │  · core_agent_sync 文件桥/提醒 │
                    └──────────────┬───────────────┘
                                   │ 显示
                            手表屏幕（454 圆屏表盘）

   主动方向（VelaTime → Agent）：
   VelaTime 写 /data/ai_agent/HEARTBEAT.md
        → agent heartbeat 定时器读到有待办
        → Agent 主动读 TASKS.md、选出最该做的
        → 写 /data/ai_agent/REMINDER.txt
        → VelaTime 轮询到新内容，直接显示提醒
```

**设计要点**：Agent 与应用之间只通过 `/data/ai_agent/` 下的文件通信，
不跨进程调用对方内部 API，因此两侧可以独立重启、独立演进。

## 四、功能清单

已实现的功能全部列在下面（每一项都可在代码中对应到具体文件）。

### 4.1 界面（LVGL 9 手写，五个页面）

| 页面 | 内容 | 实现位置 |
|---|---|---|
| **W1 表盘** | 中心时间（72px）+ 日期 + 星期 + 信封通知入口 + 未完成任务的红色角标；**深空星空背景** | `ui/ui_home.c` |
| **W2 课表** | 今日课程 + 空闲时段（起止时间与分钟数）；无课时显示"今天没有课，整天空闲" | `ui/ui_schedule.c` |
| **W3 任务列表** | 纯文字行 + 状态圆点（未开始/进行中/已完成/已延后）+ 截止时间（今天/明天/已超期/无截止） | `ui/ui_tasks.c` |
| **W4 任务详情** | 任务标题 + 状态与截止 + 三个药丸按钮：**完成任务 / 延后处理 / 删除任务**；删除带二次确认 | `ui/ui_tasks.c` |
| **W5 通知中心** | 药丸形面板，只显示"现在该做什么"，**不放置任何操作按钮**；下滑关闭 | `ui/ui_popup.c` |

### 4.2 交互

| 功能 | 说明 | 实现位置 |
|---|---|---|
| **滑动切页** | 左右滑动在任务/首页/课表之间切换（胶片模型：左滑露出右边那页） | `ui/ui_home.c` 的 `on_indev_swipe` |
| 进入任务详情 | 点击任务行 | `on_row_click` |
| 完成 / 延后 / 删除 | 详情页三个药丸按钮，删除有二次确认 | `on_action_complete/postpone/delete` |
| 通知中心 | 表盘上的信封入口进入；点击或滑动关闭 | `on_envelope_click` / `on_notify_dismiss` |
| 页面缓存 | 每个页面只构建一次，切回直接复用；数据变化时才重建 | `ui/ui_theme.c` 屏幕缓存接口 |

### 4.3 核心逻辑（core 层）

| 功能 | 说明 | 实现位置 |
|---|---|---|
| 任务存储与状态机 | 增删改查、状态切换（未开始/进行中/已完成/已延后）、按 id 查找 | `core/core_task.c` |
| 课程表与空闲窗口 | 按星期取当天课程，做区间合并算出可用时间窗口 | `core/core_schedule.c` |
| **评分推荐** | 紧迫度分段（已逾期 100 / 今天 90 / 明天 70 / 本周内 50 / 本周 30 / 更远 10）+ 优先级 + 时长匹配，取最高分 | `core/core_recommend.c` |
| **可解释推荐理由** | 理由文本由命中的评分依据直接拼装，如 `明天 08:30 截止 · 120 分钟空档` | `core/core_recommend.c` |
| **主动提醒** | 设备侧定时触发（非用户提问）：写 `HEARTBEAT.md` → Agent heartbeat 读任务 → 写 `REMINDER.txt` → 应用轮询显示；**端侧也能直接生成，不依赖网络与模型** | `core/core_agent_sync.c` |
| 运行时自动同步 | 每秒轮询 `TASKS.md` 并比较内容，变化时界面 1~2 秒内刷新 | `core/core_agent_sync.c` |
| 自定义 Skill 安装 | 启动时把 Skill 写入设备 `/data/ai_agent/skills/` | `core/core_agent_sync.c` |
| 统一时间换算 | UTC 与北京时间互转，不依赖 TZ | `include/velatime_time.h` |

### 4.4 AI 能力

| 功能 | 说明 | 实现位置 |
|---|---|---|
| **自然语言建任务** | "明天下午五点交高数作业，大概半小时" → Agent 理解后按固定格式写入 `TASKS.md` | `ai_agent` + 自定义 Skill |
| 任务标记完成（对话方式） | `- [ ]` → `- [x]` | `ai_agent` + 自定义 Skill |
| 云端大模型接入 | 真机上真实调用小米 MiMo（TLS 1.2 + Bearer 鉴权 + 工具调用） | `CONFIG_EXAMPLES_AI_AGENT_VELA=y` |
| 自定义 Skill | 学生任务管家（运行时）+ 真机迭代闭环（开发流程），见根目录 `SKILL.md` | `skills/` + `core/core_agent_sync.c` |

### 4.5 板级与显示

| 功能 | 说明 | 实现位置 |
|---|---|---|
| 中文显示 | 11 个分字号字库（时间/任务名/正文/导航等按用途分别生成），覆盖 GB2312 一级汉字 | `ui/velatime_font_*.c` |
| 开机自启 | 启动脚本用 `velatime &` 替换官方 `lvgldemo widgets &` | `board/bes2800bp/rcS.ap` |
| **开机自动联网** | 上电后自动连 WiFi，无需任何手动操作 | `board/bes2800bp/rcS.ap` |
| **开机自动校时** | 板载无 RTC 电池，新增 `time_sync` 工具从网络 HTTP `Date` 头取真实时间并设置系统时钟 | `tools/time_sync_main.c` |
| 圆屏自适应 | 所有尺寸按屏幕直径的千分比计算，454 真机与 1280×800 模拟器共用一套界面 | 各 `ui/*.c` |

### 4.6 手机网页控制台（界面模板已完成，两端尚未打通）

| 项 | 状态 |
|---|---|
| 网页本身 | ✅ **已完成**：Flask + SocketIO + SQLite，27 个页面路由 + 11 个 API 路由，手机浏览器直接打开，不需要装 App |
| 已有界面 | 课表与空闲时间、课表 CSV 批量导入、待办管理、消息推送、快捷短语、主题、每日一句、倒计时、壁纸管理、数据导出与备份 |
| **与开发板的连接** | ❌ **尚未打通**。板子应用侧还没有 HTTP 客户端，两端目前各自独立运行 |
| 定位 | **后续开发方向** —— 接口已经定义好、板子的 TLS/HTTP/JSON/DHCP/DNS 能力也已在真机验证，后续会继续完善升级，把两端真正连起来 |

## 五、目录结构（比赛仓）

```
app/velatime/
├── CMakeLists.txt              # nuttx_add_application，SRCS 需手工登记每个 .c
├── Kconfig                     # EXAMPLES_VELATIME / PRIORITY / STACKSIZE=40960
├── Make.defs / Makefile
├── velatime_main.c             # 入口：初始化 + 任务导入 + 同步定时器 + UI
├── include/velatime_types.h    # 任务/课程/推荐的数据结构（两端契约）
├── core/
│   ├── core_task.[ch]          # 任务内存存储（含 replace_all 安全批量替换）
│   ├── core_schedule.[ch]      # 课程表 + 空闲窗口
│   ├── core_recommend.[ch]     # 评分推荐 + 可解释理由
│   └── core_agent_sync.[ch]    # skill 安装 / TASKS.md 同步 / 主动提醒
├── skills/student-task-planner.md   # 自定义 Skill 源文件
└── ui/
    ├── velatime_ui.h           # UI 接口 + VELATIME_FONT_CN 宏
    ├── ui_home.c               # W1 表盘：时间/日期/星期 + 信封入口 + 滑动判定
    ├── ui_schedule.c / ui_tasks.c / ui_popup.c
    ├── ui_mock.[ch]            # 无 Agent 任务时的演示数据
    ├── velatime_bg_moon.c      # 表盘背景图（深空星空；文件名 moon 是早期版本的遗留）
    └── velatime_font_*.c       # 11 个分字号中文字库（lv_font_conv 生成）
                                #   time72 / name36 / name22 / name20 / body28
                                #   cn16 / meta17 / meta15 / nav16 / ui16 / hour56
                                #   （共 11 个，与 CMakeLists.txt 的 SRCS 一致）

website/                        # 手机网页控制台（见第九节）
├── app.py                      # Flask + SocketIO + SQLite，含内嵌前端
├── requirements.txt
└── README.md

board/bes2800bp/                # 真机（BES2800BP）板级适配
├── rcS.ap                      # 开机自启 VelaTime + 自动连 WiFi
├── defconfig.ap                # 启用 VELATIME / WEBCLIENT / CJSON
└── README.md                   # 编译、烧录、联网完整复现说明

logs/                           # AI Coding 日志（大赛必交）
docs/REAL_DEVICE_EVIDENCE.md    # 真机验证证据（命令与实拍输出）
```

> ⚠️ 注意事项：NuttX CMake 的 `INCDIR` 在本工程不生效，**所有头文件必须用相对路径 include**；
> 新增 `.c` 文件必须手工加入 `CMakeLists.txt` 的 `SRCS`。

## 五点五、真机验证证据

真机（BES2800BP）的完整验证记录单独成文：**[`docs/REAL_DEVICE_EVIDENCE.md`](docs/REAL_DEVICE_EVIDENCE.md)**

内容包括：镜像指纹、烧录输出、开机自启、内存占用、滑动切页的串口日志、
开机自动联网、开机自动校时、表盘时区、AI Agent 上板、云端大模型接通，
以及**如实列出的已知边界**（哪些没做）。

每条证据都按「命令 → 实际输出」成对给出，可直接对照复现。

## 六、构建与运行

```bash
# 1) 编译（在 openvela 工作区根目录）
cd /home/yy/openvela-contest
./build.sh vendor/openvela/boards/vela/configs/goldfish-arm64-v8a-ap --cmake -j2

# 2) 启动模拟器（务必带 -data，否则每次编译都会清空任务数据）
./emulator.sh cmake_out/vela_goldfish-arm64-v8a-ap/ \
    -data /home/yy/openvela-persistent/vela_data.bin

# 3) 在模拟器串口 NSH 中
velatime            # 启动应用（命令名区分大小写，全小写）
ai_agent            # 启动 Agent（另一个终端/会话）
```

Agent 内的一次性配置：

```
set_proxy 10.0.2.2 7897                     # 虚拟机内 Clash 的 mixed 端口
set_llm https://token-plan-cn.xiaomimimo.com/v1 mimo-v2.5 tp-你的Key
```

## 七、自定义 Skill 与主动提醒

`skills/student-task-planner.md` 与内嵌在 `core_agent_sync.c` 的规则会在
VelaTime 启动时安装到 `/data/ai_agent/skills/task-manager.md`，约束 Agent：

1. 任务文件是 `/data/ai_agent/TASKS.md`，每行格式 `- [ ] [YYYY-MM-DD] 任务标题`；
2. 日期是**截止日期**（不是创建日期）；完成用 `- [x]`；
3. 建任务时必须同一轮并行调用 `get_current_time` 与 `read_file`（兼容未打补丁的 Agent）；
4. **主动推荐流程**：读 `TASKS.md` → 选最紧急的一条 → 写一行到
   `/data/ai_agent/REMINDER.txt`，格式 `标题 | 剩余时间 | 现在怎么做`。

VelaTime 启动时把待办情况写进 `/data/ai_agent/HEARTBEAT.md`，
Agent 的 heartbeat 线程（默认 30 分钟，演示构建为 1 分钟）检测到有待办就会
**主动发起一次思考**，无需用户输入。这就是本项目的"主动 + 执行"场景。

为方便评审复现，模拟器构建把 heartbeat 间隔改成了 60 秒：

```
apps/packages/ai_agent/CMakeLists.txt:
  add_compile_definitions(AGENT_HEARTBEAT_INTERVAL_MS=60000)
```

量产/默认值为 `30 * 60 * 1000`（在 `agent_config.h`，已加 `#ifndef` 保护）。

## 八、避坑记录（开发中真实踩过）

1. **数据丢失**：构建脚本的 `gen_images` 每次都会 `mkfs.fat` 重建 `cmake_out/.../vela_data.bin`，
   所以必须用 `-data` 指向独立数据盘，否则每次编译任务/密钥全没。
2. **`.config` 手改不生效**：改完要重新跑 `cmake -S nuttx -B cmake_out/...` 才会重写 `nuttx/config.h`。
3. **Agent 单轮工具捷径**：`agent_loop.c` 原逻辑下，单轮只调一次 `read_file` 就结束，
   导致"先读后写"流程走不完；已修正为 `read_file`/`edit_file` 必须回到模型。
   （该文件属公共源码，提交比赛仓时不含此补丁，故 Skill 里要求同轮并行调用以自保。）
4. **推荐分数曾经全部相同**：Agent 写的是 `[YYYY-MM-DD]`，而早期解析要求带时分，
   解析失败后所有任务都拿兜底分 → 推荐顺序失真。现已支持两种格式。
5. **中文方块**：内置 SimSun 字库不含"业"等常用字，且 `font_multilang_small` 只有百余汉字，
   最终自建 GB2312 一级汉字字库解决。
6. **include 路径**：见第五节注意事项，跨目录一律相对路径。
7. **模拟器 `adb shell` 不可用**（`error: closed`），调试请用串口 NSH。

## 九、手机网页控制台

网页控制台在 `website/`，定位是 VelaTime 的**手机/电脑端界面模板**：
手表负责"在你眼前提醒"，网页负责"让你方便地录入与查看"。

> ⚠️ **当前的完成度**：网页本身已经可以完整运行（27 个页面路由 + 11 个 API 路由），
> 但**与开发板尚未打通** —— 应用侧还没有 HTTP 客户端。
> 两端连起来是我们明确的后续开发方向，详见第十节「功能完成情况与后续规划」。

详细说明见 [`website/README.md`](website/README.md)，这里只列要点。

### 跑起来

```bash
cd website
pip install -r requirements.txt
python app.py          # 监听 0.0.0.0:5000
```

手机在**同一个 WiFi** 下打开 `http://<主机IP>:5000` 即可。

> Windows 上手机连不上通常是防火墙挡了入站，需要放行 5000：
> `netsh advfirewall firewall add rule name="VelaTime Web" dir=in action=allow protocol=TCP localport=5000`

### 设备（手表）接口

> ⚠️ **网页侧的这些接口已经实现并可用（`curl` 可直接验证），
> 但 VelaTime 应用侧【还没有接入 HTTP 客户端】，因此目前没有任何设备在调用它们。**
> 也就是说：**网页与开发板目前各自独立运行，尚未打通**。
> 这是明确的后续开发方向，详见第十节。

接口按下面这套约定设计，两侧只交换 JSON：

| 接口 | 方法 | 用途 |
|---|---|---|
| `/api/ping` | POST | 心跳，网页据此显示"在线设备" |
| `/api/upload` | POST | 上报心率 / 步数（**应用侧未接入；且板上无传感器**） |
| `/api/schedule` | GET | 拉课表（今日 + 整周 + 空档） |
| `/api/todos` | GET | 拉待办 |
| `/api/messages` | GET | 拉推送给手表的消息 |
| `/api/daily` | GET | 每日一句 / 倒计时 / 目标 / 主题 / 壁纸 |

### 板子侧的网络能力（已在真机验证）

BES2800BP 的 AP 镜像里 TLS / HTTP / JSON / DHCP / DNS 齐备：

```
CONFIG_CRYPTO_MBEDTLS=1        TLS（HTTPS）
CONFIG_NETUTILS_WEBCLIENT=1    HTTP 客户端
CONFIG_NETUTILS_CJSON=1        JSON
CONFIG_NETUTILS_DHCPC=1        DHCP
CONFIG_LIBC_NETDB=1            DNS
```

**开机自动连 WiFi** 已做进板级启动脚本（见
[`board/bes2800bp/README.md`](board/bes2800bp/README.md)）：
上电后自动加入局域网并拿到 IP，无需任何手动操作。

### 安全提醒

网页**没有鉴权**，所有接口开放 —— 适合局域网演示，**不要暴露到公网**。
另外 `app.py` 的启动参数必须保持 `debug=False`：绑在 `0.0.0.0` 且开
debug 时，Werkzeug 调试器允许远程执行任意代码（仓库已修复）。

## 十、功能完成情况与后续规划

> 大赛明确：**没做完的功能如实说明不扣分；冒充跑通才扣分。**
> 这一节以【报名时提交的方案】为基准，说明哪些已经完成、
> 哪些受时间限制尚未完善，以及后续的升级方向。

### 10.1 报名方案声明的核心功能 —— 逐项对照

| # | 报名方案里的功能 | 状态 | 说明 |
|---|---|---|---|
| 1 | **课程与任务管理** | ✅ **已完成** | 课程表、空闲窗口计算、任务增删改与状态机（未开始/进行中/已完成/已延后），真机验证通过 |
| 2 | **AI 任务解析与拆解** | 🟡 **模拟器已完成，真机待优化** | 模拟器上完整跑通（自然语言 → 写入任务文件 → 界面刷新）；真机上 API 链路已验证（TLS / 鉴权 / 模型 / 工具调用全部成功），但多轮对话累计耗时超过板子看门狗容忍时间，需要继续优化 |
| 3 | **碎片时间任务推荐** | ✅ **已完成** | 紧迫度分段 + 优先级 + 时长匹配的评分推荐，理由可解释；端侧路径不依赖网络 |
| 4 | **主动提醒与延后重规划** | 🟡 **主动提醒已完成，音频待补充** | 主动提醒已完成（设备定时触发，端侧可直接生成，不依赖网络）；**音频提醒尚未实现**，是后续要补充的部分 |
| 5 | **离线运行与联网同步** | 🟡 **离线运行已完成，联网同步待完成** | 离线运行已完成；应用侧尚未接入 HTTP 客户端，与网页控制台的联网同步是后续工作 |

### 10.2 报名方案之外已完成的扩展

| 扩展 | 状态 | 说明 |
|---|---|---|
| **手机网页控制台** | ✅ **界面模板已完成** | Flask + SocketIO + SQLite；27 个页面路由 + 11 个 API 路由；手机浏览器直接打开，不需要装 App。课表与空档时间、课表 CSV 批量导入、待办管理、消息推送、快捷短语、主题、每日一句、倒计时、壁纸、数据导出与备份均已实现 |

### 10.3 后续开发方向

以下几项已明确规划，但由于时间限制尚未完善，**我们会在赛后继续丰富这个作品**：

| # | 方向 | 目前进展 | 后续计划 |
|---|---|---|---|
| 1 | **开发板与网页控制台打通** | 网页界面模板已完成；接口已定义；板子的 TLS / HTTP / JSON / DHCP / DNS 能力已在真机验证 | 在应用侧接入 HTTP 客户端，实现课表、待办、消息的双向同步，让两端真正联动 |
| 2 | **真机上的多轮 AI 对话** | API 链路已验证（TLS / 鉴权 / 模型 / 工具调用全部成功）；模拟器上完整跑通 | 换用更快的模型（`mimo-v2-flash`）+ 减少请求轮数 + 拆分任务粒度，把「说一句话就建任务」做到稳定可演示 |
| 3 | **音频提醒** | 报名方案中已列出（第 4 项含「音频」） | 接入板载音频输出，在主动提醒时给出语音提示 |
| 4 | **网页健康数据页** | 界面已做出（心率曲线、心率热力图、步数日历），数据由 `generate_mock()` 生成 | 网页上这几个健康数据界面是我们准备拓展的功能，但受时间限制未能完善 —— BES2800BP 上未连接生理传感器，我们也没有编写其驱动。**后续计划接入传感器或对接已有穿戴设备**，把这条链路补完 |
| 5 | **语音输入** | 报名方案中即写明「作为后续增强功能」 | 保持原规划，后续实现 |

### 10.4 关于数据的说明

| 界面上看到的内容 | 数据来源 | 说明 |
|---|---|---|
| 表盘时间 / 日期 / 星期 | 开机自动从网络 HTTP `Date` 头校准 | ✅ 真实 |
| 任务列表 | Agent 写入 `/data/ai_agent/TASKS.md`；无 Agent 数据时回落到 `ui_mock.c` 的演示任务 | 演示阶段使用演示数据 |
| 课程表 | `core_schedule` 内存表；演示时由 `ui_mock.c` 提供 8 门课 | 演示阶段使用演示数据 |
| 大模型对话（`ask`） | 真机上真实调用小米 MiMo | ✅ 真实 |
| 任务文件读写 / 界面自动刷新 | 真实读写 `/data/ai_agent/TASKS.md`，每秒轮询 | ✅ 真实 |
| **网页上的心率 / 步数** | `website/app.py` 的 `generate_mock()` 生成 | **模拟数据**，对应 10.3 第 4 项 |

**特别说明**：BES2800BP 开发板上没有连接任何生理传感器，我们也没有编写其驱动。
网页上的健康数据界面是**准备拓展的功能**，受时间限制尚未完善，
页面数值为模拟数据。我们如实说明这一点，不把它当作已完成的功能。

### 10.5 想验证什么，去哪里看

| 想验证什么 | 怎么验证 |
|---|---|
| 真机确实跑起来了 | 见 [`docs/REAL_DEVICE_EVIDENCE.md`](docs/REAL_DEVICE_EVIDENCE.md)（命令与实测输出成对） |
| 主动提醒是真的 | 串口日志 `VelaTime: proactive reminder: ...`；端侧路径不依赖网络 |
| 大模型是真调用 | 串口日志里能看到 TLS 握手 + `[llm] Response: ... bytes` + 工具调用 |
| 网页与板子是否已打通 | 在 `app/velatime/` 下搜索 `webclient` / `http` / `/api/` —— 目前没有任何调用 |

## 十一、赛道要求对照

| 官方要求 | 本项目的满足方式 |
|---|---|
| 编译 openvela + ai_agent 并在设备上运行 | ✅ **模拟器**：openvela + `ai_agent` 完整跑通，可用自然语言建任务<br>✅ **真机 BES2800BP**：openvela + VelaTime 已烧录运行（开机自启、自动联网、自动校时、454 圆屏）<br>✅ **真机已编入 `ai_agent`**（`CONFIG_EXAMPLES_AI_AGENT_VELA=y`），实测简单问答 7.5 秒返回，TLS / 鉴权 / 工具调用全部验证通过 |
| 至少 1 个自定义 Skill | ✅ `task-manager.md`（Student Task Planner，含主动推荐流程） |
| 至少 1 个"主动 + 执行"场景 | ✅ heartbeat 定时触发 → Agent 选任务 → 写入提醒 → 应用显示<br>✅ 另有**端侧即时提醒**：不依赖网络与模型，离线也能演示 |
| 完整应用场景说明 | 见本文第一、三、四节，第九节（手机网页控制台），以及第十节（功能完成情况与后续规划） |
| AI Coding 日志 | `logs/`（3 个会话，见该目录 README） |
| 通过 PR 提交到专属仓 | ✅ fork `fei-hua/contest2026_481_naiwajundui` → [PR #1](https://github.com/open-vela/contest2026_481_naiwajundui/pull/1)（30 提交）+ [PR #2](https://github.com/open-vela/contest2026_481_naiwajundui/pull/2)（3 提交），**均已合并到 `dev-ai-contest-2026`**，CLA 已签 |

### 关于真机上的 ai_agent

真机的 AP 镜像里**已经编入 `ai_agent`**（`CONFIG_EXAMPLES_AI_AGENT_VELA=y`），
并已在真机上完成验证：

```
✅ 启动无报错：全部子系统 rc=0（cfgstore / memory / session / proxy / llm_router / tools）
✅ 网络连通：net_test → Handshake OK (TLSv1.2) → HTTP Status: 200
✅ 模型调用：set_llm 配置小米 MiMo Token Plan → ask 只回复OK → 7.5 秒返回
✅ 工具调用：get_current_time / read_file 均已实际执行成功
✅ 自定义 Skill 已写入设备 /data/ai_agent/skills/task-manager.md
```

**已知的不足**：真机上多轮工具调用（如"说一句话就建任务"）会因累计耗时
超过板子看门狗容忍时间而中断，需要继续优化（见第十节 10.3 第 2 项）。
这属于**时序优化问题**，不是链路不通 —— TLS、鉴权、模型、工具调用都已分别验证成功。

真机验证的完整记录见 [`docs/REAL_DEVICE_EVIDENCE.md`](docs/REAL_DEVICE_EVIDENCE.md)。
