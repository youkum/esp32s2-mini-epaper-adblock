# 第三方声明

本项目根目录的 [LICENSE](LICENSE) 适用于项目自身以 MIT 提供的代码，不替代第三方版权声明、许可文本或发行义务。代码来源与原有 MIT 全文见 [CREDITS.md](CREDITS.md)。以下提交定位的是本次核对的许可证，**不是**历史移植所用的确切提交。

核对日期：2026-11-19。表中五份许可证链接均已读取核对；不代表已审计上游所有文件，
也不代表旧发行物的许可事项已完成。历史移植版本和旧数据来源缺少记录时，在此保留
不确定性，不把本次查到的上游提交写成历史来源。

|组件|本次核对的上游条款|范围|
|---|---|---|
|9M2PJU/9M2PJU-ESP32-C3-AdBlocker|[MIT，Copyright (c) 2026 zed](https://github.com/9M2PJU/9M2PJU-ESP32-C3-AdBlocker/blob/f0b03f6765d8af6d942da96387c5b49c3bb3c03c/LICENSE)|DNS、黑名单格式及相关工具、控制台交互|
|M-Abozaid/esp32-c3-adblock|[MIT，Copyright (c) 2026 zed](https://github.com/M-Abozaid/esp32-c3-adblock/blob/e3bd0be43f5f4c23b5c6490f259806f8e897d90b/LICENSE)|DNS 引擎的原始上游|
|datascale-ai/inksight|[MIT](https://github.com/datascale-ai/inksight/blob/ea73138e8f69b8632d39295afeab32c3560d92c4/LICENSE)|电子纸驱动、绘图代码及板型配置|
|StevenBlack/hosts|[MIT 声明及归属](https://github.com/StevenBlack/hosts/blob/ce25ce24d6dca08fa484612091a822bc93587086/license.txt)|构建器默认 `hosts` 输入；还需核对聚合列表所列来源|
|hagezi/dns-blocklists|[GPL-3.0](https://github.com/hagezi/dns-blocklists/blob/746b4541882a983ad465f41e1a7993a8e66a2b72/LICENSE)|构建器默认 `light-onlydomains.txt` 输入；不应当成 MIT 数据|

## 固件中的平台组件

实际构建使用 `platform-espressif32 6.12.0`、Arduino-ESP32 2.0.17 和其 S2 SDK 4.4.7，
完整包版本见[构建验证记录](docs/BUILD-VERIFICATION.md)。主要条款入口如下；这些不是
固件全部传递依赖的许可证清单：

|组件|条款入口|分发范围|
|---|---|---|
|Arduino-ESP32 2.0.17|[LGPL 文本](https://github.com/espressif/arduino-esp32/blob/2.0.17/LICENSE.md)，并保留文件级声明|静态链接的 core 和本项目用到的库；具体版本的源码、许可及 LGPL 适用的修改 / 重新链接要求需随发行处理|
|ESP-IDF 4.4.7|[主体 Apache-2.0](https://github.com/espressif/esp-idf/blob/v4.4.7/LICENSE)，[组件目录](https://github.com/espressif/esp-idf/tree/v4.4.7/components)|SDK、bootloader 及其组件；SDK 中的第三方目录、预编译库和 ROM 功能各有适用条款|
|lwIP|[上游 BSD-3-Clause](https://github.com/lwip-tcpip/lwip/blob/STABLE-2_1_3_RELEASE/COPYING)|随 SDK 链接到固件的网络栈；该链接是条款入口，不是本项目实际 Espressif fork 的版本证明|

PlatformIO Core、平台构建脚本、esptool、Node.js 和 Pillow 主要用于构建或上传，
使用工具不等于把整个工具打进固件；如果另行打包分发工具，也需附它们及其依赖的条款。

## 已确认与待补充项

两个代码上游的 MIT 版权及许可全文已在 [CREDITS.md](CREDITS.md) 保留，并说明原始 DNS 上游。
以下事项影响当前仓库里的生成资产以及新二进制发行，并非只有点击 GitHub Release 时才相关：

- **黑名单二进制。** 现有 `data/blocklist.bin` 的 SHA-256 为 `1591fe4d1c0f08f62c2de52fed50c640364eae6e670faaf0a40fc20b6e6774d0`，提交历史未给出准确的输入快照。FNV 哈希不是归属或许可豁免。新发布建议先用可再分发、固定版本的输入重建，并记录来源、版本、输入/输出的 SHA-256、生成脚本及命令；如保留 GPL 数据，按其适用条款提供相应许可、版权及可修改数据来源。没有完成核对前不要把旧列表打进新发行物。
- **中文字形。** `src/cjk_font.cpp` 和预览图包含从 SimHei 提取的点阵。没有提交 TTF 并不证明位图可再分发；微软字体条款未在仓库中得到确认。新发行前应确认适用许可，或换成允许再分发的字体、重新生成点阵和预览并附上原文。InkSight 的 MIT 不涵盖外部字体。
- **固件与工具链。** 源码仓库没有随附 PlatformIO、esptool 或 Node.js 本身；固件和合成镜像可能包含 Arduino-ESP32、ESP-IDF、lwIP、bootloader 等平台组件。以实际打包版本附带的 `LICENSE`、`NOTICE` 和 SDK 条款为准，保存固件对应的仓库提交、PlatformIO 平台及包版本，并处理实际适用的源码或重新链接要求。不要只附根目录的 MIT 就发合成镜像。

## 新列表和发行物如何留存依据

重新构建列表时使用固定提交 URL 或本地输入快照，保存原始输入、各来源许可与归属、
输入 / 输出 SHA-256、构建器版本和完整命令。只有哈希二进制无法代替可修改的输入。
GPL 数据与 MIT 代码应分别标明条款；不能因数据被哈希就删去 GPL 归属，也不能仅凭
同一合成镜像就把项目全部代码重新标成 GPL。

字体更换时记录字体项目、版本、原文许可、输入文件校验和和生成命令，重新生成
`cjk_font.cpp`、运行缺字检查并更新预览。不能只把声明中的 SimHei 改成另一字体名。
公开发布镜像时附该版本的源码 / 构建信息、所包含组件的许可材料以及相应数据来源。
v1.2.0 发布保留当前生成资产，并附本声明、上游条款文本和构建版本信息；
编译、合成验证和发布动作没有补足上文旧列表、SimHei 点阵的来源与再分发依据。
