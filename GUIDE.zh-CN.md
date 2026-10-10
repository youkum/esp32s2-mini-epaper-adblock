# 详细指南

**ESP32-S2 mini 四色电子纸 DNS 广告拦截器**固件从零到跑通的完整步骤：
构建、烧写、配置与故障排查。

如果只想要最短路径，见 [README](README.zh-CN.md#快速开始)。本文是长版本：
每一部分是什么、为什么这样设计，以及出问题时该怎么办。

> **阅读约定**：本文为中文说明，但**信号名、GPIO 编号、接口名、命令、路径、
> 标识符等技术术语一律保留英文原文**，以免翻译造成歧义。例如 `EPD DC`
> 不译作「直流」、`EPD BUSY` 不译作「繁忙」、`Board` 不译作「董事会」、
> `MIT` 不译作「麻省理工学院」、`trunk` 不译作「树干」。
>
> **AI 构建项目。** 本文内容由 AI 代理编写并在真实硬件上验证。信任前请先阅读
> [AI 构建声明](README.zh-CN.md#ai-构建声明)。

---

## 目录

1. [你需要准备什么](#1-你需要准备什么)
2. [接线](#2-接线)
3. [安装工具链](#3-安装工具链)
4. [构建](#4-构建)
5. [烧写](#5-烧写)
6. [首次启动：读取密码](#6-首次启动读取密码)
7. [接入网络](#7-接入网络)
8. [把客户端指向它](#8-把客户端指向它)
9. [Web 控制台](#9-web-控制台)
10. [电子纸仪表盘](#10-电子纸仪表盘)
11. [修改管理员密码](#11-修改管理员密码)
12. [blocklist：更新与定制](#12-blocklist更新与定制)
13. [构建合成发行镜像](#13-构建合成发行镜像)
14. [tools 目录](#14-tools-目录)
15. [性能与已知上限](#15-性能与已知上限)
16. [故障排查](#16-故障排查)
17. [架构说明](#17-架构说明)

---

## 1. 你需要准备什么

**硬件**

* 一块 **ESP32-S2 mini** 开发板。已在 **ESP32-S2FNR2** 上验证：4 MB flash +
  **2 MB PSRAM**。PSRAM 是必需的——128 KB Bloom filter 就分配在那里。
  没有 PSRAM 的型号无法工作。
* 一块 **7.5 英寸 768×552 四色电子纸**，JDY79668 / GDEY075Z08 class。
* 一根 USB-C（或 USB-micro，视板子而定）**数据线**。
* 9 根母对母杜邦线。
* 可选：接在 Battery ADC 引脚上的 LiFePO4 / 18650 电池，做成无线版本。

**软件**

* **PlatformIO Core**（`python -m pip install --upgrade platformio`），或
  PlatformIO IDE 插件。
* **Python 3.8+**，用于辅助脚本。
* **Node.js**，用于 `tools/*.js` 的字体 / 预览脚本（可选，只有改屏上文字时
  才需要）。
* **esptool**（PlatformIO 会自动安装；只有走合成镜像路径时才直接用到）。

**知识**

* 基本的终端操作能力。
* 知道你路由器里 DNS 设置在哪（好把整个网络指到这台设备）。

---

## 2. 接线

按下表把面板接到 ESP32-S2 mini。这些引脚沿用了 InkSight 的 S2 配置，固定在
[`src/config.h`](3.98/merged-adblock-epd/src/config.h) 中。

| Panel pin | ESP32-S2 mini GPIO | 说明 |
|---|---|---|
| MOSI / DIN | **11** | bit-bang，任意 GPIO 均可 |
| SCK / CLK | **7** | |
| CS | **12** | |
| DC | **17** | data/command |
| RST | **16** | |
| BUSY | **4** | 刷新期间面板会拉低此脚 |
| VCC | 3V3 | 请对照你面板的 datasheet |
| GND | GND | 共地 |

板载引脚：

| 功能 | GPIO |
|---|---|
| Battery ADC | 6 |
| Config button | 0 (BOOT) |
| Status LED | 15 |

> **切勿**把面板改接到硬件 SPI 外设上。本固件是**刻意**用 bit-bang 驱动 SPI
> 的——原因见 [性能与已知上限](#15-性能与已知上限)。

---

## 3. 安装工具链

```bash
python -m pip install --upgrade platformio
```

首次构建时，PlatformIO 会下载 `espressif32` platform、Arduino-ESP32 framework
和工具链。这个下载很大（数百 MB），网络慢请耐心等待。

> 网络很慢时，可以用 `tools/fetch_pio_framework.js` 通过多条并行 HTTP range
> 请求直接把 framework tarball 拉进 PlatformIO 的缓存。见
> [tools 目录](#14-tools-目录)。

---

## 4. 构建

固件位于子目录 `3.98/merged-adblock-epd/`，默认 PlatformIO 环境为 `s2mini`。

```bash
python -m platformio run -d 3.98/merged-adblock-epd
```

成功构建以类似下面的输出结尾：

```
RAM:   [==        ]  25.0% (used ... bytes from 327680 bytes)
Flash: [========= ]  87.3% (used ... bytes from 1179648 bytes)
========================= [SUCCESS] =========================
```

产物位于 `3.98/merged-adblock-epd/.pio/build/s2mini/`：

| 文件 | 用途 |
|---|---|
| `firmware.bin` | 应用程序 |
| `bootloader.bin` | 二级 bootloader |
| `partitions.bin` | partition table |
| `firmware.elf` / `.map` | 调试符号 / 内存映射 |

**flash 预算。** 镜像约占 1.125 MB OTA slot 的 **87%**。还有增长空间，但不大
——大改动后请留意这个数字。

---

## 5. 烧写

### 方式 A — 常规上传（app + 文件系统）

```bash
python -m platformio run -d 3.98/merged-adblock-epd -t upload      # app
python -m platformio run -d 3.98/merged-adblock-epd -t uploadfs    # LittleFS (blocklist)
```

### 方式 B — 合成单个镜像，从 0x0 烧写

```bash
python -m platformio run -d 3.98/merged-adblock-epd -t buildfs
python 3.98/merged-adblock-epd/tools/make_release.py
esptool.py --chip esp32s2 --port COMx write_flash 0x0 \
    3.98/merged-adblock-epd/firmware-merged.bin
```

见 [构建合成发行镜像](#13-构建合成发行镜像)。

### 关于首次 upload 失败

在控制台走芯片**原生 USB CDC**（无 UART 桥）的板子上，`upload` 几乎每次都会
在第一次失败，报 `OSError(22, 'device does not support the request')`——芯片
重新枚举时端口会消失。**再跑一次即可**，第二次就成功。

`tools/flash_and_capture.ps1`（Windows）会自动重试，并抓取串口控制台：

```powershell
powershell -File 3.98/merged-adblock-epd/tools/flash_and_capture.ps1 -Port COM3 -CaptureSec 45
```

加 `-SkipBuild` 可跳过重新构建直接烧写。

> **读取原生 USB 控制台需要同时拉高 DTR 和 RTS。** ESP32-S2 的 USB CDC 只有在
> 两者都置位时才报告 "connected"。很多串口工具默认把它们拉低，于是设备发出的
> 每个字节都被静默丢弃。抓取脚本已替你置位。

---

## 6. 首次启动：读取密码

以 **115200** 波特率打开串口控制台。每次启动设备都会打印管理员凭据：

```
AdBlock EPD booting (reset=3 heap=144544 psram=2095103)
[FS] mounted total=1703936 used=659456 heap=142100
[AUTH] created admin account: user admin password Xy3kQp9RmN
[BL] 103366 domains, bloom 128 KB
[BL] selftest 4/4 blocked, 3/3 clean
[BOOT] blocklist/persistence loaded, painting boot screen
[DASH] frame streamed in 1347 ms, panel refreshing
[EPD] arm=239 rows=1107 stream=1347 refresh=15339 off=70 total=16756 ms
[HB] up 23s ip 192.168.10.190 wl 3 boot 3 dom 103366 bl 128KB q 0 blk 0/0 top - 0 heap 86016 ps 1948939
```

* `[AUTH] created ...` → 全新设备，已生成新密码。
* `[AUTH] admin account: ...` → 已有密码，重新打印。
* `[HB]` 每 5 s 重复一次：uptime、IP、域名数、blocked/total、Top 域名、
  free heap 与 PSRAM。若出现 `ip offline`，说明它还没上网。

**把密码记下来。** 它是 Web 控制台的登录凭据（`admin` / 该密码）。密码每次
启动都打印是**有意**的：控制台在 Basic Auth 后面，若只显示一次，忘记后就只能
抹掉设备才能恢复——而串口本就是物理接触设备的方式。

---

## 7. 接入网络

凭据按以下顺序尝试，第一个存在的胜出：

1. **`/wifi.cfg`**（LittleFS）——由 setup portal 写入。
2. **`secrets.h`**（`WIFI_SSID` / `WIFI_PASS`）——除非仍是出厂占位符。
3. **ESP-IDF 的 NVS 存储**——之前固件保存的网络。

若都不可用，设备会开启一个名为 **`AdBlock-Setup`** 的开放接入点，并绘制
「配网模式」画面；控制台日志显示：

```
[WIFI] no credentials in /wifi.cfg, secrets.h or NVS
[SETUP] AP AdBlock-Setup open at http://192.168.4.1/
```

### 通过 setup portal 入网

1. 用手机 / 电脑连接 WiFi **`AdBlock-Setup`**（无密码）。
2. 浏览器打开 **`http://192.168.4.1/`**。
3. 填 WiFi 名称与密码，保存。
4. 设备写入 `/wifi.cfg`，重启为 station 并加入网络。

### 之后如何再进 portal

连上网络后，portal 就不能再用 IP 访问了。三种回去的方式：

* **按住 BOOT 键 3 s** —— 立即重新开启 `AdBlock-Setup`（**短按**只是重绘
  仪表盘）。
* 在控制台的 **网络** 卡片：保存 `/setwifi` → 设备重启并连到新网络。
* **什么都不做** —— 若链路掉线，固件每 30 s 重试，连续 **6 次**失败后
  （约 3 分钟）自动重开 portal。

> portal 会让设备进入 AP 模式，因此在你配好之前 DNS 拦截会暂停。

---

## 8. 把客户端指向它

设备就是端口 53 上的普通 DNS 服务器（UDP + TCP）。两种用法：

**推荐 —— 整个网络：** 把它设为路由器的上游 DNS：

```
primary DNS   -> 设备 IP (例如 192.168.10.190)
secondary DNS -> 任意一台「非」sinkhole 的 DNS
```

**单客户端：** 只把你关心的设备指向该设备 IP。

设备**不**运行 DHCP，因此它不必是网关——只需作 resolver。被拦截的查询返回
`0.0.0.0` / `::`；其余则是从 `9.9.9.9` 转发回来的真实答案。

---

## 9. Web 控制台

浏览器访问 `http://adblock.local/`（mDNS）或面板头部显示的 IP。整个 UI 按
设计是中文的，且自包含（无 CDN）。

**卡片**

| 卡片 | 作用 |
|---|---|
| 顶部磁贴 | 今日拦截、今日查询、拦截率、累计拦截、规则数量、free heap、RSSI、芯片温度 |
| 每小时拦截 | 最近 24 h 被拦截查询的直方图 |
| 域名排行榜 | 今日 Top 被拦截域名 |
| 客户端 | 每个客户端的 IP/MAC、blocked/allowed、ban 切换 |
| 自定义拦截域名 | 添加 / 删除指定域名 |
| 规则更新 | 远程 blocklist URL + 自动更新间隔，或上传 `.bin` |
| 设备 | uptime、Bloom filter、pending 查询、累计值、**立即刷新屏幕** |
| 网络 | 修改 WiFi |
| 管理员密码 | 修改 `admin` 密码 |

**认证模型。** Basic Auth（`admin` + 你的密码）加上一个 CSRF `token`；token
由 `/stats` 返回，每次写操作都必须带上。页面每 5 s 拉取一次 `/stats`。

---

## 10. 电子纸仪表盘

面板**每 3 分钟**做一次四色全刷（`DASH_REFRESH_MS`）。每次刷新端到端约 17 s，
但在后台运行，因此 DNS 持续应答（见 [架构说明](#17-架构说明)）。

显示内容：

* 今日拦截数（大号数字），以及总查询数与拦截率；
* 24 条每小时直方图；
* Top 被拦截域名及其命中数；
* 页脚：累计值、uptime 与最后更新时间。

**手动刷新：** 控制台的 **立即刷新屏幕** 按钮，或**短按** BOOT 键。

不用烧写也能在 PC 上预览版式：`node 3.98/merged-adblock-epd/tools/preview_dashboard.js`
→ `preview-dashboard.png`。

---

## 11. 修改管理员密码

控制台的 **管理员密码** 卡片：

1. 两次输入新密码（6–64 个字符，可打印 ASCII，不含空格）。
2. 点「修改密码」。

设备会校验、写入 `/auth.cfg` 并记录日志。浏览器在下一个请求会被重新要求登录
——用新密码登录即可。

**忘记了？** 它在每次启动时都打印在串口控制台。要强制生成全新的，抹掉 LittleFS
上的 `/auth.cfg`（或重新烧写一个不含 `/auth.cfg` 的合成镜像）。

---

## 12. blocklist：更新与定制

### 从源列表重建

```bash
cd 3.98/merged-adblock-epd
python tools/build_blocklist.py data/blocklist.bin           # StevenBlack + Hagezi Light
python tools/build_blocklist.py data/blocklist.bin a.txt b.txt   # 你自己的列表/URL
```

选项：

| 参数 | 含义 |
|---|---|
| `--bloom-bits N` | Bloom filter 位数（默认 1,048,576 = 128 KB） |
| `--no-bloom` | 跳过 Bloom filter |

格式固定：20-byte header + Bloom filter + 排序后的 40-bit 哈希，magic `CADB`、
`HASH_BYTES = 5`。与上游项目**字节兼容**。

### 上传列表

* **从 PC：** 把 `blocklist.bin` 放进 `data/` 后 `pio run -t uploadfs`，或用
  控制台的 规则更新 卡片上传 `.bin`。
* **远程自动更新：** 把你的 `.bin` 托管到某处并在控制台填 URL；设备会按你选的
  间隔重新下载。

### 自定义单个域名

在 自定义拦截域名 卡片里添加。它们存于 `/custom.txt`，重启后保留。

---

## 13. 构建合成发行镜像

合成镜像是一整块从 `0x0` 烧写的单文件——方便 ESP Web Tools、`esptool-js`，
或交给他人。

```bash
python -m platformio run -d 3.98/merged-adblock-epd -t buildfs
python 3.98/merged-adblock-epd/tools/make_release.py
# -> 3.98/merged-adblock-epd/firmware-merged.bin
```

它按板子使用的偏移打包：

| 偏移 | 内容 |
|---|---|
| `0x1000` | `bootloader.bin` |
| `0x8000` | `partitions.bin` |
| `0xE000` | `boot_app0.bin` |
| `0x10000` | `firmware.bin` |
| `0x250000` | `littlefs.bin`（blocklist） |

> **`-t buildfs` 很重要。** 没有它，`make_release.py` 仍会运行，但会漏掉文件
> 系统（打印 `(absent: run 'pio run -t buildfs' ...)`），烧出来的设备拦不住
> 任何东西、仪表盘也是空的。只有在你**故意**想要那样时才加 `--no-fs`。

**预编译合成镜像**在
[Releases](https://github.com/youkum/esp32s2-mini-epaper-adblock/releases) 页面。

---

## 14. tools 目录

`3.98/merged-adblock-epd/tools/` —— 全部可在仓库根目录离线运行。

| 工具 | 作用 |
|---|---|
| `build_blocklist.py` | 编译源列表 → `blocklist.bin` |
| `test_build_blocklist.py` | blocklist 构建器的单元测试 |
| `make_release.py` | 合并构建产物 → `firmware-merged.bin` |
| `gen_cjk_font.py` | 从 SimHei 栅格化中文点阵字体表 |
| `check_font.js` | 报告 UI 中缺失于字体表的字符 |
| `preview_dashboard.js` | 不烧写即渲染 `preview-dashboard.png` |
| `flash_and_capture.ps1` | 带重试的烧写 + 抓串口控制台（Windows） |
| `fetch_pio_framework.js` | 通过并行 range 请求拉取 framework tarball |
| `probe_flash.ps1` / `read_part.ps1` | 回读 flash 分区 |

注意 `gen_cjk_font.py` 需要 **SimHei**（`simhei.ttf`，Windows 自带）；在其他
系统上用 `CJK_FONT` 环境变量覆盖。

---

## 15. 性能与已知上限

**延迟**（硬件实测，单条查询）：

| 查询类型 | p50 | p90 |
|---|---|---|
| blocked（本地） | ~22 ms | ~28 ms |
| forwarded（上游） | ~245 ms | ~255 ms |
| NXDOMAIN | ~246 ms | ~263 ms |

**吞吐。** 家用 resolver 是个位数 qps，无损。持续负载在接近 100 qps 时开始
出现问题：

| 提供速率 | 丢包 |
|---|---|
| ≤ 50 qps | 0% |
| ~100 qps | ~44% |
| ~200 qps | ~77% |
| ~300 qps | ~87% |

该上限是**单次突发约 6 个数据报的硬天花板**——lwIP 的 UDP 接收邮箱深度
（`CONFIG_LWIP_UDP_RECVMBOX_SIZE = 6`，编译进预制的 `liblwip.a`）。把查询间隔
拉开到 10 ms 几乎可全部收回。这是**已记录、已接受的上限**，不是 bug：完整推理
与「为何 `parsePacket()` 排空循环无济于事」见
[`AGENTS.md`](3.98/merged-adblock-epd/AGENTS.md) 的 "burst ceiling" 条目。

**面板刷新。** 一帧包含两种截然不同的耗时：传输比特约 1.4 s，之后**约 15 s**
供面板移动色素。固件把两者拆开，让那 ~15 s 在后台运行；这就是为什么 `[EPD]`
日志行里的 `refresh=` 字段是面板的**健康检查**（~15 s = 真刷新；几毫秒 =
更新其实没被接受）。

**为什么用 bit-bang SPI。** 曾短暂尝试硬件 SPI 外设，帧看似快了 ~35×，实际
什么都没刷新——那 15 s 属于面板，不属于总线。因此**有意**保留 bit-bang。

---

## 16. 故障排查

| 现象 | 原因 / 处理 |
|---|---|
| 首次 `upload` 报 `OSError(22)` | 原生 USB 重新枚举。**再跑一次**（或用 `flash_and_capture.ps1`）。 |
| 串口控制台无输出 | 原生 USB CDC 需要同时拉高 **DTR 和 RTS**。用 `flash_and_capture.ps1`，或在终端里都置位。 |
| `[HB] ... ip offline` | 没上网。连 `AdBlock-Setup` 配置 WiFi，或按住 BOOT 3 s 重开 portal。 |
| 画面空白 / 陈旧，`refresh=` 只有几毫秒 | 面板更新未被接受。检查接线（尤其 BUSY 在 GPIO 4）与供电。 |
| 画面被撕裂 / 镜像 | band-window 或旋转变换被破坏。只能通过 `drawTextColor()` / `fillRectColor()` 绘制，且每行的 window 必须由其**数据行**推导——见 `AGENTS.md`。 |
| 面板似乎一直不更新 | 四色全刷要 ~15 s 且会闪烁；给它时间。 |
| `pio` 下载卡住 | 用 `tools/fetch_pio_framework.js`，或换个好网络重试。 |
| 重新烧写后控制台 401 | 合成镜像没有 `/auth.cfg`，已生成新密码——读串口日志。 |
| 整个网络失去 DNS | 设备宕机或掉线；路由器应配置一个非 sinkhole 的备用 DNS。 |
| 中文显示成空白方框 | `cjk_font.cpp` 缺字。跑 `node tools/check_font.js`，再用 `gen_cjk_font.py` 重新生成。 |

---

## 17. 架构说明

以下是固件赖以成立的规则；完整细节见
[`AGENTS.md`](3.98/merged-adblock-epd/AGENTS.md)。

* **没有全帧缓冲。** 768×552 @ 2 bpp 为 105,984 字节——在 S2 的 176 KB DRAM
  （还要与 WiFi 共享）里放不下。画面从不整体物化：`epdSendFrame()` 一次渲染
  并流式发送**一个 64 行的 band**，写入 12,288 字节的静态缓冲。
* **绝不阻塞 `loop()`。** 一帧发完后玻璃会把 BUSY 拉低约 15 s；先让
  `epdSendFrame()` 返回，由 `loop()` 驱动 `epdServiceRefresh()`，DNS 才能持续
  应答。
* **两个相互抵消的旋转变换。** `drawTextColor()` / `fillRectColor()` 应用其一，
  `epdRowData()` 应用其二。只能通过 helper 绘制，**绝不要**直接索引 `colorBuf`。
* **window 由数据行推导**，绝不用运行计数器——见 `epd_driver.cpp` 里的 `0x83`
  window 映射。
* **PSRAM 是刻意使用的**（128 KB Bloom filter）；每次查询的热状态留在 DRAM。
* **DNS 热路径不分配内存**——`trackerRecord()` 不用 `String`、不 `malloc`、
  不打日志。

---

*问题或修复：请在
[issue tracker](https://github.com/youkum/esp32s2-mini-epaper-adblock/issues) 提 issue。*
