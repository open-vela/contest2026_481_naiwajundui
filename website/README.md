# VelaTime 手机网页控制台

VelaTime 的**手机/电脑端控制台**。手表负责"在你眼前提醒"，这个网页负责
"让你方便地录入与查看"—— 课表、待办、消息、壁纸、健康数据都在这里管理。

```
┌─────────────┐        局域网 HTTP        ┌──────────────┐
│  手机浏览器  │ ◀──────────────────────▶ │  本仓库的     │
│  （控制台）  │   http://<主机>:5000      │  app.py      │
└─────────────┘                           └──────┬───────┘
                                                 │
                                     HTTP 拉取/上报
                                                 │
                                          ┌──────▼───────┐
                                          │ BES2800BP    │
                                          │ VelaTime 手表 │
                                          └──────────────┘
```

---

## 一、这是什么

单文件 Flask 应用（`app.py`，约 1200 行），内含：

| 组成 | 说明 |
|---|---|
| 后端 | Flask + Flask-SocketIO（实时推送） |
| 存储 | SQLite（`velatime.db`，首次运行自动建表） |
| 前端 | 内嵌 HTML 模板 + Chart.js（图表）+ Socket.IO（实时） |
| 依赖 | 全部通过 CDN 加载，**不需要 npm 构建** |

**七张表**：`records`（心率/步数）、`config`（个性化配置）、
`device_names`、`online`（在线状态）、`schedule`（课表）、
`messages`（消息）、`todos`（待办）。

---

## 二、怎么运行

```bash
pip install -r requirements.txt
python app.py
```

默认监听 `0.0.0.0:5000`，浏览器打开：

```
本机      http://127.0.0.1:5000
手机/手表  http://<主机局域网IP>:5000
```

> **手机访问要在同一个 WiFi 下**，并且主机的防火墙要放行 5000 端口。
> Windows 上如果手机连不上，通常是防火墙挡了入站：
> ```
> netsh advfirewall firewall add rule name="VelaTime Web" ^
>     dir=in action=allow protocol=TCP localport=5000
> ```
> （需要管理员权限）

---

## 三、手机上能做什么

**健康数据**
- 实时心率仪表盘
- 心率热力图（按小时）
- 步数日历（最近 30 天）
- 多设备对比
- 今日 / 本周统计

**日程与任务**
- 今日课表 + 空档时间
- 待办事项（增删改）
- 课表 CSV 批量导入（带模板下载）

**与手表互动**
- 消息推送（发到手表显示）
- 快捷短语
- 壁纸管理（上传 / 删除 / 设为当前）
- 主题（明亮 / 暗黑 / 节日）
- 每日目标、倒计时、每日一句

**数据工具**
- 导出 Excel / CSV / JSON
- 备份 / 恢复 / 清空
- 生成模拟数据（演示用）

---

## 四、给设备（手表）用的接口

板子侧通过 HTTP 拉取或上报，字段都是 JSON。

### 设备上报

| 接口 | 方法 | 说明 |
|---|---|---|
| `/api/ping` | POST | 设备心跳，带上 `device_id` 后网页会显示"在线设备" |
| `/api/upload` | POST | 上报心率/步数，字段 `device_id` / `heart_rate` / `steps` |

```bash
curl -X POST http://<主机>:5000/api/ping \
     -H 'Content-Type: application/json' \
     -d '{"device_id":"BES2800BP"}'
# -> {"status":"ok"}
```

### 设备拉取

| 接口 | 说明 |
|---|---|
| `/api/schedule` | 课表：今日课程 + 整周 + 空档时间 |
| `/api/todos` | 待办列表 |
| `/api/messages` | 推送给手表的消息 |
| `/api/daily` | 每日一句 / 倒计时 / 每日目标 / 主题 / 快捷短语 / 当前壁纸 / 在线设备 |

```bash
curl http://<主机>:5000/api/daily
# -> {"quote":"今天也辛苦了","daily_goal":8000,"theme":"light", ...}
```

---

## 五、开发说明

### 目录

```
website/
├── app.py              # 全部后端 + 内嵌前端模板
├── requirements.txt
├── README.md
└── velatime.db         # 运行时生成（不必提交）
```

### 启动配置

```python
socketio.run(app, host='0.0.0.0', port=5000, debug=False,
             allow_unsafe_werkzeug=True)
```

**`debug` 必须保持 `False`。** 绑在 `0.0.0.0` 且开启 debug 时，
Werkzeug 调试器允许**远程执行任意代码** —— 这是严重安全漏洞。
（本仓库已修复，见提交历史。）

---

## 六、已知限制

- **没有鉴权**：所有接口开放。适合局域网演示，**不要暴露到公网**。
- 前端图表依赖 CDN（`cdn.jsdelivr.net` / `cdn.socket.io`）。
  如果网络访问不了这两个 CDN，页面能打开但图表会空白 ——
  可以把这两个库下载到本地改成本地引用。
- 数据库是单文件 SQLite，没有并发写保护，单机演示够用。

---

## 七、作者

- 初版：[@TiAmomeovv](https://github.com/TiAmomeovv)（队友）
- 整理与安全修复：本仓库提交历史
