# AdBlock DNS sinkhole + four-colour e-paper dashboard

A merge of two projects, running on one **ESP32-S2 mini**:

| Half | Taken from | What we kept |
|---|---|---|
| DNS engine | [9M2PJU/ESP32-C3-AdBlocker](https://github.com/9M2PJU/ESP32-C3-AdBlocker) | The whole blocking DNS server |
| Renderer | InkSight ("墨鱼") | The e-paper driver, font and drawing primitives |

Everything InkSight did *around* the panel is gone: no cloud backend, no
Next.js site, no mobile app, no 24 built-in modes, no LLM content. The
screen no longer shows anything downloaded from someone else's server —
it shows what this device itself blocked.

## What it does

* Runs a DNS sinkhole on port 53 (UDP **and** TCP) that answers blocked
  domains with `0.0.0.0` / `::`, and forwards everything else to
  9.9.9.9.
* Blocks from a 40-bit FNV-1a hash table in flash, pre-filtered by a
  Bloom filter in RAM, plus a small editable custom list.
* Per-client tracking (IP, MAC, blocked/allowed counters, ban toggle).
* A self-contained Chinese console served from the firmware (Basic
  Auth, no CDN, no external assets): live tiles, an hourly SVG chart,
  the top-domain table, per-client rows with ban/unban, custom-domain
  chips, the blocklist auto-update URL, firmware/blocklist upload and a
  "refresh the screen now" button.
* The e-paper panel, refreshed every 3 minutes, showing:
  * **today's blocked count** as a big black number, with total queries
    and the block rate;
  * a **24-bar per-hour histogram** of blocked queries;
  * the **top blocked domains** with hit counts;
  * a footer with all-time totals, uptime and the last update time.

## Hardware

ESP32-S2 mini (verified on an **ESP32-S2FNR2**: 4 MB flash, 2 MB PSRAM)
plus a 7.5 inch 768x552 four-colour panel, JDY79668 / GDEY075Z08 class,
driven with the same wiring as InkSight's S2 profile:

| Signal | GPIO |
|---|---|
| MOSI | 11 |
| SCK | 7 |
| CS | 12 |
| DC | 17 |
| RST | 16 |
| BUSY | 4 |
| Battery ADC | 6 |
| Config button | 0 (BOOT) |
| Status LED | 15 |

## Why there is no full-frame buffer

768 x 552 at 2 bpp is **105,984 bytes**. Internal DRAM is the real
bottleneck on this chip: 176 KB total, shared with the WiFi stack, the
DNS buffers and the tracker — and the link fails outright if a frame
buffer of that size is made static. (2 MB of PSRAM *is* available on the
S2FNR2 and is used deliberately for the 128 KB Bloom filter, but a
106 KB canvas is traffic this screen — refreshed every three minutes —
does not need.)

So the picture is never materialised at all. `epdSendFrame()` renders
and streams it **one 64-row band at a time** into a 12,288-byte static
buffer (`s_bandBuf`, `BAND_BUF_LEN`). Each band redraws the whole screen
through the clipped drawing helpers, so the layout code above stays
band-agnostic, and the firmware keeps working on S2 mini variants that
have no PSRAM.

## Rotation

Two transforms sit in the drawing path and they cancel out:

* `drawTextColor()` / `fillRectColor()` map `px = W-1-px, py = H-1-py`;
* `epdRowData()` emits the bytes of a row right-to-left and swaps the
  four 2-bit pixels inside each byte.

The driver also has to place each row in the right `0x83` window. The
vendor's invariant is `window p carries data row H-1-p`; it never writes
that down because its source buffer is a whole frame and its `flippedY`
lookup keeps window and data in step automatically. A banded renderer
streams *logical* rows, so it must state the invariant itself: band-local
row `k` is logical row `rBase+k` and therefore belongs in window
`H-1-(rBase+k)`.

Copying the vendor's *window* expression (`2r+1` / `2(H-1-r)`) into the
band loop instead is a subtle and total break: consecutive logical rows
land two panel rows apart, the frame splits into two interleaved combs,
and every byte is still accepted — the panel reports a healthy 15 s
refresh while the picture on the glass is shredded. It was a photograph
of the panel that caught it, not the logs.

Net effect: the canvas is in normal logical coordinates. **Always draw
through `drawTextColor` / `fillRectColor`; never index `colorBuf`
directly**, or you will apply only one of the two transforms and end up
with mirrored text.


## Refresh cost, and why it runs in the background

A frame costs two very different amounts of time:

| phase | time | code |
|---|---|---|
| reset + power-on | ~0.24 s | `epdSendFrame()` |
| stream all 106 KB (bit-bang) | ~1.1 s | `epdSendFrame()` |
| **panel moving the pigments** | **~15.3 s** | `epdServiceRefresh()` |

Every frame logs `[EPD] arm= rows= stream= refresh= off= total= ms`.
**The `refresh` field is the panel health check**: ~15 s means the glass
really refreshed; a few tens of milliseconds means the update was never
accepted. That is exactly what a brief experiment with the hardware SPI
peripheral produced - it made a frame look 35x faster (16.8 s to 0.47 s)
while silently doing nothing, because the 15 s belongs to the panel, not
to us. Measuring `digitalWrite` (370 ns/op, so ~1.1 s for the 3.03M calls
a frame needs) is what exposed it, and bit-banging was kept as a result.

15 s of blocking would stop the DNS engine answering queries every three
minutes, so the transfer is split in two. `epdSendFrame()` streams the
bands and issues the `0x12` update command, then returns at once;
`loop()` calls `epdServiceRefresh()`, which waits for BUSY to go high and
only then sends `0x02` to power the panel down. The network therefore
keeps running for the whole refresh, and a new frame simply waits for any
refresh already in flight (`epdWaitRefresh()`).
## Build

```bash
python -m pip install --upgrade platformio
python -m platformio run -d .
python -m platformio run -d . -t upload
```

The flash layout in `partitions.csv` spends the 4 MB as two 1.125 MB
OTA app slots (the image is ~1.0 MB, so OTA-updating works) plus a
**1.625 MB** LittleFS partition for the blocklist — enough for the
default StevenBlack + Hagezi Light list, which is ~200k domains: a 128 KB
Bloom filter plus 5 bytes per hash, about 1.1 MB.

### Flashing

`python -m platformio run -d . -t upload` writes the four images to their
own offsets. If you would rather have a single file — for a browser
flasher, or to hand to someone else — merge them first:

```bash
python tools/make_release.py
```

That writes `firmware-merged.bin`, which is flashed at offset 0. It
bundles the LittleFS image too (run `-t buildfs` first), so one file
gives you a device that actually blocks something; pass `--no-fs` to
leave the blocklist out. The image is ~2.8 MB with the default list:

```bash
esptool.py --chip esp32s2 write_flash 0x0 firmware-merged.bin
```

`tools/make_release.py` holds the offsets in `LAYOUT`; if you change
`partitions.csv`, change them there too.

## Blocklist

`tools/build_blocklist.py` produces the `/blocklist.bin` blob (20-byte
header + Bloom filter + sorted 40-bit hashes). The format and the
constants are identical to the upstream project — `HASH_BYTES = 5`,
magic `CADB`.

```bash
python tools/build_blocklist.py blocklist.bin
```

Then either upload it through the web dashboard, or:

```bash
python -m platformio run -t uploadfs
```

with `blocklist.bin` in `data/`.

## Logging in

The device generates a random 10-character password on first boot and
stores it in `/auth.cfg` on LittleFS along with a CSRF token. Browsing
to `http://adblock.local/` (or the IP shown in the panel header)
prompts for it: **user `admin`**, with that password.

The password is printed on **every** boot, as an `[AUTH] admin account:
user admin password ...` line on the serial console (115200 baud). That
is deliberate: the console is behind Basic Auth, so a password that were
only printed once would be unrecoverable, and the serial port is already
physical access to the device. Deleting `/auth.cfg` from LittleFS is
what asks the firmware to generate a new one.

The console is the chip's **native USB CDC port** (there is no UART
bridge), so it only appears once a host has the port open — the firmware
waits up to 5 s at boot for that and prints a `[HB]` status line every
5 s thereafter (uptime, IP, domain count, blocked/total, top domain,
free heap and PSRAM). On macOS/Linux that is the `USB JTAG/serial`
device; on Windows it enumerates as `USB 串行设备 (COMx)`.

