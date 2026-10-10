# esp32s2-mini-epaper-adblock

**English** | [简体中文](README.zh-CN.md)

An **ESP32-S2 mini** turned into a self-contained **DNS ad-blocker** whose
statistics are painted on a **7.5" four-colour e-paper "magic" panel**
(768×552, JDY79668 / GDEY075Z08 class). No cloud, no phone app, no account:
one board, one screen, one power cable, and a network that stops showing
ads.

The panel refreshes every three minutes and shows what the device itself
blocked — today's total, the block rate, a per-hour histogram and the top
domains — while the DNS engine keeps answering queries in the background.
Everything else (per-client stats, custom rules, ban lists, blocklist
updates, OTA, password) lives in a built-in Chinese web console served
straight from the firmware.

> **This is a pure AI-built project.** The entire codebase — firmware,
> e-paper driver, web console, build tooling and documentation — was
> written end-to-end by an AI coding agent under human direction, and was
> verified on real hardware at every step. See
> [AI-built declaration](#ai-built-declaration) below. It also stands on
> two MIT-licensed open-source projects; see [Credits](#credits).

---

## Table of contents

- [What it does](#what-it-does)
- [Hardware](#hardware)
- [Quick start](#quick-start)
- [Detailed guide](GUIDE.md) — full build, flash, first-boot, console, blocklist and troubleshooting walkthrough
- [Downloads / releases](#downloads--releases)
- [Built on open source (credits)](#built-on-open-source-credits)
- [AI-built declaration](#ai-built-declaration)
- [Repository layout](#repository-layout)
- [License](#license)

---

## What it does

* **DNS sinkhole on port 53** (UDP **and** TCP). Blocked domains answer
  `0.0.0.0` (A) / `::` (AAAA); everything else is forwarded to `9.9.9.9`
  asynchronously.
* **Blocklist in flash**: a sorted 40-bit FNV-1a hash table, pre-filtered
  by a 128 KB Bloom filter allocated in PSRAM. The format (magic `CADB`,
  `HASH_BYTES = 5`) is byte-compatible with the upstream
  [9M2PJU/ESP32-C3-AdBlocker](https://github.com/9M2PJU/ESP32-C3-AdBlocker)
  project. The shipped list is ~103,000 domains.
* **Per-client tracking**: IP, MAC, blocked/allowed counters, ban/unban.
* **A self-contained web console** (Basic Auth + CSRF, no CDN, no external
  assets): live tiles, an hourly chart, the top-domain table, per-client
  rows, custom-domain chips, blocklist auto-update URL, firmware/blocklist
  upload, an **admin-password change** form and a "refresh the screen now"
  button.
* **E-paper dashboard, refreshed every 3 minutes**: today's blocked count,
  total queries and block rate, a 24-bar histogram, the top blocked
  domains, and a footer with all-time totals and uptime.
* **WiFi setup that recovers itself**: on power-up it tries `/wifi.cfg`,
  then `secrets.h`, then the ESP-IDF NVS store; if all fail it opens an
  `AdBlock-Setup` access point. If it drops off the network at runtime it
  retries every 30 s and, after six failures, reopens that portal by
  itself. Holding the BOOT button for 3 s also reopens it on demand.
* **OTA** firmware update and **mDNS** (`adblock.local`).

## Hardware

| Part | Detail |
|---|---|
| Board | ESP32-S2 mini, verified on **ESP32-S2FNR2** — 4 MB flash + 2 MB PSRAM |
| Panel | 7.5" 768×552 **four-colour** e-paper, JDY79668 / GDEY075Z08 class |
| Wiring | see the pin table in the [detailed guide](GUIDE.md#wiring) |

| Signal | GPIO |
|---|---|
| EPD MOSI | 11 |
| EPD SCK | 7 |
| EPD CS | 12 |
| EPD DC | 17 |
| EPD RST | 16 |
| EPD BUSY | 4 |
| Battery ADC | 6 |
| Config button | 0 (BOOT) |
| Status LED | 15 |

## Quick start

```bash
# 1. build (PlatformIO)
python -m pip install --upgrade platformio
python -m platformio run -d 3.98/merged-adblock-epd

# 2. flash the app + filesystem
python -m platformio run -d 3.98/merged-adblock-epd -t upload
python -m platformio run -d 3.98/merged-adblock-epd -t uploadfs

# 3. or build one merged image and flash it at 0x0
python -m platformio run -d 3.98/merged-adblock-epd -t buildfs
python 3.98/merged-adblock-epd/tools/make_release.py
esptool.py --chip esp32s2 write_flash 0x0 3.98/merged-adblock-epd/firmware-merged.bin
```

Open the serial console at **115200** baud to read the admin password
(printed on every boot). Then browse to `http://adblock.local/` (or the IP
shown on the panel) and log in as **`admin`**.

**Full walkthrough → [GUIDE.md](GUIDE.md).**

## Downloads / releases

Prebuilt **merged firmware** images (`firmware-merged.bin`, flashed at
offset `0x0`) are attached to the
[**Releases**](https://github.com/youkum/esp32s2-mini-epaper-adblock/releases)
page. A merged image bundles bootloader + partition table + application +
LittleFS (with the blocklist), so one file gives you a device that actually
blocks something.

> A freshly flashed merged image has **no `/auth.cfg`**, so the device
> generates a new random admin password on first boot — read it from the
> serial console.

## Built on open source (credits)

This project is a **derivative work**. Two MIT-licensed projects carry most
of the heavy lifting, and they are credited here, in
[CREDITS.md](CREDITS.md), and in the source headers:

* **DNS engine** — [9M2PJU/ESP32-C3-AdBlocker](https://github.com/9M2PJU/ESP32-C3-AdBlocker)
  (MIT, © 2026 zed): the blocking DNS server, the 40-bit FNV-1a hash
  blocklist format, the Bloom pre-filter and the async upstream forwarding.
* **E-paper renderer** — [datascale-ai/inksight](https://github.com/datascale-ai/inksight)
  (InkSight / 墨鱼, MIT, © 2026 datascale-ai): the four-colour panel driver,
  drawing primitives and the ESP32-S2 mini pin profile.

Also used: **PlatformIO**, **Arduino-ESP32**, **ESP-IDF** (incl. lwIP),
**esptool**, **Node.js**, and the **StevenBlack/hosts** + **hagezi/dns-blocklists**
data lists. Full breakdown, including what was deliberately *not* reused,
is in [CREDITS.md](CREDITS.md).

## AI-built declaration

**This is a pure AI project.** Every line of firmware, driver, web console,
build script and document in this repository was produced by an AI coding
agent (Trae / Claude-class model) working from human instructions. There
was no pre-existing codebase that a human wrote for this device and then
handed over; the agent read the two upstream projects, designed the merge,
wrote the code, built it, flashed it and verified it against an actual
ESP32-S2 mini and panel.

What that means in practice:

* **Verification is real, not assumed.** The merge rule is: nothing counts
  as working until it is seen on the hardware. Examples that were caught
  only by measurement, not by reading the code — a hardware-SPI path that
  looked 35× faster but silently refreshed nothing; a band-window counter
  bug that shredded the picture while the logs reported a healthy refresh;
  and the UDP receive-mailbox ceiling documented in
  [`AGENTS.md`](3.98/merged-adblock-epd/AGENTS.md).
* **Known limits are written down, not hidden.** See the "burst ceiling"
  section in [AGENTS.md](3.98/merged-adblock-epd/AGENTS.md) and the
  *Throughput* section of the [firmware README](3.98/merged-adblock-epd/README.md#throughput-and-the-burst-ceiling).
* **You should still review before trusting it.** It drives a mains-powered
  device on your network. Read the source; the agent's constraints and
  reasoning are recorded in [`AGENTS.md`](3.98/merged-adblock-epd/AGENTS.md).

## Repository layout

```
.
├── 3.98/merged-adblock-epd/        ← the firmware project
│   ├── src/                         firmware sources
│   ├── tools/                        build / font / release / flash helpers
│   ├── data/blocklist.bin            compiled blocklist image
│   ├── partitions.csv                flash layout
│   ├── platformio.ini                build config (env: s2mini)
│   ├── README.md                     firmware technical deep-dive
│   └── AGENTS.md                     engineering constraints & invariants
├── docs/agents/                      AI-agent workflow docs (issue tracker, triage, domains)
├── GUIDE.md                          detailed step-by-step guide
├── CREDITS.md                        open-source attribution
├── LICENSE                           MIT (+ upstream notices)
└── AGENTS.md                         repo-level agent policy
```

## License

MIT — see [LICENSE](LICENSE). The upstream components are also MIT; their
notices are reproduced in [CREDITS.md](CREDITS.md).
