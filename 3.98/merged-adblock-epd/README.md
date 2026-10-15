# merged-adblock-epd 固件

ESP32-S2 mini 上的 **DNS sinkhole + 四色电子纸仪表盘**固件。由两个 MIT 许可的
上游项目合并而成：DNS 引擎来自
[9M2PJU/ESP32-C3-AdBlocker](https://github.com/9M2PJU/ESP32-C3-AdBlocker)，
电子纸驱动与绘图原语来自 [datascale-ai/inksight](https://github.com/datascale-ai/inksight)。

> 本目录是固件源码。项目总览见仓库根的 [README.md](../../README.md)；
> 完整的构建、烧写、配置与故障排查教程见 [GUIDE.md](../../GUIDE.md)。

## 目录结构

* `src/` — 固件源码（DNS 引擎、电子纸驱动、Web 控制台）
* `tools/` — 构建 / 字体 / 发布 / 烧写辅助脚本
* `data/blocklist.bin` — 编译后的 blocklist 镜像
* `partitions.csv` — flash 分区布局（两个 1.125 MB OTA slot + 1.625 MB LittleFS）
* `platformio.ini` — 构建配置（env: `s2mini`）
* `AGENTS.md` — 工程约束与不变量

## 构建与烧写

在本目录内运行（或从仓库根用 `python -m platformio run -d 3.98/merged-adblock-epd`）：

```bash
pio run                  # 构建
pio run -t upload        # 烧写应用程序
pio run -t uploadfs      # 烧写文件系统（LittleFS，含 blocklist）
pio run -t buildfs       # 构建 LittleFS 镜像（合成发行镜像前必须先跑）
python tools/make_release.py   # 合成 firmware-merged.bin（含 LittleFS，从 0x0 烧写）
```

合成镜像用 esptool 从偏移 `0x0` 烧写：

```bash
esptool.py --chip esp32s2 write_flash 0x0 firmware-merged.bin
```

## blocklist

```bash
python tools/build_blocklist.py data/blocklist.bin
```

格式（magic `CADB`、`HASH_BYTES = 5`、flash 中的 40-bit FNV-1a 哈希 + RAM
Bloom 预过滤）与上游项目**字节兼容**。构建好的镜像通过
`pio run -t uploadfs` 烧写，或经 Web 控制台上传 / 远程 URL 自动更新。

## 关键工程约束（摘录）

* **没有全帧缓冲**：768×552 @ 2 bpp 约 106 KB，放不进与 WiFi 共享的内部
  DRAM；画面按 **64 行 band** 流式渲染进 12,288 字节静态缓冲。
* **绝不阻塞 `loop()`**：帧传输约 1.1 s，其后面板刷新约 15 s 由
  `epdServiceRefresh()` 在后台完成；`[EPD]` 日志行的 `refresh=` 字段是面板
  健康检查（~15 s = 真刷新，几毫秒 = 更新未被接受）。
* **只用 `drawTextColor()` / `fillRectColor()` 绘制**，每行的 `0x83` window
  必须由其**数据行**推导，绝不用运行计数器。
* **UDP DNS 有约 6 个数据报的 burst ceiling**（lwIP 预编译库的
  `CONFIG_LWIP_UDP_RECVMBOX_SIZE = 6`），已实测、已接受，不是 bug。

完整规则与实测推理见 [`AGENTS.md`](AGENTS.md)，通俗版见
[详细指南的架构说明](../../GUIDE.md#17-架构说明)与
[性能与已知上限](../../GUIDE.md#15-性能与已知上限)。
