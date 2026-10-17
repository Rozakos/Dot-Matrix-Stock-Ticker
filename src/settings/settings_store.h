#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <vector>
#include <atomic>

class SettingsStore {
 public:
  // seedWifiSsid/seedWifiPass are written to LittleFS only on first boot
  // (or when /settings.json is absent). After that the captive portal /
  // web admin owns these.
  void begin(const char* seedKey,
             const char* seedWifiSsid = nullptr,
             const char* seedWifiPass = nullptr);

  String              apiKey() const;
  uint32_t            refreshSeconds() const;
  std::vector<String> symbols() const;

  // Display tuning — all live-editable from the web admin.
  uint32_t dwellSeconds() const;   // per-symbol dwell before advancing
  uint16_t scrollSpeedMs() const;  // MD_Parola frame delay (lower = faster)
  uint8_t  brightness() const;     // MAX7219 intensity 0..15
  uint32_t revision() const { return _revision.load(); }

  String   wifiSsid() const;
  String   wifiPass() const;

  // Atomic update from the web form. Persists to LittleFS.
  void update(const String& apiKey,
              uint32_t refreshSeconds,
              const String& symbolsCsv,
              uint32_t dwellSeconds,
              uint16_t scrollSpeedMs,
              uint8_t  brightness);

  // Captive portal saves the user's home WiFi here.
  void setWifi(const String& ssid, const String& pass);

 private:
  void load(const char* seedKey, const char* seedWifiSsid, const char* seedWifiPass);
  void save() const;

  mutable SemaphoreHandle_t _mu = nullptr;
  std::atomic<uint32_t> _revision{0};
  String   _apiKey;
  uint32_t _refresh    = 60;
  String   _symbolsCsv;
  uint32_t _dwell      = 3;
  uint16_t _scrollMs   = 30;
  uint8_t  _brightness = 6;
  String   _wifiSsid;
  String   _wifiPass;
};
