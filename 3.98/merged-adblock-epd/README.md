# merged-adblock-epd 固件

ESP32-S2 mini 上的 **DNS sinkhole + 四色电子纸仪表盘**固件。由两个 MIT 许可的
上游项目合并而成：DNS 引擎来自
[9M2PJU/ESP32-C3-AdBlocker](https://github.com/9M2PJU/9M2PJU-ESP32-C3-AdBlocker)，
电子纸驱动与绘图原语来自 [datascale-ai/inksight](https://github.com/datascale-ai/inksight)。

> 本目录是固件源码。项目总览见仓库根的 [README.md](../../README.md)；
> 完整的构建、烧写、配置与故障排查教程见 [GUIDE.md](../../GUIDE.md)。

## 目录结构

* `src/` — 固件源码（DNS 引擎、电子纸驱动、Web 控制台）
* `tools/` — 构建 / 字体 / 发布 / 烧写辅助脚本
* `data/blocklist.bin` — 编译后的 blocklist 镜像
* `partitions.csv` — flash 分区布局（两个 1.125 MiB OTA slot + 1.625 MiB LittleFS）
* `platformio.ini` — 构建配置（env: `s2mini`）
* `AGENTS.md` — 工程约束与不变量

## 构建与烧写

以下命令在本目录内执行；项目固定 `espressif32@6.12.0`，本次使用 PlatformIO Core 6.2.0。
从仓库根调用时加 `-d 3.98/merged-adblock-epd`。完整结果见
[构建验证记录](../../docs/BUILD-VERIFICATION.md)。

```bash
python -m pip install "platformio==6.2.0"
python -m platformio run -e s2mini
python -m platformio run -e s2mini -t upload --upload-port COM3
# 仅首次部署或需要覆盖全部配置时执行：
python -m platformio run -e s2mini -t uploadfs --upload-port COM3
```

`COM3` 替换为实际端口。已有设备升级只上传应用程序；`uploadfs` 会覆盖
WiFi、密码、自定义规则、封禁和累计统计。Web OTA 使用 `firmware.bin`。
合成镜像会覆盖 NVS 和打包的 LittleFS，适用于首次部署 / 重新初始化：

```bash
python -m platformio run -e s2mini -t buildfs
python tools/make_release.py
python -m pip install "esptool==4.9.0"
python -m esptool --chip esp32s2 --port COM3 write_flash 0x0 firmware-merged.bin
```

## blocklist

```bash
python tools/build_blocklist.py data/blocklist.bin
```

格式（magic `CADB`、`HASH_BYTES = 5`、flash 中的 40-bit FNV-1a 哈希 + RAM
Bloom 预过滤）与上游 v1 格式兼容，导入还受文件长度、非空和空间限制。
已有设备优先经 Web 控制台上传列表，保留其他配置；首次部署可以 `uploadfs`。
默认重建会联网下载 StevenBlack + HaGeZi；许可、旧二进制出处与字体待核对项见
[第三方声明](../../THIRD-PARTY-NOTICES.md)。HTTPS 在控制台配置可信 PEM CA 并完成 NTP
对时后使用；后台下载 / 主备 DNS / 缓存说明见根目录指南第 18 节。

## 关键工程约束（摘录）

* **没有全帧缓冲**：768×552 @ 2 bpp 约 106 KB，放不进与 WiFi 共享的内部
  DRAM；画面按 **64 行 band** 流式渲染进 12,288 字节静态缓冲。
* **面板全过程状态机**：复位、上电、每次两行发送、刷新、断电均由
  `epdServiceRefresh()` 驱动。阶段 BUSY 45 s / 单次整帧 120 s 上限，失败最多再试两次。
  每帧固定数据快照，仍只保留一个 64 行静态像素分带。
* **只用 `drawTextColor()` / `fillRectColor()` 绘制**，每行的 `0x83` window
  必须由其**数据行**推导，绝不用运行计数器。
* **UDP DNS 有约 6 个数据报的 burst ceiling**（lwIP 预编译库的
  `CONFIG_LWIP_UDP_RECVMBOX_SIZE = 6`），属于历史实测；本次只确认 SDK 配置值，未复测吞吐。

完整规则与实测推理见 [`AGENTS.md`](AGENTS.md)，通俗版见
[详细指南的架构说明](../../GUIDE.md#17-架构说明)与
[性能与已知上限](../../GUIDE.md#15-性能与已知上限)。

## v1.3.1 回归入口

```bash
python tools/test_native.py
python tools/test_regressions.py
python tools/test_build_blocklist.py
python tools/test_make_release.py
node tools/check_font.js
```

原生测试需 g++ / clang++，或 `--compiler C:/工具路径/zig.exe`。测试显式开启断言，
不能用 `NDEBUG` 空断言制造通过。新状态、配置及接口见根目录指南第 18 节，
实机记录和后续回归项目见[验收清单](../../docs/HARDWARE-TEST-CHECKLIST.md)。
正式版及升级方式见[更新说明](../../docs/releases/v1.3.1.md)。
