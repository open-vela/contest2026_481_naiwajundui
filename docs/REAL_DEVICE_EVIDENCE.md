# 真机验证证据（BES2800BP）

> 本文档是**真机跑通的原始证据**，不是结论性描述。
> 每条都按「命令 → 实际输出」成对给出，可直接对照复现。
>
> 三个容易误解的点，先说明：
> - **代码行数不是评分依据** —— 这里只贴能证明"真跑起来了"的东西
> - **没领到板子用模拟器完全没问题** —— 我们两边都有，模拟器结果见 `logs/verification.md`
> - **没做的部分如实写** —— 见文末「已知边界」

---

## 0. 硬件与镜像

| 项 | 值 |
|---|---|
| 开发板 | 恒玄 BES 2800BP（`best1700_ep` / `aos_evb`） |
| 屏幕 | 454 × 454 圆形 AMOLED（DMC RM69330） |
| 系统 | openvela（NuttX）+ LVGL 9.1 |
| 串口 | COM3 @ 921600 8N1 |
| 镜像 | `nuttx_ap.bin` 6.49 MB（分区 9.50 MB，余量约 3.0 MB） |

**镜像指纹（可复现编译产物一致性）**

| 阶段 | MD5 | 大小 |
|---|---|---|
| 仅 VelaTime | `8f5049c56c69f5efa2c9a33009f483d9` | 6,381,080 |
| + ai_agent | `dbc9ad1498fada4fcbc458a0b8d8ecd1` | 6,804,728 |
| + 时钟修复 | `20749cca68f46e8a362ce1dd91525b3b` | 6,804,920 |
| + 时区修复 | `26874fcb9160e0147d87ae06897737ed` | 6,806,280 |
| + 屏幕缓存 | `76f68c5cb9f8e46f14d426b085e7f5ce` | 6,806,856 |
| + 课程表修复（最终） | `3e570fa8ea5e127b626f4d55988819bc` | 6,806,792 |

---

## 1. 烧录

**命令**
```
dldtool.exe 3 --reboot .\programmer1700_dual.bin --set-dual-chip 1 \
  -M .\nuttx_ap.bin --pgm-rate 2000000
```

**实际输出（尾部）**
```
burn_file/--- Burn magic number: addr=0x30190000 value=0xBE57EC1C ---
sys_cmd_boot_cmd/--- Send SYS REBOOT msg ---
---------------------
PROGRAMMING SUCCEEDED
---------------------
[SET_DUAL_CHIP] index=-1 flashEn=1 secRegEn=0
```

**耗时**：约 107 秒（含等板子同步的最长 76 秒窗口，期间任意时刻按 RESET 都能进）

---

## 2. 应用开机自启

**命令**
```
ps
```

**实际输出**
```
PID GROUP PRI POLICY   TYPE    NPX STATE    EVENT     SIGMASK            STACK    USED FILLED    CPU COMMAND
11    11 100 RR       Task      - Ready              0000000000000000 0040816 0012544  30.7%  15.8% velatime
```

`velatime` 是 PID 11，由板级启动脚本 `rcS` 拉起（替换掉官方 demo）：

```diff
- lvgldemo widgets &
+ velatime &
```

---

## 3. 内存占用

**命令**
```
free
```

**实际输出**
```
total       used       free    maxused    maxfree  nused  nfree name
53556480     243840   53312640     305368   53260384     65     17 Umem
```

**可用 53.3 MB，应用只占约 0.24 MB。** 连续运行无泄漏迹象（`used` 稳定）。

---

## 4. 滑动切页

**背景**：这是本项目**最难的一个真机问题**。
模拟器上滑动正常，真机上点击能用、滑动完全没反应。

**排查过程**：

加触摸调试输出并烧录，用 120 秒串口监听抓真实数据 ——

```
输入设备层（indev）收到事件：200+ 次
屏幕层（screen）收到 TOUCH：  6 次
```

