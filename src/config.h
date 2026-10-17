#pragma once

#include <cstdint>

namespace cfg {

// Self-hosted yfinance proxy at rozakos.eu. Bearer-token auth; Cloudflare
// bot-fight blocks empty/default User-Agents, so requests MUST send a
// non-empty User-Agent — see API_USER_AGENT.
inline constexpr const char* API_BASE       = "https://rozakos.eu/stocks/api/v1";
inline constexpr const char* FIRMWARE_VERSION = "1.2.0";
#ifdef DISPLAY_OLED
inline constexpr const char* DEVICE_NAME = "OLED Stock Ticker";
inline constexpr const char* DEVICE_TYPE = "oled_stock_ticker";
inline constexpr const char* DEVICE_PREFIX = "oled-ticker-";
#else
inline constexpr const char* DEVICE_NAME = "Dot-Matrix Stock Ticker";
inline constexpr const char* DEVICE_TYPE = "dot_matrix_stock_ticker";
inline constexpr const char* DEVICE_PREFIX = "dot-ticker-";
#endif
inline constexpr const char* API_USER_AGENT = "DotMatrix-Stock-Ticker/1.1 (ESP32)";
inline constexpr size_t MAX_SYMBOLS = 16;
// Eastern European time with DST: UTC+2 winter, UTC+3 summer.
inline constexpr const char* TIME_TZ        = "EET-2EEST,M3.5.0/3,M10.5.0/4";

#ifdef DISPLAY_OLED
inline constexpr const char* DEFAULT_SYMBOLS = "NVDA,INTC,AMD";
#else
inline constexpr const char* DEFAULT_SYMBOLS = "AAPL,MSFT,NVDA,TSLA";
#endif

inline constexpr uint32_t DEFAULT_REFRESH_SECONDS = 20;
inline constexpr uint32_t MIN_REFRESH_SECONDS     = 15;

// Per-symbol dwell before the ticker advances to the next symbol.
inline constexpr uint32_t DEFAULT_DWELL_SECONDS = 3;
inline constexpr uint32_t MIN_DWELL_SECONDS     = 1;
inline constexpr uint32_t MAX_DWELL_SECONDS     = 30;

// MD_Parola scroll animation frame delay (ms). Lower = faster scroll.
inline constexpr uint16_t DEFAULT_SCROLL_MS = 30;
inline constexpr uint16_t MIN_SCROLL_MS     = 10;
inline constexpr uint16_t MAX_SCROLL_MS     = 150;

// MAX7219 intensity (0..15).
inline constexpr uint8_t DEFAULT_BRIGHTNESS = 6;
inline constexpr uint8_t MAX_BRIGHTNESS     = 15;

inline constexpr const char* SETTINGS_PATH = "/settings.json";

// ---- Display hardware (confirmed) -----------------------------------------
// 8x MAX7219 8x8 modules, FC-16 type, ONE daisy chain over the ESP32 default
// hardware-SPI (VSPI) bus: SCK=GPIO18 (CLK), MOSI=GPIO23 (DIN). Only CS is a
// free choice; no MISO is used. Wiring (confirmed): DIN enters the top-left
// module; modules 0-3 = top row left->right, modules 4-7 = bottom row
// left->right (no serpentine, no 180 flip). Two MD_Parola zones map directly:
//   zone 0 (top)    -> modules 0-3 -> symbol
//   zone 1 (bottom) -> modules 4-7 -> price/change
inline constexpr uint8_t DISPLAY_CS_PIN      = 5;
inline constexpr uint8_t DISPLAY_MAX_DEVICES = 8;

}  // namespace cfg
