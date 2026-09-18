# BES2800BP 板级适配（VelaTime 真机）

本目录保存**让 VelaTime 在恒玄 BES 2800BP 开发板上开机自启**所需的板级改动。

因为板级文件位于另一个 git 仓库（`vendor/bes`，上游 `open-vela/vendor_bes`，
我们没有推送权限），所以在这里留一份副本，保证提交材料自包含、可复现。

---

## 一、两个改动

| 文件 | 目标位置 | 改动 |
|---|---|---|
| `rcS.ap` | `vendor/bes/boards/best1700_ep/aos_evb/src/etc/init.d/rcS.ap` | `lvgldemo widgets &` → `velatime &` |
| `defconfig.ap` | `vendor/bes/boards/best1700_ep/aos_evb/configs/ap/defconfig` | 启用 VelaTime + HTTP 库 |

### 1. rcS.ap —— 为什么必须改

```
#ifdef CONFIG_BESWATCH
beswatch &
#else
lvgldemo widgets &      ← 原样，官方 LVGL demo 会被自动拉起
#endif
```

`velatime_main.c` 开头有：

```c
if (lv_is_initialized())
  {
    return -1;          /* 一看 LVGL 已被初始化就直接退出 */
  }
```

官方的 `lvgldemo` 会先调 `lv_init()`，所以两个应用**不能共存**。
如果只把 VelaTime 编进镜像而不改启动脚本，现象是：
`help` 里能看到 `velatime` 命令，但屏幕毫无变化，`ps` 里显示的是
`lvgldemo widgets`。

改为：

```
velatime &
```

### 2. defconfig —— 启用应用与 HTTP 库

```
CONFIG_EXAMPLES_VELATIME=y
CONFIG_NETUTILS_WEBCLIENT=y      # 手机网页控制台 <-> 开发板
CONFIG_NETUTILS_CJSON=y
```

板子已有的网络能力（实测）：

```
CONFIG_CRYPTO_MBEDTLS=1           TLS（HTTPS）
CONFIG_NETUTILS_WEBCLIENT=1       HTTP 客户端
CONFIG_NETUTILS_CJSON=1           JSON
CONFIG_NETUTILS_DHCPC=1           DHCP
CONFIG_LIBC_NETDB=1               DNS
CONFIG_WIRELESS_WAPI=y            wapi 命令行工具
CONFIG_WIRELESS_WAPI_CMDTOOL=y
CONFIG_BES_WIFI_RPMSG_APC0C1=y    BES WiFi
```

---

## 二、最容易踩的坑：改了 rcS.ap 却不生效

**`add_board_rcsrcs()` 是在 CMake【配置阶段】把 `#include "rcS.ap"`
展开成最终 `rcS` 的。只跑 ninja 构建不会重新展开 —— 改了 `rcS.ap`
也白改，编出来的镜像 MD5 一点不变。**

必须强制重新配置：

```bash
touch vendor/bes/boards/best1700_ep/aos_evb/src/CMakeLists.txt
rm -f cmake_out/aos_evb_ap/etc/init.d/rcS \
      cmake_out/aos_evb_ap/romfs_etc/init.d/rcS \
      cmake_out/aos_evb_ap/romfs.img
```

**判断有没有真的重新配置**：看 ninja 的目标数。
- 只有 ~18 个 → **没重新配置，改动不会生效**
- 2000+ 个 → 正常全量重编（约 13 分钟）

烧录前务必自检：

```bash
python3 - cmake_out/aos_evb_ap/nuttx_ap.bin <<'PY'
import sys
d = open(sys.argv[1],'rb').read()
print("镜像 %.2f MB" % (len(d)/1048576))
print("'lvgldemo widgets':", "还在(错)" if d.find(b"lvgldemo widgets")>=0 else "已去除(对)")
print("'velatime &'      :", "已加入(对)" if d.find(b"velatime &")>=0 else "没有(错)")
PY
```

---

## 三、编译与烧录

