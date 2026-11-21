# 详细指南

**简体中文**

**ESP32-S2 mini 四色电子纸 DNS 广告拦截器**固件从零到跑通的完整步骤：
构建、烧写、配置与故障排查。

如果只想要最短路径，见 [README](README.md#快速开始)。本文是长版本：
每一部分是什么、为什么这样设计，以及出问题时该怎么办。

> **阅读约定**：本文为中文说明，但**信号名、GPIO 编号、接口名、命令、路径、
> 标识符等技术术语一律保留英文原文**，以免翻译造成歧义。例如 `EPD DC`
> 不译作「直流」、`EPD BUSY` 不译作「繁忙」、`MIT` 不译作「麻省理工学院」。
>
> 本文由 AI 代理辅助维护。历史硬件数据和本次构建验证分别见
> [性能与已知上限](#15-性能与已知上限)和[构建验证记录](docs/BUILD-VERIFICATION.md)。

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
18. [v1.3.0 部署与回归检查](#18-v130-部署与回归检查)

---

## 1. 你需要准备什么

**硬件**

* 一块 **ESP32-S2 mini** 开发板。已在 **ESP32-S2FNR2** 上验证：4 MB flash +
  **2 MB PSRAM**。推荐使用同型号：128 KB Bloom filter 放在 PSRAM 中。
  分配失败会回退到 flash 查找，性能会降低；其他板型未作为部署目标验收。
* 一块 **7.5 英寸 768×552 四色电子纸**，JDY79668 / GDEY075Z08 class。
* 一根 USB-C（或 USB-micro，视板子而定）**数据线**。
* 9 根母对母杜邦线。
* 使用稳定的板卡电源；Battery ADC 是电压测量输入，不能直接接电池供电。
  电池方案需自行设计稳压、充电保护和 ADC 分压，并遵守板卡额定电压。

**软件**

* **PlatformIO Core 6.2.0**（本次验证版本），或
  PlatformIO IDE 插件。
* **Python 3.10+**，用于 PlatformIO 和辅助脚本。
* **Node.js**，用于 `tools/*.js` 的字体 / 预览脚本（可选，只有改屏上文字时
  才需要）。
* **Pillow 8.0+**，仅在重新生成中文字形时需要；普通构建使用已提交的字形表。
* **esptool**：PlatformIO 自带的副本用于上传和合成；直接运行 `python -m esptool`
  需要在当前 Python 环境中另行安装。

**知识**

* 基本的终端操作能力。
* 知道你路由器里 DNS 设置在哪（好把整个网络指到这台设备）。

---

## 2. 接线

按下表把面板接到 ESP32-S2 mini。这些引脚沿用了 InkSight 的 S2 配置，固定在
[`src/config.h`](3.98/merged-adblock-epd/src/config.h) 中。

| 面板引脚 | ESP32-S2 mini GPIO | 说明 |
|---|---|---|
| MOSI / DIN | **11** | bit-bang，任意 GPIO 均可 |
| SCK / CLK | **7** | |
| CS | **12** | |
| DC | **17** | data/command |
| RST | **16** | |
| BUSY | **4** | 刷新期间面板会拉低此脚 |
| VCC | 3V3 | 请对照你面板的数据手册 |
| GND | GND | 共地 |

板载引脚：

| 功能 | GPIO |
|---|---|
| 电池 ADC | 6 |
| 配置按键 | 0 (BOOT) |
| 状态 LED | 15 |

> **切勿**把面板改接到硬件 SPI 外设上。本固件是**刻意**用 bit-bang 驱动 SPI
> 的——原因见 [性能与已知上限](#15-性能与已知上限)。

---

## 3. 安装工具链

```bash
git clone https://github.com/youkum/esp32s2-mini-epaper-adblock.git
cd esp32s2-mini-epaper-adblock
python -m pip install "platformio==6.2.0"
```

后文命令默认从仓库根目录执行，代码块适用于 PowerShell / Bash；出现 `cd` 时
只影响该示例。已有检出可跳过 clone。`python` 必须指向安装了 PlatformIO 的解释器。

项目固定 `espressif32@6.12.0`，使用 Arduino-ESP32 2.0.17。首次构建下载工具链，
需要联网和数百 MB 空间；依赖完整安装后普通构建不需要下载列表或字体。
不要把平台版本改成不带版本号的 `espressif32`，那会自动选取不同的 SDK。

Windows 若遇到解压路径过长，可在当前 PowerShell 设置一个较短、可写的缓存路径：

```powershell
$env:PLATFORMIO_CORE_DIR = 'C:\pio-adblock'
```

这会使用独立缓存并重新安装依赖。构建、`buildfs`、上传和 `make_release.py`
应在同一环境中运行；新终端要重新设置该变量。下载失败先检查网络并重试。

---

## 4. 构建

固件位于子目录 `3.98/merged-adblock-epd/`，默认 PlatformIO 环境为 `s2mini`。

```bash
python -m platformio run -d 3.98/merged-adblock-epd -e s2mini
```

成功构建以 `[SUCCESS]` 结尾。实际大小与版本见[构建验证记录](docs/BUILD-VERIFICATION.md)。
文件位于 `3.98/merged-adblock-epd/.pio/build/s2mini/`：

| 文件 | 用途 |
|---|---|
| `firmware.bin` | 应用程序 |
| `bootloader.bin` | 二级 bootloader |
| `partitions.bin` | partition table |
| `firmware.elf` / `.map` | 调试符号 / 内存映射 |

**flash 预算。** 每个 OTA slot 为 `0x120000`（1,179,648 字节，即 1.125 MiB）。
构建输出的 RAM 数值只描述静态占用；运行时 WiFi、堆和 PSRAM 仍需通过日志观察。
`firmware.bin` 是应用程序；`firmware-merged.bin` 是从 0x0 烧写的整合镜像，两者不能互换。

---

## 5. 烧写

### 方式 A — 首次部署（app + 文件系统）

```bash
python -m platformio device list
python -m platformio run -d 3.98/merged-adblock-epd -e s2mini -t upload --upload-port COM3
python -m platformio run -d 3.98/merged-adblock-epd -e s2mini -t uploadfs --upload-port COM3
```

`COM3` 替换为实际端口；Linux 常为 `/dev/ttyACM0`，macOS 常为 `/dev/cu.usbmodem...`。
`uploadfs` 会重新生成并覆盖整个 LittleFS，丢失其中已有的密码、WiFi、自定义规则、
封禁和累计统计。**日常升级只执行 `upload`，或通过 Web OTA 上传 `firmware.bin`。**
若分区表发生变化，应按首次部署处理；升级前记录必要配置。

### 方式 B — 合成单个镜像，从 0x0 烧写

```bash
python -m platformio run -d 3.98/merged-adblock-epd -e s2mini
python -m platformio run -d 3.98/merged-adblock-epd -e s2mini -t buildfs
python 3.98/merged-adblock-epd/tools/make_release.py
python -m pip install "esptool==4.9.0"
python -m esptool --chip esp32s2 --port COM3 write_flash 0x0 3.98/merged-adblock-epd/firmware-merged.bin
```

合成镜像会以填充字节覆盖 NVS 区域，并以打包内容覆盖 LittleFS：原有 WiFi、密码和
统计可能丢失。它用于首次部署 / 重新初始化，**不能作为 Web OTA 文件**。
`--no-fs` 只省略文件系统，不能使整合镜像成为保留配置的升级方式。
见 [构建合成发行镜像](#13-构建合成发行镜像)。

### 关于首次 upload 失败

原生 USB CDC 板子可能因重新枚举出现 `OSError(22)`。关闭占用端口的串口工具，
等端口重新出现后更新端口名并重试。仍失败时按住 BOOT、按一下 RESET，再松开 BOOT
进入下载模式；没有 RESET 键的板子可按住 BOOT 后重新插入 USB。烧写后复位启动。

`tools/flash_and_capture.ps1`（Windows）会自动重试，并抓取串口控制台：

```powershell
powershell -File 3.98/merged-adblock-epd/tools/flash_and_capture.ps1 -Port COM3 -CaptureSec 45
```

脚本默认从自身位置定位固件目录，不依赖旧电脑路径；`-Root` 可指定其他检出。
它只上传应用程序，不上传文件系统。`-SkipBuild` 使用已有构建产物，应确认版本。

**备份与离线查看。** 完整重新初始化前保留 Flash 备份，备份含 WiFi、密码和 token，
仅保存在本机。离线查看已经运行过的 LittleFS 时，本次发现 PlatformIO 附带的旧
`mklittlefs` 会漏列设备后写入的配置；使用
[官方 mklittlefs 4.1.0](https://github.com/earlephilhower/mklittlefs/releases/tag/4.1.0)
读取核对，解包目录在 Windows 下使用相对路径。不要因旧工具漏列就覆盖原配置；
具体结果见[本次实机记录](docs/HARDWARE-TEST-2026-11-21.md)。

> **读取原生 USB 控制台需要同时拉高 DTR 和 RTS。** ESP32-S2 的 USB CDC 只有在
> 两者都置位时才报告 "connected"。很多串口工具默认把它们拉低，于是设备发出的
> 每个字节都被静默丢弃。抓取脚本已替你置位。

也可单独监控串口：

```bash
python -m platformio device monitor --port COM3 --baud 115200 --dtr 1 --rts 1
```

---

## 6. 首次启动：读取密码

以 **115200** 波特率打开串口控制台。每次启动设备都会打印管理员凭据。
以下是旧版日志示例，不是 v1.3.0 实测；新驱动在帧完成或故障时打印状态：

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

开机按以下优先级选择一组凭据；所选网络连接失败后进入 portal，
不会继续轮流测试优先级更低的网络：

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

**推荐 —— 整个网络：** 在路由器中为设备做 DHCP 地址保留，固定它的 IP，
再把该 IP 作为 DHCP 发给客户端的 DNS 地址。只修改路由器自身的上游 DNS 时，
应确认它会代客户端转发，否则不一定覆盖所有设备。

```
DNS -> 设备固定 IP（例如 192.168.10.190）
第二个 DNS -> 留空，或另一台有相同拦截规则的 DNS
```

**单客户端：** 只把你关心的设备指向该设备 IP。

设备**不**运行 DHCP，因此它不必是网关——只需作 resolver。被拦截的查询返回
`0.0.0.0` / `::`；其余则是从 `9.9.9.9` 转发回来的真实答案。

普通公共 DNS 作为第二地址时，客户端可能随时使用它，造成拦截绕过。
若主动选择公共 DNS 以提高可用性，应接受这一取舍。同步检查路由器 IPv6 DNS
通告、系统私有 DNS 和浏览器 DoH，避免客户端走其他解析器。
路由器代转发时，按客户端统计可能只看到路由器 IP。

先用单客户端验证，再推广到全网；重启或清理客户端 DNS 缓存后查询：

```bash
nslookup example.com 192.168.10.190
nslookup doubleclick.net 192.168.10.190
```

第一条应能解析；第二条在当前列表包含该域名时应返回 `0.0.0.0`（A）。
需要独立验证 TCP 时，使用已安装的 `dig +tcp @192.168.10.190 example.com`。

---

## 9. Web 控制台

浏览器访问 `http://adblock.local/`（mDNS）或面板头部显示的 IP。整个 UI 按
设计是中文的，且自包含（无 CDN）。

**卡片**

| 卡片 | 作用 |
|---|---|
| 顶部磁贴 | 今日拦截、今日查询、拦截率、累计拦截、规则数量、free heap、RSSI、芯片温度 |
| 每小时拦截 | 今日 00–23 时直方图，跨日清零（不是滚动 24 h） |
| 域名排行榜 | 今日 Space-Saving 近似 Top：计数上界 / 误差；真实计数位于二者差值到上界之间 |
| 客户端 | 每个客户端的 IP/MAC、blocked/allowed、ban 切换 |
| 自定义拦截域名 | 添加 / 删除指定域名 |
| 规则更新 | URL、间隔、立即后台下载、CA 设置、字节进度、本地上传 |
| 设备 | uptime、Bloom、pending、缓存、最大 loop 用时、面板 / OTA 状态、剩余容量 |
| 网络 | 修改 WiFi、主 / 备 DNS 上游 IPv4 |
| 管理员密码 | 修改 `admin` 密码 |

**认证模型。** Web 控制台使用 Basic Auth（`admin` + 密码），页面每 5 s 拉取
`/stats`。所有受保护的写接口还检查 CSRF `token`（128 bit，旧短 token 自动迁移）。
固件 / 列表上传在 `START` 阶段就检查 URL 中的 token 或 `X-CSRF-Token`，
不是写完才检查。AP 配网的 `/setwifi` 不要求认证。
页面为局域网 HTTP 服务，应在可信局域网中使用；不要把管理端口映射到公网。

---

## 10. 电子纸仪表盘

面板**每 3 分钟**请求四色全刷（`DASH_REFRESH_MS`）。v1.3.0 将复位、上电、
逐行发送、刷新、断电拆成状态机，每次服务最多发送两条完整行；每个 64 行分带
仍同步绘制一次。每帧固定统计快照，防止不同分带的数据不一致。
旧版端到端约 17 s / 面板刷新约 15 s 仅为历史参考；当前耗时须实测。
阶段 BUSY 超时为 45 s、单次尝试整帧上限 120 s，失败退避 2 s 后最多再试两次；
故障后保留错误信息，等待下次手动 / 周期请求。单个分带绘图、行发送和 flash 擦写
仍有有限同步开销（见 [架构说明](#17-架构说明)）。

显示内容：

* 今日拦截数（大号数字），以及总查询数与拦截率；
* 24 条每小时直方图；
* Top 被拦截域名及其命中数；
* 页脚：累计值、uptime 与最后更新时间。

**手动刷新：** 控制台的 **立即刷新屏幕** 按钮，或**短按** BOOT 键。
Web 返回 HTTP 202 表示请求已排队，面板空闲后执行；重复请求合并为一次待刷新。

不用烧写也能在 PC 上预览版式：`node 3.98/merged-adblock-epd/tools/preview_dashboard.js`
→ `3.98/merged-adblock-epd/preview-dashboard.png`。

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

对外分发列表或含列表的固件前，先核对 [第三方声明](THIRD-PARTY-NOTICES.md)。

```bash
python 3.98/merged-adblock-epd/tools/build_blocklist.py 3.98/merged-adblock-epd/data/blocklist.bin
# 或指定本地文件 / URL；a.txt、b.txt 为相对当前终端目录的路径
python 3.98/merged-adblock-epd/tools/build_blocklist.py 3.98/merged-adblock-epd/data/blocklist.bin a.txt b.txt
```

选项：

| 参数 | 含义 |
|---|---|
| `--bloom-bits N` | Bloom filter 位数（默认 1,048,576 = 128 KiB，建议保持默认） |
| `--no-bloom` | 跳过 Bloom filter |

格式固定：20-byte header + Bloom filter + 排序后的 40-bit 哈希，magic `CADB`、
`HASH_BYTES = 5`。与上游项目**字节兼容**。

默认列表需要联网，结果会随上游分支变化。格式兼容不代表任意列表都能装入：
文件必须完整、含至少一条哈希，并且放得下 LittleFS；替换时还需同时容纳旧文件和新文件。
构建器遇到任一来源读取失败、无有效域名或非法 Bloom 位数时退出并保留旧输出；
成功后通过临时文件原子替换。非 2 次幂参数也采用与固件一致的 64 位中间计算。
`--bloom-bits` 范围为 1..16,777,216；设备仍需足够 PSRAM，否则回退 flash 查找。
远程更新 URL 必须返回 `CADB` 二进制，不能直接填 hosts / 域名文本 URL。

### 上传列表

* **已有设备：** 优先用控制台的 规则更新 卡片上传 `blocklist.bin`，保留其他配置。
* **首次部署：** 把文件放进 `data/` 后执行本指南第 5 节的 `uploadfs`。
  已有设备使用此方式会覆盖整个文件系统。
* **远程自动更新：** 把你的 `.bin` 托管到某处并在控制台填 URL；设备会按你选的
  间隔重新下载。

上传和定时下载都会先写临时文件、检查 `CADB` 头和文件长度，再替换旧列表。
传输中断、文件结构不符或 LittleFS 空间不足时，应保留原列表；更新期间 LittleFS
需要同时容纳旧列表和新文件。结构检查不证明列表来源可信或哈希内容正确。
远程下载在单个后台任务中执行；任务只写 `/blocklist.tmp`，关闭后由主循环安装。
它与 OTA / 本地列表上传互斥，预留 8 KiB 文件系统余量，必须能与旧表共存。
响应头和正文均有长度上界、5 s 无数据超时、120 s 传输期限；连接超时 5 s，
TLS 握手另有 10 s 上限。支持 Content-Length、chunked 和连接关闭封帧，拒绝 gzip、
自动跳转及矛盾响应头。取消为协作式，库内连接 / flash 写入仍受各自调用耗时约束。
间隔为 0..720 小时，0 表示关闭；失败保留旧表，下次按配置间隔尝试，也可手动立即下载。

**HTTPS 配置步骤：**
1. 从站点运营方或可信 CA 官方渠道取得证书链所需 PEM CA，不把未经确认的证书当作可信根。
2. 在「规则更新 → HTTPS CA 设置」粘贴并保存（64..8192 字节，先解析再原子保存到 `/blocklist-ca.pem`）。
3. 确保已完成 NTP 对时，填写直接返回 `blocklist.bin` 的 HTTPS URL。
4. 点击「立即下载远程规则」，检查进度及终态；站点证书更新时重新维护 CA。

不使用 `setInsecure()`；未配置 CA / 未对时拒绝 HTTPS。HTTP 只宜用于可信本地来源，
不能证明数据来源真实性。结构校验不检查排序或 Bloom 内容；使用已通过构建器测试的文件。
本地上传仍由同步 WebServer 解析 multipart，慢客户端和 flash 写入可能暂停 DNS，
不能把后台下载优化宣传成所有上传完全无阻塞。

### 自定义单个域名

在 自定义拦截域名 卡片里添加。它们存于 `/custom.txt`，重启后保留。
自定义规则独立匹配，不受主黑名单 Bloom 或主表为空的限制；仍逐级检查域名后缀。
规则和封禁变更会清空 DNS 缓存；保存失败返回错误，不把未持久化结果说成成功。

---

## 13. 构建合成发行镜像

合成镜像是一整块从 `0x0` 烧写的单文件——方便 ESP Web Tools、`esptool-js`，
或交给他人。

本节命令仅生成本地产物。公开分发前必须处理
[第三方声明](THIRD-PARTY-NOTICES.md)中的列表出处、字体和平台组件事项，
保存源码提交、工具链版本、构建命令和文件校验和。历史二进制不能当作当前源码的验证结果。

```bash
python -m platformio run -d 3.98/merged-adblock-epd -e s2mini
python -m platformio run -d 3.98/merged-adblock-epd -e s2mini -t buildfs
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

`make_release.py` 默认要求全部产物齐备，并检查它们不超过对应容量；
缺少 `littlefs.bin` 会报错。显式 `--no-fs` 可以省略文件系统，但空设备将没有默认列表。
该脚本固定用于 `s2mini`，应用和文件系统偏移、容量读取自 `partitions.csv`。
它使用 `PLATFORMIO_CORE_DIR` 对应的 esptool，默认是用户目录下的 `.platformio`。
输出包括 SHA-256；脚本不会上传 GitHub、烧写设备或自动核对再分发许可。

**预编译合成镜像**在
[Releases](https://github.com/youkum/esp32s2-mini-epaper-adblock/releases) 页面。

---

## 14. tools 目录

`3.98/merged-adblock-epd/tools/` 中各工具用完整路径调用；不能假定它们都离线或都
可直接无参数运行。路径规则与额外依赖如下：

| 工具 | 作用 |
|---|---|
| `build_blocklist.py` | 编译源列表 → `blocklist.bin` |
| `test_build_blocklist.py` | blocklist 构建器的单元测试 |
| `test_make_release.py` | 合成脚本的缺文件、容量和分区偏移回归检查 |
| `make_release.py` | 合并构建产物 → `firmware-merged.bin` |
| `gen_cjk_font.py` | 从所选字体生成点阵；需 Pillow 和有适用许可的字体文件 |
| `check_font.js` | 报告 UI 中缺失于字体表的字符 |
| `preview_dashboard.js` | 不烧写即渲染 `preview-dashboard.png` |
| `flash_and_capture.ps1` | 带重试的烧写 + 抓串口控制台（Windows） |
| `fetch_pio_framework.js` | 联网下载缓存辅助工具；需提供 URL、缓存目标、SHA-256、字节数，非通用安装器 |
| `probe_flash.ps1` / `read_part.ps1` | 历史回读调试脚本，包含原开发机路径；使用前自行适配，不是部署步骤 |

普通构建无需重生成字体。修改屏上文字后先安装 `python -m pip install "Pillow>=8.0"`，
使用 `CJK_FONT` 指向自己有权使用的字体文件，再运行生成脚本、`check_font.js` 和预览。
脚本默认查找 Windows 的 `simhei.ttf`，不保证系统已安装；该字体的点阵再分发许可
尚未确认，公开发行前需处理[第三方声明](THIRD-PARTY-NOTICES.md)中的字体项。

---

## 15. 性能与已知上限

以下是改动前的历史硬件参考，测试网络、工具链和脚本未完整归档，不能作为当前版本承诺。
当前编译通过也不代表这些指标已复测。

**延迟**（历史硬件实测，单条查询）：

| 查询类型 | p50 | p90 |
|---|---|---|
| blocked（本地） | ~22 ms | ~28 ms |
| forwarded（上游） | ~245 ms | ~255 ms |
| NXDOMAIN | ~246 ms | ~263 ms |

**吞吐。** 历史持续负载记录：

| 提供速率 | 丢包 |
|---|---|
| ≤ 50 qps | 0% |
| ~100 qps | ~44% |
| ~200 qps | ~77% |
| ~300 qps | ~87% |

原测试环境的**单次突发约 6 个数据报上限**对应 lwIP 的 UDP 接收邮箱深度
（`CONFIG_LWIP_UDP_RECVMBOX_SIZE = 6`，编译进预制的 `liblwip.a`）。把查询间隔
拉开到 10 ms 的历史测试几乎可全部收回。完整推理
与「为何 `parsePacket()` 排空循环无济于事」见
[`AGENTS.md`](3.98/merged-adblock-epd/AGENTS.md) 的 "burst ceiling" 条目。

**主循环。** BOOT 键去抖与长按检测不等待按键释放；短按遇到面板忙时排队，
长按进入配网门户仍会断开当前 STA。仪表盘的 Top 10 每帧选取一次，不再为全部
9 个分带重复选取。本节原有硬件跑分为改动前的参考，未重新采集。

**面板刷新。** 历史比特传输约 1.4 s、面板移动色素约 15 s。v1.3.0 连传输也拆成完整行，
不再一次发送整帧。驱动必须观察到刷新 BUSY，未观察到或超时会报故障；
`refresh=` 只能佐证刷新发生，不能证明画面方向 / 内容正确。

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
| `pio` 下载卡住 | 检查网络后重试；依赖下载量较大。高级缓存下载工具需要完整参数，见第 14 节。 |
| 重新烧写后控制台 401 | 合成镜像没有 `/auth.cfg`，已生成新密码——读串口日志。 |
| 整个网络失去 DNS | 检查设备电源、WiFi 和地址保留；可临时切回公共 DNS 恢复网络，但这期间拦截会被绕过。 |
| 中文显示成空白 / 缺字 | 运行 `node 3.98/merged-adblock-epd/tools/check_font.js`；缺字时按第 14 节重生成，检查预览。 |
| 只有部分客户端没有拦截 | 检查 DHCP DNS、IPv6 DNS、系统私有 DNS、浏览器 DoH 和缓存。 |
| HTTPS 规则下载失败 | 检查 PEM CA、NTP 对时、主机名和证书链；需要直接 200 响应，不支持跳转 / gzip。失败保留旧表。 |
| 上传列表失败 | 必须是完整的 v1 `CADB` 文件；检查大小及 LittleFS 是否能同时容纳旧列表和新文件。 |
| 编译依赖解压报路径过长 | 固定项目平台版本，并按第 3 节设置较短的 `PLATFORMIO_CORE_DIR`。 |

---

## 17. 架构说明

以下是固件赖以成立的规则；完整细节见
[`AGENTS.md`](3.98/merged-adblock-epd/AGENTS.md)。

* **没有全帧缓冲。** 768×552 @ 2 bpp 为 105,984 字节；S2 标称 320 KiB RAM
  并非都可用作画布，内部堆还与 WiFi、网络栈共享。画面不整体物化：`epdSendFrame()` 一次渲染
  并流式发送**一个 64 行的 band**，写入 12,288 字节的静态缓冲。
* **面板状态机不等待 BUSY。** `epdSendFrame()` 只提交，`epdServiceRefresh()` 每轮最多发两行；
  一个分带渲染仍是同步短任务。保持完整字节 / 行的厂商时序，不声称所有循环零阻塞。
* **两个相互抵消的旋转变换。** `drawTextColor()` / `fillRectColor()` 应用其一，
  `epdRowData()` 应用其二。只能通过 helper 绘制，**绝不要**直接索引 `colorBuf`。
* **window 由数据行推导**，绝不用运行计数器——见 `epd_driver.cpp` 里的 `0x83`
  window 映射。
* **PSRAM 是刻意使用的**（128 KB Bloom filter）；每次查询的热状态留在 DRAM。
* **追踪热路径避免分配**——`trackerRecord()` 不用 `String`、不 `malloc`、
  不打日志。

---

## 18. v1.3.0 部署与回归检查

本版为待实机验收测试版，见[更新说明](docs/releases/v1.3.0.md)。已有设备建议只升级
应用 `firmware.bin`，保留 LittleFS / NVS，不运行 `uploadfs`。上传时可填写发行附件
`SHA256SUMS.txt` 中对应 **firmware.bin** 的 SHA-256；即使未填写摘要，也必须通过
完整长度、ESP32-S2 头部和 ESP-IDF 应用镜像校验才激活。OTA 失败不重启；成功响应后
延后重启。未启用未认证的 ArduinoOTA 服务，升级入口为 Web 或串口。
升级中不要断电；镜像校验不是签名或防降级机制。

**DNS 主备与缓存：** 主上游 2 s 未回应后改用备用，再等 2 s；最终失败返回 SERVFAIL。
应答必须匹配来源 IP / 端口、事务 ID 与原问题。UDP 最多 32 个 pending；正文优先用
PSRAM，失败降为内部内存 4 槽，完全分配失败返回失败响应。TCP 一次一个客户端 / 一个
问题，仍通过独立 UDP 套接字访问上游；上游 TC 响应原样传回，不实现上游 TCP 重试。

缓存固定 16 项，每项最多 512 字节，仅缓存单个标准 A / AAAA、IN 问题的正响应，
且无 EDNS、TC、权威 / 附加节，资源记录只接受 A / AAAA / CNAME。
NXDOMAIN、DNSSEC / EDNS 或其他情况不缓存；TTL 按经过时间逐条递减，驻留不超过
最小 TTL 或 300 s。问题大小写、类型、class 和标志原样匹配。
上游、规则、封禁及网络绑定变更时失效。先拦截再查缓存，不能用缓存绕过新自定义规则。
不要配置本机、回环、多播地址，也不要把会“转发回本设备”的路由器设为上游。

桌面回归（仓库根）：

```bash
python 3.98/merged-adblock-epd/tools/test_build_blocklist.py
python 3.98/merged-adblock-epd/tools/test_regressions.py
python 3.98/merged-adblock-epd/tools/test_make_release.py
# 需要 g++ / clang++；也可 --compiler 指向 Zig 可执行文件
python 3.98/merged-adblock-epd/tools/test_native.py
node 3.98/merged-adblock-epd/tools/check_font.js
python -m platformio run -d 3.98/merged-adblock-epd -e s2mini
```

C++ 测试编译固件共享算法与真实面板驱动，以模拟 GPIO / 时间验证状态和总线字节，
不是另一份 Python 实现。测试不覆盖真实电气时序、无线网络 / TLS 握手及掉电行为。
2026-11-21 已完成完整烧写、启动自检和屏幕目视检查，见
[本次实机记录](docs/HARDWARE-TEST-2026-11-21.md)。联网、更新失败保护和长跑继续按
[实机验收清单](docs/HARDWARE-TEST-CHECKLIST.md)记录。

*问题或修复：请在
[issue tracker](https://github.com/youkum/esp32s2-mini-epaper-adblock/issues) 提 issue。*
