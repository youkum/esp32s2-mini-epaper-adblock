# AGENTS.md

## 语言（强制）

**本项目的所有说明性内容一律用简体中文**：README、GUIDE、CREDITS、Release 说明、
提交信息正文、Issue 与 PR 描述、代码注释、串口/日志中面向人的文案，凡能用中文的
都用中文。仅以下内容保留英文原文，以免产生歧义或误操作：

- 信号名与引脚名（如 `EPD DC`、`EPD BUSY`、`MOSI`）、GPIO 编号；
- 协议/接口/标识符（`DNS`、`lwIP`、`PSRAM`、`mDNS`、`OTA`、`CSRF`、`Basic Auth` 等）；
- 命令、路径、文件名、代码标识符、寄存器/魔数（如 `CADB`、`HASH_BYTES`）；
- 许可证名称（`MIT`、`GPL-3.0`）与上游项目名。

本仓库不保留英文版文档（`README.en.md` / `GUIDE.en.md` 已移除），默认展示与
新增内容一律简体中文。GitHub 仓库页展示 `README.md`（中文），不要把它换回英文。

源码注释一律使用简体中文，仅术语（信号名、协议、标识符、魔数、许可证名）保留英文。

## 项目与来源

`merged-adblock-epd`：ESP32-S2 mini 上的 DNS sinkhole 与四色电子纸仪表盘。
部署和编译步骤见本目录 [README.md](README.md) 与根目录 [GUIDE.md](../../GUIDE.md)。

