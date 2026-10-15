# 开源署名（Credits）

本项目是**衍生作品**。没有下面这些项目它就不会存在；每一项都在此署名，
而不是埋在源码注释里。所有上游组件均以 **MIT** 许可；其许可证全文在
本文件末尾逐字转载。

若你是上游作者、希望修改这里的措辞，请开一个 issue——署名更正随时欢迎。

---

## 1. DNS 引擎 — 9M2PJU / ESP32-C3-AdBlocker

* **仓库**： <https://github.com/9M2PJU/ESP32-C3-AdBlocker>
* **许可证**： MIT — Copyright (c) 2026 zed
* **本项目采用了**：
  * 整个拦截用 DNS 服务器（端口 53，UDP + TCP 双栈）。
  * 40-bit FNV-1a 哈希 blocklist 格式：flash 中的排序哈希、magic
    `CADB`、`HASH_BYTES = 5`、二分查找，以及 RAM 中的 Bloom 预过滤。
  * 异步上游转发设计（改写查询 ID、维护 pending 表、把应答匹配回客户端）。
  * `tools/build_blocklist.py` 与 `tools/test_build_blocklist.py`，与该格式
    保持字节兼容。
  * Web 控制台的大部分交互模型（Basic Auth、CSRF token、ban/unban、
    自定义域名列表、远程 blocklist URL）。

blocklist 的**文件格式刻意与上游项目保持完全一致**，为任一固件构建的
`blocklist.bin` 都可以直接装入另一个。

## 2. 电子纸渲染器 — InkSight（墨鱼）

* **仓库**： <https://github.com/datascale-ai/inksight>
* **项目主页**： <https://www.inksight.site/>
* **许可证**： MIT — Copyright (c) 2026 datascale-ai
* **本项目采用了**：
  * JDY79668 / GDEY075Z08 class 的 7.5 英寸四色电子纸驱动
    （`epd_driver.cpp` / `epd_driver.h`），含面板专属的上电时序、
    `0x83` 行窗交错与 bit-bang SPI 传输。
  * 绘图原语与两个相互抵消的旋转变换（`display.cpp` / `display.h`）。
  * ESP32-S2 mini 引脚配置。
  * 生成式中文字形表方案（`cjk_font.cpp` + `tools/gen_cjk_font.py`）。

**刻意*没有*采用的部分**：InkSight 的云后端、Next.js 网站、手机 App、
24 个内置「模式」、LLM 内容管线与在线烧写流程。本固件是自成一体的
sinkhole，从不与 `inksight.site` 或任何远程服务通信。长期规则见
`AGENTS.md`。

## 3. blocklist 数据来源

默认的 `blocklist.bin` 由以下社区列表构建。它们是**数据**而非代码，各自
携带自己的许可（两者均较宽松；再分发派生列表前请查看相应仓库的最新条款）：

* **StevenBlack/hosts** — <https://github.com/StevenBlack/hosts>
  （默认 `hosts` 列表，MIT）。
* **hagezi/dns-blocklists** — <https://github.com/hagezi/dns-blocklists>
  （`light-onlydomains.txt`，列表数据为 GPL-3.0）。

随固件附带的 `data/blocklist.bin` 是两者并集编译成的哈希表
（约 103,000 个域名），不是任一文件的拷贝。

## 4. 平台与工具链（由 PlatformIO 拉取，未随仓库分发）

* **PlatformIO** — <https://platformio.org/>（Apache-2.0）
* **Arduino-ESP32 core**（Espressif）— <https://github.com/espressif/arduino-esp32>
  （LGPL-2.1）
* **ESP-IDF**（Espressif）— <https://github.com/espressif/esp-idf>
  （Apache-2.0），含预编译的 **lwIP** 协议栈
  （`CONFIG_LWIP_UDP_RECVMBOX_SIZE = 6` — burst ceiling 说明见 `AGENTS.md`）。
* **esptool** — <https://github.com/espressif/esptool>（GPL-2.0）
* **SimHei**（`simhei.ttf`）— `tools/gen_cjk_font.py` **离线**栅格化字形所用的
  Windows 系统字体。仓库只提交生成的 16×16 点阵，不随仓库再分发字体文件。
* **Node.js** — `tools/*.js` 辅助脚本使用（MIT）。

---

## 许可证全文

### 9M2PJU/ESP32-C3-AdBlocker

```text
MIT License

Copyright (c) 2026 zed

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### datascale-ai/inksight (InkSight / 墨鱼)

```text
MIT License

Copyright (c) 2026 datascale-ai

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
