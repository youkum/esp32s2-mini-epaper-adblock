# 构建验证记录

## v1.3.0 当前记录（2026-11-21，北京时间）

目标：ESP32-S2 mini / S2FNR2，4 MB flash、2 MB PSRAM。本版为待实机验收测试版。
对应源码见 GitHub tag `v1.3.0`，精确提交写入同版 release-metadata.json。
本轮未连接硬件，不填写无线性能、面板照片或 OTA 实机验收结果。

### 固件编译

PlatformIO Core 6.2.0，平台 espressif32@6.12.0，Arduino-ESP32 2.0.17 / S2 SDK 4.4.7，
Xtensa 8.4.0+2021r2-patch5，esptool 4.9.0，mklittlefs 包 1.203.210628。
运行环境 Windows / Python 3.14.7；继续使用独立 PLATFORMIO_CORE_DIR。
不声称跨机器或跨构建日期字节一致。编译、buildfs、合成均通过。

内部静态 RAM：**95,852 / 327,680（29.3%）**；应用 Flash：**1,030,386 / 1,179,648（87.3%）**。

未改变分区：两个 1.125 MiB OTA slot，1.625 MiB LittleFS。没有全帧缓冲，
像素仍只用 12,288 字节 band。新 pending 正文优先 PSRAM，失败降为内部 4 槽。
静态 RAM 不是运行期最小 free heap；下载任务 / TLS 峰值需实机测量。

### 算法与脚本回归

- C++ 原生回归：**33,195 个断言**通过，Zig 0.14.1 的 C++ 前端，显式 -UNDEBUG。
  直接编译共享 TopN / Bloom / DNS / HTTP 核心和真实 epd_driver.cpp。
  覆盖新热点替换、长域名、32,000 组 Bloom 位点、TTL 递减 / 到期 / 回绕 / 容量、
  CNAME 链、异常 DNS、HTTP 封帧 / 截断 / gzip / 跳转 / 空间不足、行窗及像素字节、
  初始化 / 刷新 / 断电 BUSY 卡死、未观察到 BUSY、整帧超时和有限重试。
- 原有构建器：33 项通过；失败保护回归：3 个测试方法（含 6 个失败场景）通过。
- 打包脚本：5 个 unittest 通过。
- 字形：45 字，0 缺字；未更换字体或声称再分发依据完备。
- 控制台 JavaScript 语法、工作流 YAML 解析、git diff --check 通过。
- 原生测试不能验证真实 GPIO 电气时序、TLS 握手、Web multipart 与 ESP-IDF OTA / 掉电行为。

### 镜像与部署检查

应用、分区表和 LittleFS 在合成镜像中的对应区段逐字节一致。
LittleFS 按实际 4096 / 256 / 1,703,936 参数解包，唯一 blocklist.bin 与输入逐字节一致；
应用 esptool image_info 的 checksum 和 validation hash 有效。
烧写辅助脚本通过 Windows PowerShell 5.1 语法检查。
指南核对了首次烧写、保留配置的 app 升级、Web OTA 文件选择、CA / 上游设置与恢复路径。
新增实机串行 DNS 探针，--help 已检查，本轮没有向真实设备发送查询。

|产物|字节|SHA-256|
|---|---:|---|
|`firmware.bin`|1,030,752|`bd97df61902171257605c35be2b3a154c9f004d5dbafbd80a495203dc7a591e6`|
|`littlefs.bin`|1,703,936|`69a29cb41a0ba682292cc872b4ad58b6ac3e74f6cff5fc88a26409142390f139`|
|`firmware-merged.bin`|4,128,768|`48efc1924331bd5a005be4ba54655dbe2a57aaa0362e473abdd6b6b822ee2622`|
|`blocklist.bin`|647,922|`1591fe4d1c0f08f62c2de52fed50c640364eae6e670faaf0a40fc20b6e6774d0`|

### 开源引用检查

未引入新的第三方固件库；OTA / TLS / CA / SHA-256 使用 SDK 已有组件。
新增算法 / 协议参考和仅用于本机测试的 Zig 范围说明。文档包沿用主要上游许可全文。
旧黑名单输入快照与 SimHei 点阵的再分发依据仍待补，不能把编译 / 发布当作许可完备证明。

### 实机待办

见 [HARDWARE-TEST-CHECKLIST.md](HARDWARE-TEST-CHECKLIST.md)，全部结果标待测。
特别验证新行间间隔、下载 / 刷新并发 DNS、WiFi 断线重绑、证书失败、OTA 失败不重启及长跑。

---

## v1.2.0 历史记录


验证日期：2026-11-19（北京时间）。目标：`s2mini`，ESP32-S2 mini / 4 MB flash / 2 MB PSRAM。
固件源码基线为 `b2daa32eb3cbe3d1dce43744266e34c20b7d8976`；
`ea1cace3319cc88014f7df265f0e10d3e22da500` 将平台固定为 `espressif32@6.12.0`，
修正合成与烧写辅助工具，并更新指导文档。v1.2.0 发布补充发行说明和下载入口，
未修改该基线的 `src/`，使用下表已验证的同一批二进制。

## 实际工具版本

