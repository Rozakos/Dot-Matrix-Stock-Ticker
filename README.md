# Dot-Matrix Stock Ticker

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

- One HTTPS request per symbol per refresh (default every 20 s).
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

CI (self-hosted Jenkins, `Jenkinsfile`) builds every push; pushing a `v*` tag
publishes a GitHub Release with `firmware.bin` (`scripts/publish_release.py`).
