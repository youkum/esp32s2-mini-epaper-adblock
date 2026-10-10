# Credits & Open-Source Attribution

This project is a **derivative work**. It would not exist without the
projects below, and every one of them is credited here rather than buried
in a source comment. All upstream components are **MIT licensed**; their
full license texts are reproduced verbatim at the bottom of this file.

If you are an upstream author and want the wording here changed, open an
issue — attribution corrections are always welcome.

---

## 1. DNS engine — 9M2PJU / ESP32-C3-AdBlocker

* **Repository**: <https://github.com/9M2PJU/ESP32-C3-AdBlocker>
* **License**: MIT — Copyright (c) 2026 zed
* **What this project took**:
  * The whole blocking DNS server (UDP + TCP on port 53).
  * The 40-bit FNV-1a hash blocklist format: sorted hashes in flash, magic
    `CADB`, `HASH_BYTES = 5`, binary search, and the RAM Bloom pre-filter.
  * The asynchronous upstream-forwarding design (rewrite the query ID, keep
    a pending table, match the reply back to the client).
  * `tools/build_blocklist.py` and `tools/test_build_blocklist.py`, which
    stay byte-compatible with that format.
  * Much of the web-console interaction model (Basic Auth, CSRF token,
    ban/unban, custom-domain list, remote blocklist URL).

The blocklist **file format is intentionally kept identical** to the
upstream project, so a `blocklist.bin` built for either firmware loads in
the other.

## 2. E-paper renderer — InkSight (墨鱼)

* **Repository**: <https://github.com/datascale-ai/inksight>
* **Project site**: <https://www.inksight.site/>
* **License**: MIT — Copyright (c) 2026 datascale-ai
* **What this project took**:
  * The JDY79668 / GDEY075Z08-class 7.5" four-colour e-paper driver
    (`epd_driver.cpp` / `epd_driver.h`), including the panel-specific
    power-on sequence, the `0x83` row-window interleave and the
    bit-banged SPI transfer.
  * The drawing primitives and the two cancelling rotation transforms
    (`display.cpp` / `display.h`).
  * The ESP32-S2 mini pin profile.
  * The generated Chinese glyph-table approach (`cjk_font.cpp` +
    `tools/gen_cjk_font.py`).

**What was deliberately *not* taken**: InkSight's cloud backend, Next.js
web app, mobile app, the 24 built-in "modes", the LLM content pipeline and
the online flashing flow. This firmware is a self-contained sinkhole; it
never talks to `inksight.site` or any other remote service. See
`AGENTS.md` for the standing rule.

## 3. Blocklist data sources

The default `blocklist.bin` is built from these community lists. They are
**data**, not code, and carry their own licenses (both are permissive;
check the repositories for the current terms before redistributing a
derived list):

* **StevenBlack/hosts** — <https://github.com/StevenBlack/hosts>
  (default `hosts`, MIT).
* **hagezi/dns-blocklists** — <https://github.com/hagezi/dns-blocklists>
  (`light-onlydomains.txt`, GPL-3.0 for the list data).

The shipped `data/blocklist.bin` is a compiled hash table of the union of
these two (about 103,000 domains), not a copy of either file.

## 4. Platform & toolchain (fetched by PlatformIO, not vendored here)

* **PlatformIO** — <https://platformio.org/> (Apache-2.0)
* **Arduino-ESP32 core** (Espressif) — <https://github.com/espressif/arduino-esp32>
  (LGPL-2.1)
* **ESP-IDF** (Espressif) — <https://github.com/espressif/esp-idf>
  (Apache-2.0), including the pre-compiled **lwIP** stack
  (`CONFIG_LWIP_UDP_RECVMBOX_SIZE = 6` — see the burst-ceiling note in
  `AGENTS.md`).
* **esptool** — <https://github.com/espressif/esptool> (GPL-2.0)
* **SimHei** (`simhei.ttf`) — the Windows system font used **offline** by
  `tools/gen_cjk_font.py` to rasterise glyphs. Only the resulting 16×16
  bitmaps are committed; no font file is redistributed.
* **Node.js** — used by the `tools/*.js` helper scripts (MIT).

---

## Full license texts

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
