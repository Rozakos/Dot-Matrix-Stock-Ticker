#pragma once

#include <Arduino.h>

class SettingsStore;

namespace wifi_mgr {

// STA join progress.
//   Idle       — no join attempted yet (or no SSID configured)
//   Connecting — WiFi.begin() issued, waiting on the result (~15s window)
//   Connected  — got an IP
//   Failed     — attempt timed out / was rejected; see lastFailMessage()
enum class StaStatus { Idle, Connecting, Connected, Failed };

// If settings has a saved SSID, attempts STA connect (up to ~15s). On
// failure or when no SSID is configured, starts an open AP named
// Ticker-Setup-XXXX (last two MAC bytes) so the captive portal can run.
void begin(SettingsStore& settings);

// Try the configured creds again from STA mode. Called after the captive
// portal saves new credentials. Non-blocking; poll `connected()`.
void retrySta(SettingsStore& settings);

bool        connected();   // STA WL_CONNECTED
bool        apActive();    // soft-AP mode is up
StaStatus   staStatus();
const char* lastFailMessage();
String      ip();          // STA IP if connected, else AP IP
String      apSsid();      // SoftAP SSID currently advertised (empty if off)
String      apPass();      // SoftAP password (empty for open networks)

}  // namespace wifi_mgr
