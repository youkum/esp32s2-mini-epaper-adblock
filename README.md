# esp32s2-mini-epaper-adblock

**简体中文**

把一块 **ESP32-S2 mini** 变成自成一体的 **DNS 广告拦截器**，统计信息绘制在
**7.5 英寸四色电子纸「魔法屏」**（768×552，JDY79668 / GDEY075Z08 class）上。
不需要云后端或手机 App；Web 控制台使用设备本地的管理员账号。
一块主板、一块屏、一根电源线，为使用该 DNS 的客户端拦截已列入黑名单的域名。
DNS 拦截不能移除同域名承载的广告；浏览器或 App 的 DoH / 私有 DNS 可能绕过它。

面板每 3 分钟刷新一次，显示设备自己拦截到的数据——今日拦截数、拦截率、
每小时直方图与 Top 域名；与此同时 DNS 引擎在后台持续应答查询。其余功能
（按客户端的统计、自定义规则、封禁列表、blocklist 更新、OTA、密码）都在
固件自带的中文 Web 控制台里。

> **本项目使用 AI 编码代理辅助开发，是基于开源代码的衍生作品。**
> 上游来源见 [开源署名](#基于开源构建署名)。早期版本有真机记录；后续改动的
> 编译、脚本检查和真机验收应分别记录，不能由历史实测推断当前版本已经通过。

---

## 目录

- [它做什么](#它做什么)
- [硬件](#硬件)
- [快速开始](#快速开始)
- [详细指南](GUIDE.md) — 完整的构建、烧写、首次启动、控制台、blocklist 与故障排查教程
- [下载 / 发行版](#下载--发行版)
- [基于开源构建（署名）](#基于开源构建署名)
- [AI 构建声明](#ai-构建声明)
- [仓库布局](#仓库布局)
- [许可](#许可)

> **阅读约定**：本文为中文说明，但**信号名、GPIO 编号、接口名、命令、路径、
> 标识符等技术术语一律保留英文原文**，以免翻译造成歧义或误操作。例如
> `EPD DC` 不译作「直流」，`EPD BUSY` 不译作「繁忙」，`MIT` 不译作
> 「麻省理工学院」。

---

## 它做什么

* **端口 53 上的 DNS sinkhole**（UDP **与** TCP 双栈）。被拦截域名返回
  `0.0.0.0`（A） / `::`（AAAA）；其余查询异步转发到 `9.9.9.9`。
* **blocklist 存于 flash**：排序后的 40-bit FNV-1a 哈希表，先用分配在
  PSRAM 中的 128 KB Bloom filter 预过滤；PSRAM 分配失败时回退到 flash 查找。
  其 v1 格式（magic `CADB`、
  `HASH_BYTES = 5`）与上游
  [9M2PJU/ESP32-C3-AdBlocker](https://github.com/9M2PJU/9M2PJU-ESP32-C3-AdBlocker)
  项目兼容。仓库内旧列表有 **103,366 条哈希记录**，准确输入快照尚未记录，
  来源限制见 [第三方声明](THIRD-PARTY-NOTICES.md)。
* **按客户端追踪**：IP、MAC、blocked/allowed 计数、ban/unban。
* **自包含的 Web 控制台**（Basic Auth + CSRF，无 CDN、无外部资源）：
  实时数据块、每小时图表、Top 域名表、按客户端的行、自定义域名 chips、
  blocklist 自动更新 URL、固件 / blocklist 上传、**管理员密码修改**表单，
  以及「立即刷新屏幕」按钮。
* **电子纸仪表盘，每 3 分钟刷新**：今日拦截数、总查询数与拦截率、
  24 条每小时直方图、Top 被拦截域名，以及含累计值与 uptime 的页脚。
* **可自恢复的 WiFi 配置**：开机时依次尝试 `/wifi.cfg`、`secrets.h`、
  再是 ESP-IDF 的 NVS 存储；全部失败则开启 `AdBlock-Setup` 接入点。
  若运行中掉线，每 30 s 重试一次，连续 6 次失败后自动重新开启该门户。
  按住 **BOOT** 键 3 s 也可随时手动重新开启。
* **OTA** 固件升级与 **mDNS**（`adblock.local`）。

## 硬件

| 部分 | 说明 |
|---|---|
| 开发板 | ESP32-S2 mini，已在 **ESP32-S2FNR2** 上验证 — 4 MB flash + 2 MB PSRAM |
| 面板 | 7.5 英寸 768×552 **四色**电子纸，JDY79668 / GDEY075Z08 class |
| 接线 | 见 [详细指南](GUIDE.md#2-接线) 中的引脚表 |

| 信号 | GPIO |
|---|---|
| EPD MOSI | 11 |
| EPD SCK | 7 |
| EPD CS | 12 |
| EPD DC | 17 |
| EPD RST | 16 |
| EPD BUSY | 4 |
| 电池 ADC | 6 |
| 配置按键 | 0 (BOOT) |
| 状态 LED | 15 |

## 快速开始

```bash
git clone https://github.com/youkum/esp32s2-mini-epaper-adblock.git
cd esp32s2-mini-epaper-adblock
python -m pip install "platformio==6.2.0"
python -m platformio run -d 3.98/merged-adblock-epd -e s2mini

# 首次部署：COM3 换为你的串口，Linux/macOS 使用实际设备路径
python -m platformio run -d 3.98/merged-adblock-epd -e s2mini -t upload --upload-port COM3
python -m platformio run -d 3.98/merged-adblock-epd -e s2mini -t uploadfs --upload-port COM3
```

项目固定 `espressif32@6.12.0`，避免自动升级到 Arduino-ESP32 3.x。
**`uploadfs` 会覆盖整个文件系统，包括已有 WiFi、密码、自定义规则和累计统计。**
已有设备升级通常只上传应用程序，或在 Web 控制台上传 `firmware.bin`；
Web OTA 不能上传 `firmware-merged.bin`。合成镜像路径见 [指南](GUIDE.md#13-构建合成发行镜像)。

以 **115200** 波特率打开串口控制台读取管理员密码（每次启动都会打印）。
然后浏览器访问 `http://adblock.local/`（或面板上显示的 IP），
用 **`admin`** 登录。

**完整教程 → [GUIDE.md](GUIDE.md)。**

## 下载 / 发行版

历史预编译的**合成固件镜像**（`firmware-merged.bin`，从偏移 `0x0` 烧写）附在
[**Releases**](https://github.com/youkum/esp32s2-mini-epaper-adblock/releases)
页面。合成镜像打包了 bootloader + partition table + 应用程序 + LittleFS
（含 blocklist），所以一个文件就能得到一台真正能拦截的设备。

历史 Release 不随 `master` 的源码提交自动更新。自行构建的验收记录见
[构建验证记录](docs/BUILD-VERIFICATION.md)；对外发布新镜像前核对
[第三方声明](THIRD-PARTY-NOTICES.md)。

> 全新烧写的合成镜像**没有 `/auth.cfg`**，因此设备首次启动会生成一个新的
> 随机管理员密码——从串口控制台读取。

## 基于开源构建（署名）

本项目是**衍生作品**。两个 MIT 许可的项目承担了大部分底层工作，在此、
在 [CREDITS.md](CREDITS.md) 中以及源码头部均有署名：

* **DNS 引擎** — [9M2PJU/ESP32-C3-AdBlocker](https://github.com/9M2PJU/9M2PJU-ESP32-C3-AdBlocker)
  （MIT，© 2026 zed）：拦截用 DNS 服务器、40-bit FNV-1a 哈希 blocklist
  格式、Bloom 预过滤与异步上游转发。
  原始上游为 [M-Abozaid/esp32-c3-adblock](https://github.com/M-Abozaid/esp32-c3-adblock)。
* **电子纸渲染器** — [datascale-ai/inksight](https://github.com/datascale-ai/inksight)
  （InkSight / 墨鱼，MIT，© 2026 datascale-ai）：四色面板驱动、绘图原语，
  以及 ESP32-S2 mini 引脚配置。

另使用：**PlatformIO**、**Arduino-ESP32**、**ESP-IDF**（含 lwIP）、
**esptool**、**Node.js**，以及 **StevenBlack/hosts** + **hagezi/dns-blocklists**
数据列表。完整清单（包括刻意**未**复用的部分）见 [CREDITS.md](CREDITS.md)。

## AI 构建声明

AI 编码代理在人类指导下参与了代码移植、修改、工具和文档编写。上述开源
代码仍保留原作者的版权和许可，AI 参与开发不会改变其归属。

历史真机记录包括面板行窗映射、bit-bang SPI 刷新和 UDP 接收邮箱上限，见
[详细指南](GUIDE.md#15-性能与已知上限)。当前构建检查见
[构建验证记录](docs/BUILD-VERIFICATION.md)。编译成功只说明构建产物生成成功，
面板显示、按键、配网和 DNS 连续性仍需使用对应版本在硬件上验收。

## 仓库布局

```
.
├── 3.98/merged-adblock-epd/        ← 固件项目
│   ├── src/                         固件源码
│   ├── tools/                       构建 / 字体 / 发布 / 烧写辅助脚本
│   ├── data/blocklist.bin           编译后的 blocklist 镜像
│   ├── partitions.csv               flash 分区布局
│   ├── platformio.ini               构建配置 (env: s2mini)
│   ├── README.md                    固件技术深入说明
│   └── AGENTS.md                    工程约束与不变量
├── docs/agents/                     AI-agent 工作流文档 (issue tracker, triage, domains)
├── README.md                        仓库首页 (简体中文, 本文件)
├── GUIDE.md                         详细指南 (简体中文)
├── CREDITS.md                       开源署名
├── THIRD-PARTY-NOTICES.md            第三方许可范围与待核对项
├── LICENSE                          MIT (+ 上游声明)
└── AGENTS.md                        仓库级 agent 策略
```

## 许可

本项目自身代码以 MIT 提供，见 [LICENSE](LICENSE)。两个代码上游为 MIT；其声明转载于
[CREDITS.md](CREDITS.md)。

发布前请核对 [第三方声明](THIRD-PARTY-NOTICES.md)；仓库的 MIT 许可不自动覆盖黑名单数据、中文字形和平台库。