**LVGL 的事件冒泡在真机上成功率仅约 3%。**

**结论与修法**：把滑动判定从**屏幕层**挪到**输入设备层**
（`lv_indev_add_event_cb`，不经过冒泡），并用 PRESSED + RELEASED 两点坐标自己算方向。
方向映射改为**胶片模型**：左滑露出右边那页、右滑露出左边那页。

**修复后的串口日志**（120 秒窗口内 24 次滑动，左右交替）
```
VelaTime: swipe right -> schedule (d=262,28)
VelaTime: swipe left  -> tasks    (d=-225,-4)
VelaTime: swipe right -> schedule (d=240,21)
VelaTime: swipe left  -> tasks    (d=-305,43)
```

**同时可见双缓冲提交**（证明画面真的刷新了）
```
PANDBG commit cnt=1 frame=7 addr=0x3823a8c0 yoffset=0   state=3
PANDBG commit cnt=1 frame=8 addr=0x38308440 yoffset=454 state=3
```

---

## 5. 开机自动联网

**板级启动脚本里的顺序**（实测出来的，少了任何一步都不行）

```sh
sleep 8                                    # 等 WiFi 驱动就绪
ifup wlan0
wapi mode wlan0 WAPI_MODE_MANAGED
wapi psk wlan0 <SSID> <PASSWORD> 1
wapi essid wlan0 <SSID> WAPI_ESSID_DELAY_ON
wapi essid wlan0 <SSID> WAPI_ESSID_ON
renew wlan0 &
sleep 3
time_sync                                  # 见第 6 节
```

**两个坑**（都写进了时间线的避坑记录）：

1. **wapi 的 flag 必须是字符串**（`WAPI_ESSID_ON`），写数字会被驱动拒绝：
   ```
   bes_wl_set_ssid: Failed to set ssid, ret=-22
   ```
2. **`ifup` 要等驱动就绪** —— rcS 在约 1.75 s 执行时驱动还没好
   （约 1.98 s 才 `[WF-RPMSG] wifi_host_rpmsg_bind success`），
   所以先 `sleep 8`，否则报 `ioctl(SIOCSIWESSID): 25`（ENOTTY）

**实际输出（开机后无需任何手动操作）**
```
$ ifconfig wlan0
wlan0   Link encap:Ethernet HWaddr 00:80:43:5a:77:09 at RUNNING mtu 1500
        inet addr:192.168.0.157 DRaddr:192.168.0.1 Mask:255.255.255.0

$ ping -c 2 192.168.0.1
56 bytes from 192.168.0.1: icmp_seq=0 time=446.1 ms
56 bytes from 192.168.0.1: icmp_seq=1 time=263.7 ms
2 packets transmitted, 2 received, 0% packet loss

$ nslookup www.baidu.com
Host: www.baidu.com  Addr: 183.2.172.177
```

---

## 6. 开机自动校时（表盘时间准确性）

**背景**：BES2800BP **没有带电池的 RTC**，上电后系统时钟是 1970-01-01，
而 NSH 的 `date` 命令**只读**（`date -s` 报 `too many arguments`）。

**做法**：新增 `time_sync` 工具，用**明文 HTTP** 读网站响应头的 `Date:` 字段校时。

> **为什么必须是 HTTP 而不是 HTTPS**：HTTPS 要过证书校验，而证书校验依赖
> 正确的时钟 —— 板子上电是 1970，会形成死循环。明文 HTTP 不需要 TLS，
> 因此不依赖系统时钟，任何状态下都能取到真实时间。

**实际输出**
```
$ date
Sat, Sep 19 15:40:17 2026                  ← 系统时钟（UTC）

$ time_sync
time_sync: www.baidu.com -> 1789832424
time_sync: clock set to 2026-09-19 23:40:24 CST (UTC+8)
```

**同时刻主机北京时间**：`2026-09-19 23:40:24` —— **分秒不差**。

