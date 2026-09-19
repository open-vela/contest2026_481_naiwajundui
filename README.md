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
                            手表屏幕（推荐卡片）

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

| 功能 | 实现位置 | 说明 |
|---|---|---|
| 自然语言建任务 | Agent + Skill | "明天下午五点交高数作业，大概半小时" → 写入 `TASKS.md` |
| 任务标记完成 | Agent + Skill | `- [ ]` → `- [x]`，应用自动忽略已完成项 |
| 课程表与空闲窗口 | `core_schedule` | 按星期计算当天可用时间窗口 |
| 评分推荐 | `core_recommend` | 紧迫度 + 优先级 + 时长匹配，输出最高分任务 |
| 可解释推荐理由 | `core_recommend` | 如 `明天 08:30 截止 · 120 分钟空档` |
| 任务状态机 | `ui_home` + `core_task` | Start → 进行中；Delay → 延后，并立即换下一条 |
| 运行时自动同步 | `core_agent_sync` | Agent 改完 `TASKS.md`，界面 1~2 秒内自动刷新 |
| **主动提醒** | `core_agent_sync` + Agent heartbeat | 设备侧定时器触发，非用户提问 |
| 中文显示 | `ui/velatime_font_*.c` | 12 个分字号字库，按用途分别生成（时间/任务名/正文…） |
| **手机网页控制台** | `website/app.py` | 课表/待办/消息/壁纸/健康数据管理，手机浏览器直接打开 |
| **手表 ↔ 网页联动** | `website` + 板载 HTTP | 板子通过局域网拉课表、上报心率步数；网页显示设备在线 |

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
    ├── ui_home.c               # 首页：推荐卡片（flex 布局）+ 按钮 + 提醒显示
    ├── ui_schedule.c / ui_tasks.c / ui_popup.c
    ├── ui_mock.[ch]            # 无 Agent 任务时的演示数据
    ├── velatime_bg_moon.c      # 表盘月球背景（图片资源）
    └── velatime_font_*.c       # 12 个分字号中文字库（lv_font_conv 生成）
                                #   time72 / name36 / name22 / name20 / body28
                                #   cn16 / meta17 / meta15 / nav16 / ui16 / hour56

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

网页控制台在 `website/`，是 VelaTime 的**手机/电脑端**：
手表负责"在你眼前提醒"，网页负责"让你方便地录入与查看"。

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

板子侧通过局域网 HTTP 与网页通信，两侧只交换 JSON：

| 接口 | 方法 | 用途 |
|---|---|---|
| `/api/ping` | POST | 心跳，网页据此显示"在线设备" |
| `/api/upload` | POST | 上报心率 / 步数 |
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

## 十、已知限制与数据说明

> 大赛明确：**没做完的功能如实写进「已知限制」不扣分；冒充跑通才扣分。**
> 所以这一节把"哪些是真跑的、哪些是演示数据"逐项写清楚，方便评委对照代码核实。

### 10.1 数据来源（★ 请先看这个）

| 界面上看到的内容 | 数据实际从哪来 | 真实性 |
|---|---|---|
| **心率曲线 / 心率热力图 / 步数日历 / 今日步数** | `POST /generate_mock` 按"作息规律"生成的**模拟数据** | ❌ **模拟，非真实传感器** |
| 任务列表 | Agent 写入 `/data/ai_agent/TASKS.md`；无 Agent 数据时回落到 `ui_mock.c` 的 5 条演示任务 | ⚠️ 演示数据 |
| 课程表 | `core_schedule` 内存表；演示时由 `ui_mock.c` 提供 8 门课 | ⚠️ 演示数据 |
| 表盘时间 / 日期 / 星期 | 开机自动从网络 HTTP `Date` 头校准 | ✅ 真实 |
| 大模型对话（`ask`） | 真机上真实调用小米 MiMo（TLS + 鉴权 + 工具调用） | ✅ 真实 |
| 任务文件读写 / 界面自动刷新 | 真实读写 `/data/ai_agent/TASKS.md`，每秒轮询 | ✅ 真实 |

**⚠️ 特别说明：心率与步数是我们自己生成的。**

BES2800BP 开发板上**没有连接任何生理传感器**，我们也没有写它的驱动。
网页控制台里那几张健康数据图表（心率热力图、步数日历、多设备对比）
是为了**演示界面与数据通路**而生成的模拟数据 ——
`website/app.py` 的 `generate_mock()` 里有明确注释。

