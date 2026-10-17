# Changelog

## 1.1.0 — 2026-10-17

- Add an ESP32/SSD1306 128×64 OLED target on SDA 21/SCL 22, probing 0x3c/0x3d.
  Preserve the original OLED board's 4 MB partition offsets.
- Implement Rozakos Home mDNS discovery, identity, quote state and validated
  watchlist/refresh settings. Never return Wi-Fi credentials or the API key.
- Implement Rozakos IoT HTTP, WebSocket and newline JSON USB serial protocols,
  with five editable controls and up to 16 quote slots.
- Fetch up to 16 quotes in one HTTPS batch with CA verification after NTP sync;
  preserve last good values and fetch immediately after settings change.
- Keep telemetry responsive on its own task during blocking TLS requests.
- Add opt-in hardware smoke checks and a separate bench-only USB HTTP target.
- Build and archive both production targets in Jenkins; future tag builds
  publish separate application images without embedded credentials.

The OLED target was flashed and verified on an SSD1306 at 0x3c, including
LittleFS, reboot persistence, verified HTTPS quotes, mDNS and both apps' LAN
clients. Matrix and OLED targets compile; matrix hardware and Android USB serial
were not physically exercised in this session. See [integration notes](docs/integration.md).