**这个流程是开机自动完成的**（启动脚本在 WiFi 连上后调用），无需手动操作。

---

## 7. 表盘时区（时间显示正确）

**问题**：校时后系统时钟对了，但表盘显示 15:40（UTC）而不是 23:40（北京时间），**差 8 小时**。

**根因**：表盘用 `localtime_r()`，它依赖 `TZ` 环境变量；
而 NuttX 遇到 POSIX TZ 串（`"CST-8"`）会去找 zoneinfo 文件，
**找不到就静默退回 UTC**。

**修法**：新增 `include/velatime_time.h`，手工做 UTC+8 换算，不依赖 TZ：

```c
static inline struct tm *velatime_localtime(time_t t, struct tm *out)
{
  time_t local_epoch = t + VELATIME_TZ_OFFSET_SEC;   /* +8h */
  return gmtime_r(&local_epoch, out);
}
```

**影响面**：项目里共 **9 处**时间换算都受影响，全部统一（表盘、任务列表的
今天/明天/已超期判断、提醒的星期、推荐引擎的紧急度与日历天差）。

**验证结果**：表盘显示与主机一致，用户确认通过。

---

## 8. AI Agent 在真机上运行

**板级 defconfig 启用**
```
CONFIG_EXAMPLES_VELATIME=y
CONFIG_EXAMPLES_AI_AGENT_VELA=y
```

**启动日志（节选，全部子系统 rc=0）**
```
[agent] AI Agent - Vela AI Agent starting (build: Sep 19 2026 22:26:46)
[agent] [boot +4ms] P0: timezone set
[agent] [boot +5ms] P0: clock synced (was 1970)
[agent] [boot +9ms] heap: arena=53556608 free=53278808 used=277800
[cfgstore] Config store ready at /data/ai_agent/config/config.json
[bus] Message bus initialized (depth 16)
[memory] Memory store initialized successfully
[session] Session manager initialized at /data/ai_agent/sessions
[proxy] No proxy (direct connection)
[llm_router] Router initialized
[tools] Registered tool: get_current_time
[mcp_reg] MCP tool registry initialized
```

**命令表**（部分）
```
vela> help
  net_status           - Show network connection status
  net_test             - Test HTTPS connection to Baidu
  set_llm <preset|host> [model] [key] - Switch LLM backend
  set_wifi <ssid> <pw> - Connect to WiFi (real hardware; saved for reboot)
  ask <text>           - Chat with AI Agent
  heartbeat_trigger    - Manually trigger heartbeat check
```

---

## 9. 云端大模型接通（TLS / 鉴权 / 模型 / 工具调用）

**网络连通测试**
```
vela> net_test
Testing HTTPS to www.baidu.com...
[vela_tls] Handshake start: Host=www.baidu.com, UNIX=1161
[vela_tls] Clock too old, forcing to 2026
[vela_tls] Handshake OK: TLSv1.2 / TLS-ECDHE-RSA-WITH-AES-128-GCM-SHA256
SUCCESS! HTTP Status: 200
```

**配置小米 MiMo（Token Plan）**
```
vela> set_llm https://token-plan-cn.xiaomimimo.com/v1 mimo-v2.5 <KEY>
[llm_router] Backend 0 configured: token-plan-cn.xiaomimimo.com
[llm] LLM config updated atomically: token-plan-cn.xiaomimimo.com/v1/chat/completions (model: mimo-v2.5)
LLM backend: token-plan-cn.xiaomimimo.com:443/v1/chat/completions (model: mimo-v2.5) [router slot 0]
```

**模型调用成功**
```
vela> ask 只回复OK
[agent] Processing message from cli:console
[skills] Skills summary: 814 bytes
[context] System prompt built: 3289 bytes
[vela_tls] Handshake OK: TLSv1.2 / TLS-ECDHE-RSA-WITH-CHACHA20-POLY1305-SHA256
[llm] Response: 39 bytes text, 0 tool calls, finish=end_turn
[agent] LLM resp: text=39, tool_use=0, calls=0
[trace] iter=0 tool=(none) latency=7473ms llm=ok backend=0
```

