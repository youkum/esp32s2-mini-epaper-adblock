# 开源署名（Credits）

本项目是**衍生作品**。没有下面这些项目它就不会存在；每一项都在此署名，
而不是埋在源码注释里。以下两个代码上游以 **MIT** 许可，对应声明见文末。
列表、字体、平台组件和固件发布的单独要求见 [第三方声明](THIRD-PARTY-NOTICES.md)。

若你是上游作者、希望修改这里的措辞，请开一个 issue——署名更正随时欢迎。

---

## 1. DNS 引擎 — 9M2PJU / ESP32-C3-AdBlocker

* **仓库**： <https://github.com/9M2PJU/9M2PJU-ESP32-C3-AdBlocker>
* **原始上游**：[M-Abozaid/esp32-c3-adblock](https://github.com/M-Abozaid/esp32-c3-adblock)，同为 MIT，版权声明为 `Copyright (c) 2026 zed`。
* **许可证**： MIT — Copyright (c) 2026 zed
* **本项目采用了**：
  * 拦截用 DNS 服务器的基础实现（端口 53，UDP + TCP 双栈），本仓库有后续修改。
  * 40-bit FNV-1a 哈希 blocklist 格式：flash 中的排序哈希、magic
    `CADB`、`HASH_BYTES = 5`、二分查找，以及 RAM 中的 Bloom 预过滤。
  * 异步上游转发设计（改写查询 ID、维护 pending 表、把应答匹配回客户端）。
  * `tools/build_blocklist.py` 与 `tools/test_build_blocklist.py`，与该格式
    保持字节兼容。
  * Web 控制台的大部分交互模型（Basic Auth、CSRF token、ban/unban、
    自定义域名列表、远程 blocklist URL）。

本项目保持上游 v1 `CADB` 格式（20 字节头部、5 字节哈希），但导入还受非空、
精确文件长度和文件系统空间限制。格式兼容不等于数据来源或许可证相同。

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
sinkhole，不使用 InkSight 的服务；仍会访问上游 DNS、NTP 和用户配置的列表地址。长期规则见
`AGENTS.md`。

## 3. blocklist 数据来源

`tools/build_blocklist.py` **默认下载的输入**如下；这份配置不能证明仓库内旧
`data/blocklist.bin` 的准确来源。列表是数据，按各自条款处理：

* **StevenBlack/hosts** — <https://github.com/StevenBlack/hosts>
  （默认 `hosts` 聚合列表，仓库声明为 MIT；输入还列有其他维护者的来源和归属）。
* **hagezi/dns-blocklists** — <https://github.com/hagezi/dns-blocklists>
  （`light-onlydomains.txt`，列表数据为 GPL-3.0）。

`tools/build_blocklist.py` 的默认输入为这两份列表。现有 `data/blocklist.bin`
未记录输入快照及校验和，不能用默认输入的当前版本证明历史二进制的来源。

## 4. 平台、构建工具与字体

工具本身未提交到源码仓库，平台运行库和字体点阵则会进入固件。
本次实际版本见[构建验证记录](docs/BUILD-VERIFICATION.md)。

* **PlatformIO Core** — <https://github.com/platformio/platformio-core>（Apache-2.0）；
  **platform-espressif32 6.12.0** — <https://github.com/platformio/platform-espressif32/tree/v6.12.0>（Apache-2.0）。
* **Arduino-ESP32 core**（Espressif）— <https://github.com/espressif/arduino-esp32>
  （本次 2.0.17，包声明为 LGPL-2.1-or-later；部分目录另有许可，按文件原文处理）。
* **ESP-IDF**（Espressif）— <https://github.com/espressif/esp-idf>
  （本次随 Arduino 包提供的 SDK 为 4.4.7，主体 Apache-2.0）。**lwIP**
  使用 BSD-3-Clause，其他 SDK 组件按各自文件许可；不能全部归为 Apache-2.0。
  本次 S2 SDK 配置确认 `CONFIG_LWIP_UDP_RECVMBOX_SIZE = 6`，吞吐仍需真机验证。
* **esptool** — <https://github.com/espressif/esptool>（GPL-2.0）
* **SimHei**（`simhei.ttf`）— `tools/gen_cjk_font.py` **离线**栅格化字形所用的
  Windows 系统字体。仓库只提交生成的 16×16 点阵，不随仓库再分发字体文件。未证实提取字形可再分发。
* **Node.js** — `tools/*.js` 辅助脚本使用（MIT）。
* **Zig 0.14.1** — <https://ziglang.org/>（MIT，附带 LLVM / Clang 等组件各按其许可）；
  本轮仅用于 Windows 原生 C++ 回归测试，不进入固件或 Release 附件。
* **Mbed TLS** — 随 ESP-IDF SDK 使用，CA 解析、TLS 与 SHA-256 的实现；
  条款按 SDK 内实际组件处理，不作为本项目自创算法。
* **Pillow** — <https://github.com/python-pillow/Pillow>（HPND 及所用版本的附属许可），
  仅在生成字体点阵时使用。使用生成工具的许可不替代输入字体的许可。

---

## 5. 新实现与算法 / 协议参考

v1.3.0 的固定候选计数器、DNS 缓存和有界 HTTP 读取器由本仓库实现，没有引入外部
应用库；不将这些后续改动全部归为原 DNS / 面板上游的工作。
Space-Saving 算法参考 Metwally、Agrawal、El Abbadi 的
[UCSB 2005-23 技术报告](https://cs.ucsb.edu/research/tech-reports/2005-23)，
不是复制报告源码。DNS 报文和 TTL 语义参考
[RFC 1035](https://www.rfc-editor.org/rfc/rfc1035)；本实现只支持指南列明的保守子集。

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
