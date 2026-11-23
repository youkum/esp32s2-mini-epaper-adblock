# 构建验证记录

## v1.3.1 正式版记录（2026-11-23，北京时间）

目标：ESP32-S2 mini / S2FNR2，4 MB flash、2 MB PSRAM。用户确认当前设备测试正常，
将阶段日志版本纳入正式版。源码对应 tag `v1.3.1`，精确提交见同版 `release-metadata.json`。

### 本次构建与回归

应用重新编译通过；静态 RAM 为 **95,852 / 327,680（29.3%）**，应用 Flash 为
**1,030,494 / 1,179,648（87.4%）**。PlatformIO Core 6.2.0、espressif32@6.12.0、
Arduino-ESP32 2.0.17 / S2 SDK 4.4.7、Xtensa 8.4.0+2021r2-patch5、esptool 4.9.0，
LittleFS 构建包 1.203.210628。继续使用独立 `PLATFORMIO_CORE_DIR`。

- 共享 C++ 核心与真实面板驱动：33,195 项断言通过，使用 Zig 0.14.1，显式开启断言。
- 黑名单构建器：33 项通过；失败保护：3 个测试方法通过；合成脚本：5 项通过。
- 中文字形：45 字，0 缺字；没有更换字体或屏幕版式。
- `buildfs` 与合成通过；bootloader、分区表、OTA 选择数据、应用和 LittleFS 在合成镜像
  对应偏移逐字节一致，NVS 区间为擦除填充。
- esptool `image_info` 的应用 checksum 与 validation hash 均有效。
- 102 个文档本地链接与章节锚点、`git diff --check` 通过。

仅修改阶段日志开关的说明注释，保留设备上实测的状态切换日志逻辑。
本次应用 `firmware.bin` 与私有备份的实测阶段日志应用**逐字节相同**，SHA-256 也相同；
设备已有相同应用，无需重新初始化。合成镜像和 LittleFS 是本次新生成的首次部署产物，
未再次整片烧写到设备；LittleFS 只包含仓库默认 `blocklist.bin`，不含设备凭据。

### 产物摘要

|产物|字节|SHA-256|
|---|---:|---|
|`firmware.bin`|1,030,864|`c96f72adfa02150410b2c4802e1df31847f41b6aaf9f318ff7ecaea5b57a7066`|
|`littlefs.bin`|1,703,936|`a5f9481c2613e5da0202dd16a4f260d841f4548919989d2e8c91fb467812137e`|
|`firmware-merged.bin`|4,128,768|`3369980e2055f387babaf064265fda8d970db3e3edeb95af247de8bb78f210da`|
|`blocklist.bin`|647,922|`1591fe4d1c0f08f62c2de52fed50c640364eae6e670faaf0a40fc20b6e6774d0`|

### 实机与发行范围

阶段日志应用上传后与软件复位后各完成两帧约 15.35 s 的刷新，规则自检通过；
用户确认刷新正常，并于 2026-11-23 确认当前设备测试没有问题。
完整烧写、密码持久化及逐帧数据见[实机记录](HARDWARE-TEST-2026-11-21.md)。
原 v1.3.0 曾出现约 148 ms 的短 BUSY，后续两轮未复现，根因没有独立定位。

本版未新增第三方固件库。主要上游条款和来源摘要随文档包提供，调试包提供本批
应用目标文件、ELF 和 map。旧列表输入快照和 SimHei 点阵再分发依据仍待补，见
[第三方声明](../THIRD-PARTY-NOTICES.md)。用户未提供逐项数据的无线压力、TLS / OTA
故障注入及 24 / 72 h 长跑继续列在[回归清单](HARDWARE-TEST-CHECKLIST.md)。

---

## v1.3.0 历史测试版记录（2026-11-21，北京时间）

目标：ESP32-S2 mini / S2FNR2，4 MB flash、2 MB PSRAM。本版为待实机验收测试版。
对应源码见 GitHub tag `v1.3.0`，精确提交写入同版 release-metadata.json。
发布前的构建验证未连接硬件。发布后已补充
[完整烧写、启动自检与屏幕目视记录](HARDWARE-TEST-2026-11-21.md)；
无线性能、TLS、OTA 和长跑尚未验收。

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

### GitHub CI

[运行 44884049033](https://github.com/youkum/esp32s2-mini-epaper-adblock/actions/runs/44884049033)
对修复提交 `536b88ad2a85d97969d9b653da19a7e204f08137` 验证通过：Ubuntu 24.04 / Python 3.12 / Clang
原生 C++、脚本回归、字形、ESP32-S2、LittleFS、合成和产物上传均成功。
本节之后的文档补充不改变上述固件源码；发行 metadata 分别记录源码基线和 tag 提交。

### 镜像与部署检查

应用、分区表和 LittleFS 在合成镜像中的对应区段逐字节一致。
LittleFS 按实际 4096 / 256 / 1,703,936 参数解包，唯一 blocklist.bin 与输入逐字节一致；
应用 esptool image_info 的 checksum 和 validation hash 有效。
烧写辅助脚本通过 Windows PowerShell 5.1 语法检查。
指南核对了首次烧写、保留配置的 app 升级、Web OTA 文件选择、CA / 上游设置与恢复路径。
新增实机串行 DNS 探针，--help 已检查。本次后续实机启动检查仍没有发送联网 DNS 查询。

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

见 [HARDWARE-TEST-CHECKLIST.md](HARDWARE-TEST-CHECKLIST.md)。完整烧写、无凭据启动、
规则自检和屏幕显示已完成基础检查，其余结果继续标待测。
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