- DNS 基础实现来自 [9M2PJU/9M2PJU-ESP32-C3-AdBlocker](https://github.com/9M2PJU/9M2PJU-ESP32-C3-AdBlocker)，
  原始上游是 [M-Abozaid/esp32-c3-adblock](https://github.com/M-Abozaid/esp32-c3-adblock)，均为 MIT。
- 面板驱动、绘图原语和引脚配置来自 [InkSight / 墨鱼](https://github.com/datascale-ai/inksight)，MIT。
  不重新引入其后端、网站、手机 App、模式系统或云内容；面板显示本地统计。
- 黑名单、中文字形及平台组件有独立的许可范围，见根目录
  [CREDITS.md](../../CREDITS.md) 与 [THIRD-PARTY-NOTICES.md](../../THIRD-PARTY-NOTICES.md)。

## 工程约束

- 目标是 ESP32-S2FNR2，4 MB flash + 2 MB PSRAM。内部可用堆受 WiFi 和 SDK 占用影响，
  不能把标称 320 KiB RAM 当作可分配画布容量。全帧需要 105,984 字节，禁止新增全帧缓冲。
  `epdSendFrame()` 只在 12,288 字节的静态 `s_bandBuf` 中一次绘制并发送 64 行；
  `canvasBindBand()` 绑定分带并裁剪。
- 128 KiB Bloom filter 使用 `MALLOC_CAP_SPIRAM`；分配失败回退到 flash 查找，
  不用 DRAM 承接该大表。每次查询的热状态保留在内部内存。
- 面板 BUSY 等待不得阻塞 `loop()`。帧传输仍是同步的，历史约 1–1.4 s；
  此后约 15 s 的面板刷新由 `epdServiceRefresh()` 服务。发新帧前等待面板空闲，
  不要因短按 BOOT、Web 刷新或配网提示而重新初始化仍在 BUSY 的面板。
- BOOT 按键去抖和长按检测不得等待释放；短按 / Web 刷新排队，多次请求合并。
  长按进入 AP 配网会断开 STA，不承诺这期间保持局域网 DNS 服务。
- `[EPD] refresh=` 的历史正常值约 15 s；几十毫秒提示面板可能未接受更新。
  该日志不能证明画面内容正确，行窗和字体变动还需预览及真机目视确认。
- 只通过 `drawTextColor()` / `fillRectColor()` 绘制，不能直接索引 `colorBuf`；
  绘制和发送应用配套的变换，破坏其中一侧会导致镜像。
- `0x83` 行窗必须由数据行 d 推导：`d < H/2 -> 2d+1`，否则 `2(H-1-d)`。
  奇数窗口承接图像上半部分，偶数窗口倒序承接下半部分。顺序递增的窗口计数器
  会造成重复 / 镜像画面，BUSY 日志仍可能正常。
- `trackerRecord()` 在每次 DNS 查询时调用：不使用 `String`、`malloc` 或日志。
  `trackerGetTop()` 的 Top 10 结果每帧计算一次，不在 9 个分带中重复计算。
- 历史 UDP 突发上限约 6 个数据报，本次 S2 SDK 配置仍为
  `CONFIG_LWIP_UDP_RECVMBOX_SIZE = 6`。`DNS_DRAIN_MAX` 只能清空已入队报文，
  不能恢复到达时已被 lwIP 丢弃的报文。指南中的负载数字属于历史参考。
  修改预编译 lwIP 或切换 raw-PCB 路径需要独立设计和真机验证，不能用普通排空循环宣称解决上限。
- 黑名单 v1 头部、magic `CADB` 和 `HASH_BYTES = 5` 与构建器保持同步。
  更新先写临时文件，检查非空、头部和精确长度，再通过 LittleFS rename 替换旧文件。
  不能先删除旧列表；替换失败后恢复旧列表的打开状态。结构校验不验证数据出处或排序内容。
- 远程列表下载目前同步执行，可能影响 DNS；HTTPS 当前未配置 CA，不能绕过证书验证。
- WiFi 凭据按 `/wifi.cfg`、已填写的 `secrets.h`、ESP-IDF NVS 的顺序选择一组，
  不是连接失败后轮流试完三组。保留 NVS 回退，便于继承原板卡已有网络。
- `LittleFS.begin()` 必须显式传入 `LFS_PARTITION`：分区标签是 `littlefs`，
  默认的 `spiffs` 标签无法挂载它。`uploadfs` 覆盖整个文件系统，日常升级通常只上传 app。
- `connectWifi()` 开机调用，运行中掉线也要由 `loop()` 重连；当前每 30 s 尝试，
  重置无线连接后再次调用 `connectWifi()`，达到连续失败阈值后开启 portal。
  只有 `WL_CONNECTED` 表示 STA 链路可用。
- 只有累计总数持久化到 `/totals.cfg`；今日统计、每小时直方图及域名排行重启后重置。
- `src/cjk_font.cpp` 是由 `tools/gen_cjk_font.py` 生成的 16x16 点阵表。
  缺字运行时显示为空白；修改屏上文字后使用有适用许可的字体重生成，再跑缺字检查和预览。
  默认生成器使用 SimHei，其点阵再分发许可尚未确认。普通构建不需要安装字体或 Pillow。
- 中文以真实 UTF-8 保存，避免用 `\xNN` 拼接文案。生成器的源码注释也保持中文，
  不能因重新生成而把中文注释改回英文。
- 控制台是 `src/dashboard_html.h` 中的 PROGMEM 字符串，修改页面时同时更新
  `DASHBOARD_ETAG`，防止旧页面缓存。保持中文、自包含，不新增 CDN 或外部字体。
- Basic Auth 用户固定为 `admin`；`/auth.cfg` 第 1 行是密码，第 2 行是 CSRF token。
  密码每次开机通过串口打印，保留这条恢复方式。配置修改接口校验 token；
  当前二进制上传接口仅校验 Basic Auth，不能把文档写成所有写接口均有 CSRF。

## 构建与检查

本目录内执行；从仓库根调用时使用指南中的完整路径。平台固定为 `espressif32@6.12.0`。

```bash
python -m platformio run -e s2mini
python tools/test_build_blocklist.py
python tools/test_make_release.py
node tools/check_font.js
```

改字体 / 版式才执行（生成字体需要 Pillow 和合法可用的字体文件）：

```bash
python tools/gen_cjk_font.py
node tools/check_font.js
node tools/preview_dashboard.js
```

默认列表重建需要联网；生成镜像使用已安装的工具及当前数据，不自动核对许可：

```bash
python tools/build_blocklist.py data/blocklist.bin
python -m platformio run -e s2mini -t buildfs
python tools/make_release.py
```

预览脚本写本目录的 `preview-dashboard.png`，版式变动时同步维护它和 `dashboard.cpp`。
合成脚本默认要求 LittleFS 齐备，只有显式 `--no-fs` 才省略。整合镜像不能用于 Web OTA，
从 0x0 烧写会覆盖 NVS 和打包的 LittleFS；许可事项见第三方声明。

Windows 上传 / 串口捕获脚本从自身路径定位工程，仅上传 app，支持重试。原生 USB
重新枚举后应确认当前端口，必要时进入 BOOT 下载模式。`probe_flash.ps1` 和 `read_part.ps1`
是历史调试脚本，使用前适配硬编码路径。对原生程序可用重定向记录输出，避免管道影响退出状态。
Windows PowerShell 5.1 使用含中文的 `.ps1` 时保留 UTF-8 BOM，并做语法检查。

构建、脚本检查、真机结果分别报告；不把历史实测说成当前版本验收。
本次完整记录见 [BUILD-VERIFICATION.md](../../docs/BUILD-VERIFICATION.md)。

## Git

只有用户明确要求时才推送 GitHub。
