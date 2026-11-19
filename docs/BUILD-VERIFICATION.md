# 构建验证记录

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