**工具调用链也跑通过**
```
[llm] Response: 0 bytes text, 1 tool calls, finish=tool_calls
[agent] Tool call: get_current_time args={}
[tool_time] Time (local clock): 2026-02-28 18:42:07 CST (UTC+8), UNIX epoch: 1772275425
[llm] OpenAI API with tools (model: mimo-v2.5, 17078 bytes)
```

> ⚠️ **已知边界**：带工具的链路单次请求实测 **86 秒**，多轮叠加后触发板子
> AP 看门狗复位。**API 链路本身完全正常**（TLS / 鉴权 / 模型 / 工具调用全部成功），
> 瓶颈是**看门狗的时序**，不是"没做通"。下一步是换更快的模型 + 减少请求轮数。
> 详见 `logs/dev_timeline.md` 阶段 12。

---

## 10. 开发过程中修复的时序缺陷（AI 侧）

真机暴露了三个模拟器上没有的问题，都已定位并修复：

| # | 现象 | 根因 | 修法 |
|---|---|---|---|
| 1 | 带工具的请求永久卡死（>3 分钟无输出） | WiFi 省电挂起后 keep-alive 连接失效但探测不到；读超时 120 s；`WANT_READ` 不 break 无限重试 | 不复用旧连接（每次重新握手）+ `WANT_READ` 加重试上限 |
| 2 | watchdog 误判超时（`call took 2748555189 ms`） | 时钟只在首次 TLS 握手时从 1970 拨到 2026，watchdog 跨了 56 年、uint32 截断 | 启动早期（P0）就拨正时钟 + `calc_elapsed_ms` 增加前跳保护 |
| 3 | AP 看门狗复位 | 单次 LLM 86 秒，多轮累计超时 | 超时放宽 60→150 s、socket 120→180 s；**根本解是换更快的模型** |

---

## 11. 已知边界（如实说明）

| 项 | 状态 |
|---|---|
| 五个页面 + 滑动切页 | ✅ 真机验证通过 |
| 开机自启 + 自动联网 + 自动校时 | ✅ 真机验证通过 |
| 端侧主动提醒（不依赖网络） | ✅ 实现并在模拟器验证；真机可演示 |
| 云端大模型：简单问答 | ✅ 真机验证通过（7.5 秒） |
| 云端大模型：多轮工具调用建任务 | ⚠️ **真机上不稳定**（看门狗时序），API 链路已验证 |
| 应用从网页拉取数据 | ⚠️ **未实现**。接口已定好、板子 TLS/HTTP/JSON 能力已验证，是下一步工作 |
| 网页推送消息显示到手表 | ⚠️ **未实现**（同上一行） |

**未做的部分不隐瞒** —— 评委可对照代码核实。

---

## 12. 复现方式

```bash
# 1) 编译（在 openvela 工作区根目录）
./build.sh vendor/bes/boards/best1700_ep/aos_evb/configs/ap --cmake -j4
#    产物：cmake_out/aos_evb_ap/nuttx_ap.bin

# 2) 烧录
dldtool.exe 3 --reboot programmer1700_dual.bin --set-dual-chip 1 \
  -M nuttx_ap.bin --pgm-rate 2000000

# 3) 串口验证（COM3 @ 921600）
ps              # 应看到 velatime
ifconfig wlan0  # 应看到 inet addr
date            # 应看到当前时间
time_sync       # 手动再校时一次

# 4) AI（可选，需自备 API Key）
ai_agent
set_llm https://token-plan-cn.xiaomimimo.com/v1 mimo-v2.5 <KEY>
net_test
ask 只回复OK
```

板级配置与启动脚本见 `board/bes2800bp/`：
`README.md`（完整说明）、`defconfig.ap`、`rcS.ap`（含 WiFi 与校时，已用占位符）。
