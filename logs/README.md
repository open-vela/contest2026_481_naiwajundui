# AI Coding 日志说明（VelaTime）

本目录用于满足大赛"提交 AI Coding 开发日志"的要求，记录 VelaTime 的开发过程。

## 目录内容

| 文件 | 内容 |
|---|---|
| `dev_timeline.md` | 开发时间线：每个阶段做了什么、遇到什么问题、怎么解决、如何验证 |
| `session_opencode_summary.md` | 与 AI 结对开发（opencode 会话）的过程摘要，含关键报错与结论 |
| `prompts.md` | 使用过的关键提示词（Prompt）与用法说明 |
| `verification.md` | 验证记录：单元测试输出、模拟器日志、真机验收结论 |
| `fei-hua/<日期>/*.jsonl` | AI 会话日志（每个会话一份，按日期归档） |
| `manifest.json` | 会话清单：会话 id、工具、起止时间、事件数、文件路径 |

## 使用方式说明

- **AI 工具**：opencode（多轮结对开发）+ DeepSeek Harness（后期远程接入虚拟机直接改代码/编译/验证）
- **模型**：会话中先后使用过 MiMo Token Plan（设备端 `ai_agent`）与云端编码模型
- **协作模式**：AI 负责方案设计、代码编写、编译排错、自动化验证脚本；
  人负责环境搭建、执行命令、界面确认与决策
- **可复现性**：所有自动化验证脚本都保留在仓库或日志中，命令与期望输出成对出现

## 时间线速览

1. 选题与报名（VelaTime，AI 硬件产品创新赛道，BES 2800BP）
2. 环境搭建（VMware Ubuntu 22.04 + openvela 全量同步 + 交叉编译）
3. AI Agent 跑通（MiMo Token Plan + 代理，`ai_agent` 可对话）
4. VelaTime LVGL 应用骨架 → 核心逻辑（任务/课程/推荐）
5. 与 Agent 打通（Skill 定制 + TASKS.md 文件桥 + 运行时自动同步）
6. 中文字库（自建 GB2312 一级汉字字库，解决方块字）
7. 主动提醒（heartbeat 触发 + 端侧即时提醒）
8. 交付（单元测试、模拟器验收、README、PR）
9. **真机打通**（BES2800BP：编 AP 镜像 → dldtool 烧录 → 启动脚本改造）
10. **滑动切页**（事件冒泡不可靠 → 改输入设备层判定 + 胶片模型）
11. **视觉收尾与联网**（中心时间缩字号、尺寸自适应、背景统一、
    修 use-after-free、开机自动连 WiFi、并入队友的手机网页控制台）

详细过程见 `dev_timeline.md`。
