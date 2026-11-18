# 第三方声明

本项目根目录的 [LICENSE](LICENSE) 适用于项目自身以 MIT 提供的代码，不替代第三方版权声明、许可文本或发行义务。代码来源与原有 MIT 全文见 [CREDITS.md](CREDITS.md)。以下提交定位的是本次核对的许可证，**不是**历史移植所用的确切提交。

|组件|本次核对的上游条款|范围|
|---|---|---|
|9M2PJU/9M2PJU-ESP32-C3-AdBlocker|[MIT，Copyright (c) 2026 zed](https://github.com/9M2PJU/9M2PJU-ESP32-C3-AdBlocker/blob/f0b03f6765d8af6d942da96387c5b49c3bb3c03c/LICENSE)|DNS、黑名单格式及相关工具、控制台交互|
|M-Abozaid/esp32-c3-adblock|[MIT，Copyright (c) 2026 zed](https://github.com/M-Abozaid/esp32-c3-adblock/blob/e3bd0be43f5f4c23b5c6490f259806f8e897d90b/LICENSE)|DNS 引擎的原始上游|
|datascale-ai/inksight|[MIT](https://github.com/datascale-ai/inksight/blob/ea73138e8f69b8632d39295afeab32c3560d92c4/LICENSE)|电子纸驱动、绘图代码及板型配置|
|StevenBlack/hosts|[MIT 声明及归属](https://github.com/StevenBlack/hosts/blob/ce25ce24d6dca08fa484612091a822bc93587086/license.txt)|构建器默认 `hosts` 输入；还需核对聚合列表所列来源|
|hagezi/dns-blocklists|[GPL-3.0](https://github.com/hagezi/dns-blocklists/blob/746b4541882a983ad465f41e1a7993a8e66a2b72/LICENSE)|构建器默认 `light-onlydomains.txt` 输入；不应当成 MIT 数据|

## 发布前必须核对

- **黑名单二进制。** 现有 `data/blocklist.bin` 的 SHA-256 为 `1591fe4d1c0f08f62c2de52fed50c640364eae6e670faaf0a40fc20b6e6774d0`，提交历史未给出准确的输入快照。FNV 哈希不是归属或许可豁免。新发布建议先用可再分发、固定版本的输入重建，并记录来源、版本、输入/输出的 SHA-256、生成脚本及命令；如保留 GPL 数据，按其适用条款提供相应许可、版权及可修改数据来源。没有完成核对前不要把旧列表打进新发行物。
- **中文字形。** `src/cjk_font.cpp` 和预览图包含从 SimHei 提取的点阵。没有提交 TTF 并不证明位图可再分发；微软字体条款未在仓库中得到确认。新发行前应确认适用许可，或换成允许再分发的字体、重新生成点阵和预览并附上原文。InkSight 的 MIT 不涵盖外部字体。
- **固件与工具链。** 源码仓库没有随附 PlatformIO、esptool 或 Node.js 本身；固件和合成镜像可能包含 Arduino-ESP32、ESP-IDF、lwIP、bootloader 等平台组件。以实际打包版本附带的 `LICENSE`、`NOTICE` 和 SDK 条款为准，保存固件对应的仓库提交、PlatformIO 平台及包版本，并处理实际适用的源码或重新链接要求。不要只附根目录的 MIT 就发合成镜像。