```bash
# 编译（全量约 13 分钟）
./build.sh vendor/bes/boards/best1700_ep/aos_evb/configs/ap --cmake -j4
# 产物：cmake_out/aos_evb_ap/nuttx_ap.bin（约 6.1 MB）
```

烧录（Windows，只写 AP 分区，不动 bootloader）：

```
cd C:\xiaomi\shaolu\vela_2800bp
dldtool.exe 3 --reboot .\programmer1700_dual.bin --set-dual-chip 1 ^
    -M .\nuttx_ap.bin --pgm-rate 2000000
```

- `--set-dual-chip 1` 和 `--pgm-rate 2000000` **都是必须的**
- dldtool 启动后会等最多约 76 秒等板子同步，**这期间任意时刻按 RESET 都行**
- 成功标志：`PROGRAMMING SUCCEEDED`
- 失败：`FLASH_CMD_BURN_DATA 4 error=0x69`

分区容量：`CONFIG_BIN_FLASH_SIZE=0x980000` = 9.50 MB，镜像 6.1 MB，够用。

---

## 四、WiFi 联网（实测可用）

```
ifup wlan0
wapi psk   wlan0 <密码> 1
wapi essid wlan0 <SSID> WAPI_ESSID_ON
renew wlan0
ifconfig wlan0
```

### 三个坑

**① flag 必须是字符串，不能用数字**

```
g_wapi_essid_flags:   [0] WAPI_ESSID_OFF  [1] WAPI_ESSID_ON  [2] WAPI_ESSID_DELAY_ON
g_wapi_wpa_ver_flags: [0] WPA_VER_NONE  [1] WPA_VER_1  [2] WPA_VER_2  [3] WPA_VER_3

❌ wapi essid wlan0 SSID 1               → bes_wl_set_ssid ret=-22 (EINVAL)
✅ wapi essid wlan0 SSID WAPI_ESSID_ON
```

**② 密码先设、SSID 后设**

`wapi psk` 不报错也不连接；真正触发连接的是 `wapi essid ... WAPI_ESSID_ON`，
它内部调用 `bwifi_add_network ssid:xxx, passwd:yyy` 把两者一起提交。

**③ `renew` 报 `netlib_obtain_ipv4addr() failed` 是正常的**

还没 associate 时 DHCP 必然失败。连上之后 IP 往往已经由驱动内部流程拿到。

### 验证

```
ifconfig wlan0            → inet addr:192.168.0.215  DRaddr:192.168.0.1  at RUNNING
ping -c 3 <主机 IP>        → 通
nslookup www.baidu.com    → 能解析
```

---

## 五、显示驱动要点

- 帧缓冲设备是 **`/dev/fb0`**（不是 `/dev/lcd0`）
- `CONFIG_LV_USE_NUTTX_LCD` 未设置 → `lv_nuttx_dsc_init()` 默认给
  `fb_path = "/dev/fb0"`，正好对上
- `velatime_main.c` 里 `info.fb_path = "/dev/lcd0"` 那行包在
  `#ifdef CONFIG_LV_USE_NUTTX_LCD` 里，**没被编译进去**，不用管

---

## 六、为什么不用蓝牙连手机

板子硬件有蓝牙（BES2800 是蓝牙音频 SoC），但：

1. 我们的 AP 镜像里蓝牙是**关闭**的
   （`CONFIG_BES_BTH_MIDDLEWARE_DISABLE=1`，`bluetoothd` 没编进去）
2. BES 的蓝牙协议栈跑在**另一个核**，需要单独烧 `nuttx_bth.bin`
3. **最关键：手机浏览器不能用蓝牙**
   —— Web Bluetooth 要求 HTTPS + Chrome，且 iOS Safari 完全不支持；
   我们的控制台是 HTTP 网页

**所以"手机浏览器 ↔ 开发板"这个场景下 WiFi 是唯一可行方案。**
蓝牙要成立，必须额外写一个 Android/iOS 原生 App。
