# AGENTS.md

## Project

`merged-adblock-epd` — a DNS sinkhole with a four-colour e-paper
dashboard, for the ESP32-S2 mini. See `README.md` for the hardware
wiring and build steps.

## Two upstreams, and what was taken from each

* DNS engine — [9M2PJU/ESP32-C3-AdBlocker](https://github.com/9M2PJU/ESP32-C3-AdBlocker)
  (MIT). Blocklist format, hashing, async upstream forwarding, web
  dashboard.
* E-paper renderer — InkSight ("墨鱼"). Driver, font, drawing
  primitives **only**.

Do not reintroduce InkSight's backend, webapp, mobile app, mode system
or cloud content. The panel shows locally computed statistics; it is
not a content display for a remote service.

## Constraints

* **Target is ESP32-S2FNR2: 4 MB flash + 2 MB PSRAM.** The binding
  constraint is internal DRAM (176 KB, shared with WiFi), not the chip.
  The 105,984-byte full canvas would not fit in DRAM alongside WiFi, so
  the picture is never materialised: the panel is rendered in 64-row
  bands into a 12,288-byte static buffer (`s_bandBuf`, `BAND_BUF_LEN`).
* Use PSRAM deliberately, not by accident: large, sparsely-accessed
  structures go there (`MALLOC_CAP_SPIRAM`), hot per-query state stays
  in DRAM. The 128 KB Bloom pre-filter is the current example.
* Never add a full-frame staging buffer. `epdSendFrame()` streams the
  panel one band at a time and calls the renderer back per band;
  `canvasBindBand()` points the drawing helpers at that band and clips.
* **Never block `loop()` for the panel.** After a frame is streamed the
  glass sits on BUSY for ~15.3 s; `epdSendFrame()` returns before that
  and `loop()` drives `epdServiceRefresh()` instead, so the DNS engine
  keeps answering. The streamed bytes are only ~1.1 s of the ~16.8 s
  frame, so `refresh` in the `[EPD]` line is the panel health check: a
  few tens of ms there means the update was silently not accepted.
* Draw only through `drawTextColor()` / `fillRectColor()`. They apply
  the first of two cancelling transforms; indexing `colorBuf` directly
  gets you only one, i.e. mirrored text.
* Derive each row's `0x83` window from its **data row**, never from a
  running counter. The vendor loop reduces to `d < H/2 -> 2d+1`,
  otherwise `2(H-1-d)`: the odd windows carry the first half of the image
  and the even windows carry the second half in reverse. A counter that
  just walks the panel top to bottom rewrites the second half of the
  image over itself, and the glass shows the picture twice (upright on
  top, mirrored underneath) while the panel still reports a healthy 15 s
  refresh. Logs cannot catch this - only looking at the glass can.
* `trackerRecord()` runs on every DNS query. No `String`, no
  `malloc`, no logging on that path.
* The UDP DNS path has a **hard burst ceiling of ~6 datagrams**, and it
  is accepted, not a bug to chase. lwIP is pre-compiled with
  `CONFIG_LWIP_UDP_RECVMBOX_SIZE = 6`, so a burst larger than six
  overflows the receive mailbox and lwIP drops the excess before
  `handleDns()` runs. Measured: ≤50 qps sustained is 0% loss, ~100 qps
  loses ~44%, ~300 qps loses ~86%; in a single burst anything past the
  sixth packet is lost. Proof it is the arrival layer and not slow
  handling: valid BLOCKED (flash lookup), FORWARD (upstream) and
  NXDOMAIN queries, which cost wildly different amounts, all cap at the
  same ~6 of 20, and warming the ARP entry changes nothing. A
  `for`-loop around `parsePacket()` (`DNS_DRAIN_MAX`) only clears what is
  already queued; it cannot recover what lwIP has already dropped, so do
  not expect it to raise the ceiling. The only real fixes are rebuilding
  `liblwip.a` with a deeper mailbox, or a raw-PCB `udp_recv()` callback;
  both are out of scope while a household resolver runs single-digit qps
  and DNS clients retry on timeout.
* The blocklist header format is fixed at `HASH_BYTES = 5`, magic
  `CADB`, and must stay in sync with `tools/build_blocklist.py`.
* WiFi credentials resolve in this order: `/wifi.cfg` (written by the
  setup portal), `secrets.h` if someone filled it in, then the ESP-IDF
  driver's own NVS store via `WiFi.begin()`. The NVS tier is what lets a
  board that came from the original panel firmware join its old network
  with no setup at all, so keep it.
* `LittleFS.begin()` must be given `LFS_PARTITION`: it defaults to a
  partition labelled `spiffs`, and ours is labelled `littlefs`.
* The WiFi link has to be brought back by `loop()`: `connectWifi()` runs
  once in `setup()`, so without the `WiFi.reconnect()` failsafe (every
  `LIVE_WIFI_RETRY_MS` while `WiFi.status() != WL_CONNECTED`) any later
  disassociation leaves the device answering nothing until a power
  cycle. Only `WL_CONNECTED` counts as up; every other status means the
  station is down.
* Only all-time totals are persisted (`/totals.cfg`); today's counters,
  the hourly histogram and the top-domain list are RAM-only and reset on
  reboot.
* The panel font is a **generated table**: `src/cjk_font.cpp` comes from
  `tools/gen_cjk_font.py` (SimHei, 16x16, 1 bit). A missing glyph is not
  an error at runtime - it silently renders as a blank cell, so after
  changing *any* user-visible string run `python tools/gen_cjk_font.py`
  and then `node tools/check_font.js`, which reports the characters used
  in `dashboard.cpp` / `main.cpp` but absent from the table.
* Write Chinese text as real UTF-8 characters in the source. `\xNN`
  escapes do not survive the tooling that writes these files and end up
  re-encoded as mojibake, which then renders as blank cells.
* The console page is a single PROGMEM string in `src/dashboard_html.h`,
  served with `DASHBOARD_ETAG`. Bump that ETag whenever the page changes,
  or browsers keep the cached copy. The console and the panel are both in
  Chinese by design; keep it that way, and keep the page self-contained
  (no CDN, no external fonts).
* Basic Auth user is always `admin`; `/auth.cfg` holds the password on
  line 1 and the CSRF token on line 2. The password is logged on every
  boot on purpose (`[AUTH] admin account: ...`) - do not make it
  first-boot-only again, that is unrecoverable without erasing the file.

## Build

```bash
python -m platformio run -d .
```

Tools, all of which run offline from the repo root:

```bash
python tools/gen_cjk_font.py        # regenerate src/cjk_font.cpp
node tools/check_font.js            # UI strings vs. the font table
node tools/preview_dashboard.js     # render ../preview-dashboard.png
python tools/build_blocklist.py data/blocklist.bin
python tools/make_release.py        # single merged flash image
powershell -File tools/flash_and_capture.ps1 -Port COMx -CaptureSec 45
```

`tools/preview_dashboard.js` mirrors `dashboard.cpp` band for band and
writes `preview-dashboard.png`. It is the only way to see a layout or
font mistake without spending a 17 s panel refresh on it, so update it in
the same change that touches the layout.

Flashing notes: upload fails on the first try almost every time because
the port re-enumerates; the retry script is there for that. Reading flash
with esptool has the same habit (`OSError(22)` / `ClearCommError`), wait
4 s and retry. Never pipe a native `.exe` into PowerShell (`| Out-String`,
`2>&1 |`): use `*>` redirection to a file instead.

## Git

Follow the upstream ad-blocker project's preferences: do not push to
GitHub unless explicitly asked.
