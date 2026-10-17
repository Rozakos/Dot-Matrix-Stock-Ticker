# Setup and app integration

## Network setup

Current OLED and matrix firmware use a Wi-Fi captive portal, without Bluetooth
provisioning. At boot, an empty configuration or a failed connection to saved
Wi-Fi starts the open `Ticker-Setup-XXXX` network. A saved network is tried for
up to 15 seconds before fallback. Use a 2.4 GHz network.

In Rozakos Home, select **Add device > New device > OLED Stock Ticker** (or
matrix), open Android Wi-Fi settings and join that setup network. Stay connected
despite the no-internet message. Return and open the setup portal at
`http://192.168.4.1`; submit the home Wi-Fi credentials, reconnect the phone to
home Wi-Fi, then find the ticker under **Nearby**. Manual IP entry also works.

Home's CYD Bluetooth flow uses a different firmware protocol. The presence of
Bluetooth hardware on an ESP32 does not make this firmware compatible with it.
No Android Bluetooth pairing is needed for the Wi-Fi portal flow.

An unprovisioned stock API key can be entered in `/settings` after joining the
home network. Keys remain on the device; the apps do not need the stock API
master secret. `src/secrets.h` is an optional, ignored first-boot seed. Changing
the seed does not replace settings already stored in LittleFS.

## Rozakos Home

Home 0.3.4 supports `oled_stock_ticker` and `dot_matrix_stock_ticker`. Discovery
uses `_rozakos._tcp` mDNS and subnet probes. HTTP port 80 exposes:

| Route | Contract |
| --- | --- |
| `GET /api/device-info` | Stable MAC-derived ID, type, name, firmware and capabilities |
| `GET /api/state` | Ordered quotes, freshness, market session and update timestamp |
| `GET /api/settings` | Watchlist, refresh, display controls and `has_api_key`; no credentials |
| `POST /api/settings` | Validated `symbols`, `refresh_s`, optional write-only `api_key` |

Watchlists contain 1–16 distinct symbols. Refresh accepts integer seconds from
15 to 3,600. Home searches company names locally, edits a draft, and applies it
only when Save to device is pressed.

## Rozakos IoT

`GET /info` advertises the active watchlist's pins. `GET /state` and WebSocket
port 81 stream all 69 fields: five controls plus four fields for each of 16
slots. Unused slots are cleared so removed symbols do not keep stale telemetry.
USB serial uses the same `{ "pin": "...", "value": ... }` shape at 115200 baud,
8 data bits, no parity, one stop bit; each JSON object ends in a newline.

| Writable pin | Accepted value |
| --- | --- |
| `symbols_csv` | 1–16 distinct comma-separated symbols |
| `refresh_s` | Integer 15–3,600 |
| `brightness` | Integer 0–15 |
| `dwell_s` | Integer 1–30 |
| `scroll_ms` | Integer 10–150 |

Quote pins (`symbol_N`, `price_N`, `change_pct_N`, `fresh_N`) are read-only.
Commands are HTTP `POST /command` on LAN or newline JSON on USB. Network APIs
are intended for the trusted local network; they do not implement the optional
IoT device-password protocol. The HTTPS bearer key authenticates quote fetches.

## Builds and verification

Build `pio run -e esp32 -e esp32-oled`. The matrix uses `min_spiffs.csv`;
OLED uses `partitions-oled.csv`. Back up existing flash/settings before changing
firmware or partitions. The `.bin` release assets are application images, not
complete flash backups; use PlatformIO upload for the correct partition layout.

`python scripts/hardware_smoke.py http://<device-ip>` temporarily changes the
watchlist and refresh interval, checks live quotes and validation, and restores
the originals in `finally`. Do not run it while someone is editing the device.

`esp32-oled-test` and `scripts/usb_http_bridge.py` provide localhost-only USB
HTTP diagnostics for isolated networks. Normal operation uses `esp32-oled`;
the diagnostic image is neither required nor published.
