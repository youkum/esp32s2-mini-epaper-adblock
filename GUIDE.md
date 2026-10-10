# Detailed Guide

A complete, start-to-finish walkthrough for building, flashing, configuring
and troubleshooting the **esp32s2-mini-epaper-adblock** firmware.

If you only want the short version, see the [README](README.md#quick-start).
This document is the long version: what every part is, why it is the way it
is, and what to do when something goes wrong.

> **AI-built project.** Everything here was written by an AI agent and
> verified on real hardware. Read the [AI-built declaration](README.md#ai-built-declaration)
> before trusting it.

---

## Table of contents

1. [What you need](#1-what-you-need)
2. [Wiring](#2-wiring)
3. [Install the toolchain](#3-install-the-toolchain)
4. [Build](#4-build)
5. [Flash](#5-flash)
6. [First boot: read the password](#6-first-boot-read-the-password)
7. [Put it on the network](#7-put-it-on-the-network)
8. [Point clients at it](#8-point-clients-at-it)
9. [The web console](#9-the-web-console)
10. [The e-paper dashboard](#10-the-e-paper-dashboard)
11. [Changing the admin password](#11-changing-the-admin-password)
12. [Blocklist: updating and customising](#12-blocklist-updating-and-customising)
13. [Building a merged release image](#13-building-a-merged-release-image)
14. [The tools directory](#14-the-tools-directory)
15. [Performance and known limits](#15-performance-and-known-limits)
16. [Troubleshooting](#16-troubleshooting)
17. [Architecture notes](#17-architecture-notes)

---

## 1. What you need

**Hardware**

* An **ESP32-S2 mini** board. Verified on **ESP32-S2FNR2**: 4 MB flash +
  **2 MB PSRAM**. PSRAM is required — the 128 KB Bloom filter is allocated
  there. A variant without PSRAM will not work.
* A **7.5" 768×552 four-colour e-paper panel**, JDY79668 / GDEY075Z08 class.
* A USB-C (or USB-micro, board dependent) data cable.
* 9 female-to-female jumper wires.
* Optional: a LiFePO4/18650 cell on the battery ADC pin for a cable-free
  build.

**Software**

* **PlatformIO Core** (`python -m pip install --upgrade platformio`), or
  the PlatformIO IDE extension.
* **Python 3.8+** for the helper tools.
* **Node.js** for the `tools/*.js` font/preview helpers (optional, only if
  you change on-screen text).
* **esptool** (installed automatically by PlatformIO; used directly only
  for the merged-image path).

**Knowledge**

* Basic comfort with a terminal.
* Knowing where your router's DNS setting lives (to point the whole
  network at this device).

---

## 2. Wiring

Wire the panel to the ESP32-S2 mini exactly as below. These are the pins
carried over from InkSight's S2 profile and fixed in
[`src/config.h`](3.98/merged-adblock-epd/src/config.h).

| Panel pin | ESP32-S2 mini GPIO | Notes |
|---|---|---|
| MOSI / DIN | **11** | bit-banged, any GPIO works |
| SCK / CLK | **7** | |
| CS | **12** | |
| DC | **17** | data/command |
| RST | **16** | |
| BUSY | **4** | panel holds this during a refresh |
| VCC | 3V3 | check your panel's datasheet |
| GND | GND | common ground |

Board-local pins:

| Function | GPIO |
|---|---|
| Battery ADC | 6 |
| Config button | 0 (BOOT) |
| Status LED | 15 |

> **Do not** try to move the panel onto the hardware SPI peripheral. The
> firmware bit-bangs SPI on purpose — see
> [Performance and known limits](#15-performance-and-known-limits).

---

## 3. Install the toolchain

```bash
python -m pip install --upgrade platformio
```

PlatformIO will download the `espressif32` platform, the Arduino-ESP32
framework and the toolchain on the first build. That download is large
(hundreds of MB); give it time on a slow link.

> On a very slow connection, `tools/fetch_pio_framework.js` can pull the
> framework tarball directly into PlatformIO's cache using several parallel
> range requests. See [The tools directory](#14-the-tools-directory).

---

## 4. Build

The firmware lives in the subdirectory `3.98/merged-adblock-epd/`. The
default PlatformIO environment is `s2mini`.

```bash
python -m platformio run -d 3.98/merged-adblock-epd
```

A successful build ends with something like:

```
RAM:   [==        ]  25.0% (used ... bytes from 327680 bytes)
Flash: [========= ]  87.3% (used ... bytes from 1179648 bytes)
========================= [SUCCESS] =========================
```

The build outputs land in `3.98/merged-adblock-epd/.pio/build/s2mini/`:

| File | Purpose |
|---|---|
| `firmware.bin` | the application |
| `bootloader.bin` | second-stage bootloader |
| `partitions.bin` | partition table |
| `firmware.elf` / `.map` | debug symbols / memory map |

**Flash budget.** The image sits at ~87% of the 1.125 MB OTA slot. There
is room to grow but not a lot — watch the number after big changes.

---

## 5. Flash

### Option A — normal upload (app + filesystem)

```bash
python -m platformio run -d 3.98/merged-adblock-epd -t upload      # app
python -m platformio run -d 3.98/merged-adblock-epd -t uploadfs    # LittleFS (blocklist)
```

### Option B — one merged image at 0x0

```bash
python -m platformio run -d 3.98/merged-adblock-epd -t buildfs
python 3.98/merged-adblock-epd/tools/make_release.py
esptool.py --chip esp32s2 --port COMx write_flash 0x0 \
    3.98/merged-adblock-epd/firmware-merged.bin
```

See [Building a merged release image](#13-building-a-merged-release-image).

### About the flaky first upload

On boards whose console is the chip's **native USB CDC** (no UART bridge),
`upload` fails on the first attempt almost every time with
`OSError(22, 'device does not support the request')` — the port disappears
while the chip re-enumerates. **Just run it again**; it succeeds the second
time.

`tools/flash_and_capture.ps1` (Windows) automates this retry loop *and*
captures the serial console:

```powershell
powershell -File 3.98/merged-adblock-epd/tools/flash_and_capture.ps1 -Port COM3 -CaptureSec 45
```

Add `-SkipBuild` to flash without rebuilding.

> **Reading the native-USB console needs DTR *and* RTS asserted.** The
> ESP32-S2's USB CDC only reports "connected" when both are high. Many
> serial tools leave them low by default, which silently drops every byte
> the device sends. The capture script sets both for you.

---

## 6. First boot: read the password

Open the serial console at **115200** baud. On every boot the device prints
its admin credentials:

```
AdBlock EPD booting (reset=3 heap=144544 psram=2095103)
[FS] mounted total=1703936 used=659456 heap=142100
[AUTH] created admin account: user admin password Xy3kQp9RmN
[BL] 103366 domains, bloom 128 KB
[BL] selftest 4/4 blocked, 3/3 clean
[BOOT] blocklist/persistence loaded, painting boot screen
[DASH] frame streamed in 1347 ms, panel refreshing
[EPD] arm=239 rows=1107 stream=1347 refresh=15339 off=70 total=16756 ms
[HB] up 23s ip 192.168.10.190 wl 3 boot 3 dom 103366 bl 128KB q 0 blk 0/0 top - 0 heap 86016 ps 1948939
```

* `[AUTH] created ...` → fresh device, a new password was generated.
* `[AUTH] admin account: ...` → existing password, re-printed.
* `[HB]` repeats every 5 s: uptime, IP, domain count, blocked/total, top
  domain, free heap and PSRAM. If `ip offline` appears, it is not on the
  network yet.

**Write the password down.** It is the login for the web console
(`admin` / that password). It is printed every boot on purpose: the console
is behind Basic Auth, so a password shown only once would be unrecoverable
without erasing the device — and the serial port is already physical access.

---

## 7. Put it on the network

Credentials are tried in this order; the first that exists wins:

1. **`/wifi.cfg`** on LittleFS — written by the setup portal.
2. **`secrets.h`** (`WIFI_SSID` / `WIFI_PASS`) — unless still the shipped
   placeholders.
3. **The ESP-IDF NVS store** — whatever a previous firmware saved.

If none work, the device opens an open access point named
**`AdBlock-Setup`** and paints a "配网模式" screen; the console log shows:

```
[WIFI] no credentials in /wifi.cfg, secrets.h or NVS
[SETUP] AP AdBlock-Setup open at http://192.168.4.1/
```

### Joining via the setup portal

1. On your phone/laptop, connect to WiFi **`AdBlock-Setup`** (no password).
2. Browse to **`http://192.168.4.1/`**.
3. Enter your WiFi name and password, save.
4. The device writes `/wifi.cfg`, restarts as a station and joins.

### Getting back into the portal later

Once connected, the portal is not reachable by IP anymore. Three ways back:

* **Hold the BOOT button for 3 s** — reopens `AdBlock-Setup` immediately
  (a *short* press just repaints the dashboard).
* From the console's **网络** card: save `/setwifi` → the device restarts
  onto the new network.
* **Do nothing** — if the link drops, the firmware retries every 30 s and
  reopens the portal automatically after **6 consecutive failures** (about
  3 minutes).

> The portal runs the device in AP mode, so DNS blocking is paused until
> you finish configuring it.

---

## 8. Point clients at it

The device is a plain DNS server on port 53 (UDP + TCP). Two ways to use it:

**Best — whole network:** set it as the upstream DNS in your router:

```
primary DNS   -> the device IP (e.g. 192.168.10.190)
secondary DNS -> anything that is NOT a second sinkhole
```

**Per-client:** set just the devices you care about to use the device IP.

The device does **not** run DHCP, so it never has to be the gateway — only
the resolver. Blocked queries come back as `0.0.0.0` / `::`; everything
else is the real answer forwarded from `9.9.9.9`.

---

## 9. The web console

Browse to `http://adblock.local/` (mDNS) or the IP on the panel header.
The whole UI is in Chinese by design and self-contained (no CDN).

**Cards**

| Card | What it does |
|---|---|
| 顶部磁贴 | today's blocked, today's queries, block rate, all-time blocked, rule count, free heap, RSSI, chip temperature |
| 每小时拦截 | last 24 h histogram of blocked queries |
| 域名排行榜 | top blocked domains today |
| 客户端 | per-client IP/MAC, blocked/allowed, ban toggle |
| 自定义拦截域名 | add / remove specific domains |
| 规则更新 | remote blocklist URL + auto-update interval, or upload a `.bin` |
| 设备 | uptime, Bloom filter, pending queries, totals, **立即刷新屏幕** |
| 网络 | change WiFi |
| 管理员密码 | change the `admin` password |

**Auth model.** Basic Auth (`admin` + your password) plus a CSRF `token`
returned by `/stats` and required on every mutating call. The page fetches
`/stats` every 5 s.

---

## 10. The e-paper dashboard

The panel does a full four-colour refresh **every 3 minutes**
(`DASH_REFRESH_MS`). Each refresh takes ~17 s end to end, but it runs in
the background so DNS keeps answering (see
[Architecture notes](#17-architecture-notes)).

It shows:

* today's blocked count as a large number, with total queries and rate;
* a 24-bar per-hour histogram;
* the top blocked domains with hit counts;
* a footer with all-time totals, uptime and last-update time.

**Manual refresh:** the console's **立即刷新屏幕** button, or a **short
press** of the BOOT button.

You can preview the layout on a PC without flashing, via
`node 3.98/merged-adblock-epd/tools/preview_dashboard.js` →
`preview-dashboard.png`.

---

## 11. Changing the admin password

Console card **管理员密码**:

1. Type the new password twice (6–64 chars, printable ASCII, no spaces).
2. Click 修改密码.

The device validates it, writes `/auth.cfg`, and logs the change. Your
browser then re-prompts for credentials on the next request — log in with
the new password.

**Forgot it?** It is printed on every boot on the serial console. To force
a brand-new one, erase `/auth.cfg` from LittleFS (or reflash a merged image
that has no `/auth.cfg`).

---

## 12. Blocklist: updating and customising

### Rebuilding from source lists

```bash
cd 3.98/merged-adblock-epd
python tools/build_blocklist.py data/blocklist.bin          # StevenBlack + Hagezi Light
python tools/build_blocklist.py data/blocklist.bin a.txt b.txt   # your own lists/URLs
```

Options:

| Flag | Meaning |
|---|---|
| `--bloom-bits N` | Bloom filter size in bits (default 1,048,576 = 128 KB) |
| `--no-bloom` | skip the Bloom filter |

The format is fixed: 20-byte header + Bloom filter + sorted 40-bit hashes,
magic `CADB`, `HASH_BYTES = 5`. It is byte-compatible with the upstream
project.

### Uploading a list

* **From the PC:** `pio run -t uploadfs` with `blocklist.bin` in `data/`,
  or use the console's 规则更新 card to upload a `.bin`.
* **Remote auto-update:** host your `.bin` somewhere and set the URL in the
  console; the device re-downloads on the interval you choose.

### Custom single domains

Add them in the 自定义拦截域名 card. They are stored in `/custom.txt` and
survive reboots.

---

## 13. Building a merged release image

A merged image is a single file flashed at `0x0` — handy for ESP Web Tools,
`esptool-js`, or handing to someone else.

```bash
python -m platformio run -d 3.98/merged-adblock-epd -t buildfs
python 3.98/merged-adblock-epd/tools/make_release.py
# -> 3.98/merged-adblock-epd/firmware-merged.bin
```

It bundles, at the offsets the board uses:

| Offset | Content |
|---|---|
| `0x1000` | `bootloader.bin` |
| `0x8000` | `partitions.bin` |
| `0xE000` | `boot_app0.bin` |
| `0x10000` | `firmware.bin` |
| `0x250000` | `littlefs.bin` (the blocklist) |

> **`-t buildfs` matters.** Without it, `make_release.py` still runs but
> leaves the filesystem out (it prints `(absent: run 'pio run -t buildfs'
> ...)`), and the flashed device blocks nothing and shows an empty
> dashboard. Pass `--no-fs` only if you deliberately want that.

**Prebuilt merged images** are on the
[Releases](https://github.com/youkum/esp32s2-mini-epaper-adblock/releases)
page.

---

## 14. The tools directory

`3.98/merged-adblock-epd/tools/` — all run offline from the repo root.

| Tool | What it does |
|---|---|
| `build_blocklist.py` | compile source lists → `blocklist.bin` |
| `test_build_blocklist.py` | unit tests for the blocklist builder |
| `make_release.py` | merge build outputs → `firmware-merged.bin` |
| `gen_cjk_font.py` | rasterise the Chinese glyph table from SimHei |
| `check_font.js` | report UI characters missing from the glyph table |
| `preview_dashboard.js` | render `preview-dashboard.png` without flashing |
| `flash_and_capture.ps1` | flash with retry + capture the serial console (Windows) |
| `fetch_pio_framework.js` | fetch the framework tarball over parallel range requests |
| `probe_flash.ps1` / `read_part.ps1` | read flash partitions back |

Note `gen_cjk_font.py` expects **SimHei** (`simhei.ttf`, ships with
Windows); override with the `CJK_FONT` environment variable on other hosts.

---

## 15. Performance and known limits

**Latency** (measured on hardware, single queries):

| Query type | p50 | p90 |
|---|---|---|
| blocked (local) | ~22 ms | ~28 ms |
| forwarded (upstream) | ~245 ms | ~255 ms |
| NXDOMAIN | ~246 ms | ~263 ms |

**Throughput.** A household resolver sees single-digit qps and is lossless.
Sustained load starts to bite near 100 qps:

| Offered rate | Loss |
|---|---|
| ≤ 50 qps | 0% |
| ~100 qps | ~44% |
| ~200 qps | ~77% |
| ~300 qps | ~87% |

The limit is a **single-burst ceiling of about six datagrams** — lwIP's UDP
receive mailbox depth (`CONFIG_LWIP_UDP_RECVMBOX_SIZE = 6`, compiled into
the prebuilt `liblwip.a`). Spacing queries 10 ms apart recovers almost all
of them. This is a **documented, accepted limit**, not a bug: see the
"burst ceiling" entry in
[`AGENTS.md`](3.98/merged-adblock-epd/AGENTS.md) for the full reasoning and
why a `parsePacket()` drain loop does not help.

**Panel refresh.** A frame is two very different costs: ~1.4 s to stream the
bits, then **~15 s** for the panel to move its pigments. The firmware splits
these so the ~15 s runs in the background; that is why the `refresh=` field
in the `[EPD]` log line is the panel **health check** (~15 s = a real
refresh; a few ms = the update was silently not accepted).

**Why SPI is bit-banged.** A brief experiment with the hardware SPI
peripheral made frames look ~35× faster while refreshing nothing — the 15 s
belongs to the panel, not the bus. Bit-banging was kept on purpose.

---

## 16. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| First `upload` fails with `OSError(22)` | Native USB re-enumeration. **Run it again** (or use `flash_and_capture.ps1`). |
| Serial console prints nothing | Native-USB CDC needs **DTR and RTS** asserted. Use `flash_and_capture.ps1`, or set both in your terminal. |
| `[HB] ... ip offline` | Not on the network. Join `AdBlock-Setup` and configure WiFi, or hold BOOT 3 s to reopen the portal. |
| Blank / stale picture, `refresh=` a few ms | The panel update was not accepted. Re-check wiring (esp. BUSY on GPIO 4) and power. |
| Picture shredded / mirrored | A band-window or rotation transform broke. Only draw via `drawTextColor()` / `fillRectColor()`, and derive each row's window from its **data row** — see `AGENTS.md`. |
| Panel never seems to update | Four-colour refresh takes ~15 s and flashes; give it time. |
| `pio` download stalls | Use `tools/fetch_pio_framework.js` or retry on a better link. |
| Console 401 after a reflash | A merged image has no `/auth.cfg`, so a fresh password was generated — read the serial log. |
| Whole network loses DNS | The device is down or off-network; your router should have a non-sinkhole secondary. |
| Chinese text shows as blank boxes | A glyph is missing from `cjk_font.cpp`. Run `node tools/check_font.js`, regenerate with `gen_cjk_font.py`. |

---

## 17. Architecture notes

These are the rules the firmware is built on; full detail is in
[`AGENTS.md`](3.98/merged-adblock-epd/AGENTS.md).

* **No full-frame buffer.** 768×552 at 2 bpp is 105,984 bytes — it does not
  fit in the S2's 176 KB DRAM alongside WiFi. The picture is never
  materialised: `epdSendFrame()` renders and streams it **one 64-row band at
  a time** into a 12,288-byte static buffer.
* **Never block `loop()`.** After a frame is streamed the glass holds BUSY
  for ~15 s; `epdSendFrame()` returns first and `loop()` drives
  `epdServiceRefresh()` so DNS keeps answering.
* **Two cancelling rotation transforms.** `drawTextColor()` /
  `fillRectColor()` apply one; `epdRowData()` applies the other. Draw only
  through the helpers, never index `colorBuf` directly.
* **Windows come from data rows**, never a running counter — see the
  `0x83` window mapping in `epd_driver.cpp`.
* **PSRAM is used on purpose** for the 128 KB Bloom filter; hot per-query
  state stays in DRAM.
* **The hot DNS path allocates nothing** — `trackerRecord()` uses no
  `String`, no `malloc`, no logging.

---

*Questions or fixes: open an issue on the
[issue tracker](https://github.com/youkum/esp32s2-mini-epaper-adblock/issues).*
