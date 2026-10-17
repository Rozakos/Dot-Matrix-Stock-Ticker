# Dot-Matrix Stock Ticker

Also includes an **ESP32 + SSD1306 128x64 OLED** target (`esp32-oled`). It
uses I2C SDA=21 / SCL=22 and probes addresses 0x3c and 0x3d. Build with
`pio run -e esp32-oled -t upload --upload-port COM5`. Its partition table
preserves the original OLED board's NVS/filesystem locations. Back up an
existing device before changing its firmware. The OLED defaults to NVDA,
INTC and AMD; the web UI and native app can replace that watchlist.

Both variants implement the Rozakos Home local protocol: mDNS
`_rozakos._tcp`, `/api/device-info`, `/api/state`, and GET/POST `/api/settings`.
The OLED identifies as `oled_stock_ticker`; the matrix identifies as
`dot_matrix_stock_ticker`. Rozakos Home 0.3.4 recognizes both, displays live
quotes, and edits watchlists and refresh intervals natively. Tokens are
write-only; settings APIs never return WiFi credentials or the bearer token.

Rozakos IoT also connects over LAN (`GET /info`, `GET /state`, `POST /command`,
WebSocket port 81) or Android USB serial at 115200 baud. Its first dashboard
shows quotes and provides watchlist, refresh, brightness, dwell and scroll
controls. USB and WebSocket telemetry use newline-delimited `{pin,value}`
messages (WebSocket messages each contain one JSON object). The optional
`esp32-oled-test` build adds the localhost USB HTTP diagnostics used by
`scripts/usb_http_bridge.py`; use `esp32-oled` for normal operation.

Run `python scripts/hardware_smoke.py http://<device-ip>` to check the Home
protocol against hardware. It restores the original watchlist and refresh.
See [integration and setup details](docs/integration.md) and
[release notes](CHANGELOG.md).

ESP32 firmware for a two-row LED dot-matrix stock ticker: **8× MAX7219 8×8
modules** (FC-16 type) arranged as two rows of four. The top row shows the
symbol, the bottom row scrolls the price and day change with an up/down arrow.
Quotes come from the self-hosted stock API (`rozakos.eu/stocks/api/v1`, the same
one the CYD and STM32 tickers use).

## Hardware

| Part | Notes |
|---|---|
| ESP32 dev board | `esp32dev` (PlatformIO) |
| 8× MAX7219 FC-16 modules | one daisy chain, 2 rows × 4 |

Wiring (ESP32 VSPI):

| MAX7219 | ESP32 |
|---|---|
| DIN | GPIO23 (MOSI) |
| CLK | GPIO18 (SCK) |
| CS  | GPIO5 (`cfg::DISPLAY_CS_PIN`) |
| VCC / GND | 5 V / GND (use a supply that can feed 512 LEDs at your brightness) |

DIN enters the **top-left** module; modules 0-3 are the top row left to right,
4-7 the bottom row left to right (no serpentine, no 180° flip).

## Build & flash

```sh
cp src/secrets_example.h src/secrets.h   # optional WiFi + API token seed
pio run -e esp32 -t upload
pio device monitor                       # 115200
```

`src/secrets.h` is gitignored and only seeds `/settings.json` on the very first
boot; after that the web pages own the settings.

## First setup

1. On boot the display scrolls **HELLO** on both rows (wiring check).
2. Without saved WiFi it opens an open access point **`Ticker-Setup-XXXX`** and
   the display says `WIFI / JOIN Ticker-Setup-XXXX`. Join it from a phone; the
   captive portal asks for your WiFi network and password.
3. Once connected it shows `CONNECTING` until the first quotes arrive.

## Web admin

`http://<device-ip>/settings` on your network:

- symbols (add / delete), refresh interval, per-symbol dwell, scroll speed,
  brightness;
- the **API token**: write-only. The page never shows the stored token; leave
  the field blank to keep it, enter `-` to clear it.

## Behaviour

- One verified HTTPS batch request for up to 16 symbols per refresh (default
  every 20 s). Trust anchors are Google Trust Services and ISRG roots; NTP
  must synchronize before fetching. Settings changes trigger an immediate fetch.
- If a symbol's fetch fails, it keeps showing its **last good price**; once
  that price is older than 4 refresh intervals (at least 5 minutes) the bottom
  row adds **`STALE`**.
- If the network stops answering entirely for 10 refresh cycles while WiFi is
  still connected (lwIP wedge), the board reboots itself. Any HTTP reply, even
  an error, counts as "answering", so a wrong token can't cause a reboot loop.

## Layout

```
src/config.h                 API base, defaults, display pins
src/main.cpp                 display task (core 1) + network task (core 0)
src/display/                 IDisplay + MD_Parola implementation (2 zones)
src/ticker/                  symbol cycle: dwell, render, stale marker
src/net/                     WiFi manager, captive portal, web admin, fetcher, store
src/settings/                LittleFS-backed settings
```

CI (self-hosted Jenkins, `Jenkinsfile`) builds the matrix and production OLED
targets on every push. Tag builds publish separate matrix and OLED application
images (`scripts/publish_release.py`). The diagnostic target is not published.