**我们不做"用随机数冒充传感器"这件事**，所以在这里如实说明，避免误解。

### 10.2 明确没做的功能

| 项 | 状态 |
|---|---|
| 应用从网页拉取数据（HTTP 客户端） | ❌ **未实现**。接口已定好，板子的 TLS / HTTP / JSON / DHCP / DNS 能力也已在真机验证，但应用侧还没接 |
| 网页推送消息显示到手表 | ❌ **未实现**（依赖上一项） |
| 真机上的多轮工具调用建任务 | ⚠️ **不稳定**。TLS / 鉴权 / 模型 / 工具调用全部成功过，但单次请求实测 86 秒，多轮叠加触发板子看门狗复位 |
| 真机上运行 `ai_agent` | ✅ 已编入并跑通（`CONFIG_EXAMPLES_AI_AGENT_VELA=y`），简单问答 7.5 秒返回 |
| 心率 / 步数采集 | ❌ **没有做**（板上无传感器） |

### 10.3 想验证什么，去哪里看

| 想验证什么 | 怎么验证 |
|---|---|
| 真机确实跑起来了 | 见 [`docs/REAL_DEVICE_EVIDENCE.md`](docs/REAL_DEVICE_EVIDENCE.md)（命令与实测输出成对） |
| 主动提醒是真的 | 串口日志 `VelaTime: proactive reminder: ...`；端侧路径不依赖网络 |
| 大模型是真调用 | 串口日志里能看到 TLS 握手 + `[llm] Response: ... bytes` + 工具调用 |
| **数据是模拟的** | 读 `website/app.py` 的 `generate_mock()` 和 `app/velatime/ui/ui_mock.c` |

## 十一、赛道要求对照

| 官方要求 | 本项目的满足方式 |
|---|---|
| 编译 openvela + ai_agent 并在设备上运行 | ✅ **模拟器**：openvela + `ai_agent` 完整跑通，可用自然语言建任务<br>✅ **真机 BES2800BP**：openvela + VelaTime 已烧录运行（开机自启、自动联网、454 圆屏）<br>⚠️ 真机当前未编入 `ai_agent`（见下方说明） |
| 至少 1 个自定义 Skill | ✅ `task-manager.md`（Student Task Planner，含主动推荐流程） |
| 至少 1 个"主动 + 执行"场景 | ✅ heartbeat 定时触发 → Agent 选任务 → 写入提醒 → 应用显示<br>✅ 另有**端侧即时提醒**：不依赖网络与模型，离线也能演示 |
| 完整应用场景说明 | 见本文第一、三、四节，第九节（手机网页控制台），以及第十节（已知限制与数据说明） |
| AI Coding 日志 | `logs/`（3 个会话，见该目录 README） |
| 通过 PR 提交到专属仓 | ✅ fork → [PR #1](https://github.com/open-vela/contest2026_481_naiwajundui/pull/1) → CLA 已签 → `mergeable_state: clean` |

### 关于真机上的 ai_agent

真机的 AP 镜像里**没有编入 `ai_agent`**（`EXAMPLES_AI_AGENT_VELA` 未启用），
因此真机上目前是**纯端侧模式**：本地规则推荐 + 端侧即时提醒 + 文件桥，
不依赖大模型也能完整演示。

`ai_agent` 未编入的原因与后续路径：

```
✅ 板子已有：TLS(mbedtls) / HTTP(webclient) / JSON(cjson) / DHCP / DNS
✅ 板子已有：WiFi，且已做进启动脚本开机自动联网（实测拿到 IP 并可 ping 通）
✅ 板子已有：CONFIG_WIRELESS_WAPI / DHCPC / NETDB

⬜ 还需：defconfig 加 CONFIG_EXAMPLES_AI_AGENT_VELA=y 并重编（源码 78 个 .c）
⬜ 还需：运行时 set_llm <端点> <模型> <Key> 配置小米 MiMo Token Plan
         （端点 https://token-plan-cn.xiaomimimo.com/v1，模型 mimo-v2.5）

即"联网能力已具备，只差把 Agent 编进去并配 Key"。
模拟器侧该链路已完整验证过（见 logs/dev_timeline.md 阶段 2）。