## Getting it on the network

Credentials are tried in this order, and the first one that exists wins:

1. `/wifi.cfg` on LittleFS, written by the setup portal (the network
   card in the console, or `POST /setwifi` with `ssid` and `pass`);
2. the compile-time `WIFI_SSID` / `WIFI_PASS` in `src/secrets.h`, unless
   they are still the shipped placeholders;
3. the ESP-IDF driver's own NVS store (`WiFi.begin()` with no
   arguments). This is not a corner case: the panel this firmware was
   ported from saved its network through its own web app, so a board
   that has already been in service joins that network with no setup at
   all. The stored SSID is logged, the password is not.

If none of them work the device opens the **AdBlock-Setup** access point
with a setup page at `http://192.168.4.1/` (the AP itself is open), and
paints a matching screen so the two states can be told apart without a
serial console. To get back into that portal on a board that is already
configured — after changing the router's password, say — **hold the BOOT
button for 3 seconds**: the firmware brings the access point back up
from the main loop and repaints the panel. A short press just repaints
the dashboard. If the link drops, the loop also reconnects on its own
every 30 s, and falls back to the portal after six failed attempts.

Then point something at it. The device is a plain DNS server on port 53
(UDP and TCP), so either set it as the DNS server on the clients you
care about, or — better — set it as the upstream in your router so
everything on the network goes through it:

```
primary DNS   -> the IP in the panel header (e.g. 192.168.10.190)
secondary DNS -> anything except a second sinkhole, or it will answer
                 the queries the first one is supposed to swallow
```

It does not run a DHCP server by default, so it never has to be the
gateway, only the resolver. Queries against it either come back as
`0.0.0.0` / `::` (blocked) or as the real answer forwarded from
9.9.9.9.