|工具 / 包|本次版本|
|---|---|
|系统|Windows，PowerShell 7.6.6；烧写脚本另通过 Windows PowerShell 5.1 语法检查|
|Python|3.14.7|
|PlatformIO Core|6.2.0|
|platform-espressif32|6.12.0（项目配置固定）|
|framework-arduinoespressif32|3.20017.241212+sha.dcc1105b，对应 Arduino-ESP32 2.0.17|
|S2 SDK|ESP-IDF 4.4.7（随 Arduino framework 提供，版本头部确认）|
|toolchain-xtensa-esp32s2|8.4.0+2021r2-patch5|
|toolchain-riscv32-esp|8.4.0+2021r2-patch5（PlatformIO 安装的依赖，不是 S2 主编译器）|
|tool-esptoolpy|2.40900.250804，对应 esptool 4.9.0|
|tool-mklittlefs|1.203.210628，对应工具版本 2.3|
|Node.js|24.21.0（只用于字形检查）|

`platformio.ini` 固定平台版本，但平台仍对部分工具包使用兼容版本范围；上表是本次实际
解析的包版本，不声称跨时间、跨机器会得到字节一致的产物。构建时间和环境信息也可能影响镜像。
本次 `PLATFORMIO_CORE_DIR` 使用较短的独立目录，避免 Windows 依赖解压路径过长。

## 已完成检查

以下命令从仓库根目录运行；`PLATFORMIO_CORE_DIR` 保持一致：

```bash
python -m platformio run -d 3.98/merged-adblock-epd -e s2mini
python -m platformio run -d 3.98/merged-adblock-epd -e s2mini -t buildfs
python 3.98/merged-adblock-epd/tools/make_release.py
python 3.98/merged-adblock-epd/tools/test_build_blocklist.py
python 3.98/merged-adblock-epd/tools/test_make_release.py
node 3.98/merged-adblock-epd/tools/check_font.js
git diff --check
```

- 固件构建与 LittleFS 构建均为 `SUCCESS`。
- 静态 RAM：82,588 / 327,680 字节（25.2%）；Flash：1,033,694 / 1,179,648 字节（87.6%）。
  这是链接器统计，应用二进制文件含镜像头和校验，实际文件大小见下表。
- 黑名单构建器检查：33 项通过；合成脚本检查：5 项通过，覆盖分区偏移、
  缺少文件系统、显式 `--no-fs`、缺少应用程序及应用超过分区容量。
- 中文字形表：45 字，缺字 0；本次未修改屏幕文案或版式，没有重生成字体或预览。
- 合成镜像内的 partition table、OTA 选择数据、应用程序和 LittleFS，与输入产物在
  指定偏移逐字节一致。NVS 范围 `0x9000..0xE000` 是 `0xFF` 填充，因此整合烧写会覆盖旧配置。
- esptool `image_info` 确认应用镜像的 checksum 与 validation hash 有效。
- Windows 烧写脚本通过 PowerShell 7 和 5.1 语法检查；文档中的串口监控参数已与 CLI 帮助核对。
  这不代表已连接设备实际运行上传或抓串口。
- 73 个文档本地文件链接与章节锚点已检查；主要开源许可证链接已读取核对。

## 本地产物

|文件|字节数|SHA-256|
|---|---:|---|
|`.pio/build/s2mini/firmware.bin`|1,034,064|`5a11a9b3e7b6104243cdc86feab1af01f444cd4e8acb9cffc65d8e1af6fc1376`|
|`.pio/build/s2mini/littlefs.bin`|1,703,936|`32897b874ad24998f62435fc8acce5391c4fff390363e175139323dc9a59e5e9`|
|`firmware-merged.bin`|4,128,768|`5bd1ca66a3fa33aa294951b8112884164a4bb3d507ff6bb7408912b5473b958e`|
|`data/blocklist.bin`（旧列表，未重建）|647,922|`1591fe4d1c0f08f62c2de52fed50c640364eae6e670faaf0a40fc20b6e6774d0`|

路径相对于 `3.98/merged-adblock-epd/`。构建产物由 `.gitignore` 排除，作为
[v1.2.0 Release 附件](https://github.com/youkum/esp32s2-mini-epaper-adblock/releases/tag/v1.2.0)提供；
应用程序、合成镜像、文件系统、列表、分区烧写包、调试包、校验和及文档包均在附件中。
历史 GitHub Release 不随源码推送自动更新。SHA-256 仅用于识别本次产物，不作为跨机器重建的硬性预期。

## 尚未验收的范围

本次没有在板上烧写和运行。DNS UDP/TCP、面板显示、长短按 BOOT、首次配网、
掉线重连、Web OTA，以及黑名单中断 / 截断 / 空间不足时的旧列表保留，均需真机回归。
需重新测量刷新期间和自动下载期间的 DNS 延迟、丢包与面板帧耗时；指南中的旧数据只是参考。

此次发布保留原列表和字形；发布动作不构成旧黑名单输入快照或 SimHei 点阵许可的证明。源码、数据、字体和平台组件
的许可范围及发行待办见[第三方声明](../THIRD-PARTY-NOTICES.md)，部署命令见[指南](../GUIDE.md)。
