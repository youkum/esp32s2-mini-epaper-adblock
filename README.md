# esp32s2-mini-epaper-adblock

**简体中文**

把一块 **ESP32-S2 mini** 变成自成一体的 **DNS 广告拦截器**，统计信息绘制在
**7.5 英寸四色电子纸「魔法屏」**（768×552，JDY79668 / GDEY075Z08 class）上。
没有云、没有手机 App、没有账号：一块主板、一块屏、一根电源线，让整个网络
不再显示广告。

面板每 3 分钟刷新一次，显示设备自己拦截到的数据——今日拦截数、拦截率、
每小时直方图与 Top 域名；与此同时 DNS 引擎在后台持续应答查询。其余功能
（按客户端的统计、自定义规则、封禁列表、blocklist 更新、OTA、密码）都在
固件自带的中文 Web 控制台里。

> **这是一个纯 AI 构建的项目。** 整个代码库——固件、电子纸驱动、Web 控制台、
> 构建工具与文档——均由 AI 编码代理在人类指导下端到端编写，并在每一步都用
> 真实硬件验证。详见下方的 [AI 构建声明](#ai-构建声明)。同时它建立在两个
> MIT 许可的开源项目之上，详见 [开源署名](#基于开源构建署名)。

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
  PSRAM 中的 128 KB Bloom filter 预过滤。其格式（magic `CADB`、
  `HASH_BYTES = 5`）与上游
  [9M2PJU/ESP32-C3-AdBlocker](https://github.com/9M2PJU/ESP32-C3-AdBlocker)
  项目**字节兼容**。随固件附带的列表约 **103,000** 个域名。
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
# 1. 构建 (PlatformIO)
python -m pip install --upgrade platformio
python -m platformio run -d 3.98/merged-adblock-epd

# 2. 烧写 app + 文件系统
python -m platformio run -d 3.98/merged-adblock-epd -t upload
python -m platformio run -d 3.98/merged-adblock-epd -t uploadfs

# 3. 或者合成单个镜像，从 0x0 烧写
python -m platformio run -d 3.98/merged-adblock-epd -t buildfs
python 3.98/merged-adblock-epd/tools/make_release.py
esptool.py --chip esp32s2 write_flash 0x0 3.98/merged-adblock-epd/firmware-merged.bin
```

以 **115200** 波特率打开串口控制台读取管理员密码（每次启动都会打印）。
然后浏览器访问 `http://adblock.local/`（或面板上显示的 IP），
用 **`admin`** 登录。

**完整教程 → [GUIDE.md](GUIDE.md)。**

## 下载 / 发行版

预编译的**合成固件镜像**（`firmware-merged.bin`，从偏移 `0x0` 烧写）附在
[**Releases**](https://github.com/youkum/esp32s2-mini-epaper-adblock/releases)
页面。合成镜像打包了 bootloader + partition table + 应用程序 + LittleFS
（含 blocklist），所以一个文件就能得到一台真正能拦截的设备。

> 全新烧写的合成镜像**没有 `/auth.cfg`**，因此设备首次启动会生成一个新的
> 随机管理员密码——从串口控制台读取。

## 基于开源构建（署名）

本项目是**衍生作品**。两个 MIT 许可的项目承担了大部分底层工作，在此、
在 [CREDITS.md](CREDITS.md) 中以及源码头部均有署名：

* **DNS 引擎** — [9M2PJU/ESP32-C3-AdBlocker](https://github.com/9M2PJU/ESP32-C3-AdBlocker)
  （MIT，© 2026 zed）：拦截用 DNS 服务器、40-bit FNV-1a 哈希 blocklist
  格式、Bloom 预过滤与异步上游转发。
* **电子纸渲染器** — [datascale-ai/inksight](https://github.com/datascale-ai/inksight)
  （InkSight / 墨鱼，MIT，© 2026 datascale-ai）：四色面板驱动、绘图原语，
  以及 ESP32-S2 mini 引脚配置。

另使用：**PlatformIO**、**Arduino-ESP32**、**ESP-IDF**（含 lwIP）、
**esptool**、**Node.js**，以及 **StevenBlack/hosts** + **hagezi/dns-blocklists**
数据列表。完整清单（包括刻意**未**复用的部分）见 [CREDITS.md](CREDITS.md)。

## AI 构建声明

**这是一个纯 AI 项目。** 本仓库中的每一行固件、驱动、Web 控制台、构建脚本
与文档，都由 AI 编码代理（Trae / Claude 类模型）在人类指令下生成。这里没有
一段"人类为这台设备写好、再交给 AI"的既有代码：代理先阅读两个上游项目，
设计合并方案，编写代码，然后构建、烧写，并在真实的 ESP32-S2 mini 与面板上
逐项验证。

这意味着：

* **验证是实测的，不是假定的。** 合并的原则是：没在硬件上看到，就不算通过。
  几个只有靠测量（而非读代码）才能发现的例子：一条硬件 SPI 路径看似快了
  35×，实际却什么都没刷新；一个 band-window 计数错误把画面撕成两半，而日志
  仍报告刷新正常；以及 [`AGENTS.md`](3.98/merged-adblock-epd/AGENTS.md) 中
  记录的 UDP 接收邮箱上限。
* **已知限制是写下来的，不是藏起来的。** 见
  [AGENTS.md](3.98/merged-adblock-epd/AGENTS.md) 的 "burst ceiling" 一节，
  以及[详细指南](GUIDE.md#15-性能与已知上限)的性能一节。
* **信任之前请自行审阅。** 它会驱动一台接入你网络的、市电供电的设备。
  请阅读源码；代理的约束与推理都记录在
  [`AGENTS.md`](3.98/merged-adblock-epd/AGENTS.md) 中。

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
├── LICENSE                          MIT (+ 上游声明)
└── AGENTS.md                        仓库级 agent 策略
```

## 许可

MIT — 见 [LICENSE](LICENSE)。上游组件同样是 MIT；其声明转载于
[CREDITS.md](CREDITS.md)。
