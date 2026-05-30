#include "settings_store.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

#include "../config.h"
#include "../storage/fs_lock.h"

namespace {

std::vector<String> split_csv(const String& s) {
  std::vector<String> out;
  int start = 0;
  while (start < (int)s.length()) {
    int comma = s.indexOf(',', start);
    if (comma < 0) comma = s.length();
    String tok = s.substring(start, comma);
    tok.trim();
    tok.toUpperCase();
    if (tok.length()) out.push_back(tok);
    start = comma + 1;
  }
  return out;
}

uint32_t clamp_u32(uint32_t v, uint32_t lo, uint32_t hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

}  // namespace

void SettingsStore::begin(const char* seedKey,
                          const char* seedWifiSsid,
                          const char* seedWifiPass) {
  _mu = xSemaphoreCreateMutex();
  load(seedKey, seedWifiSsid, seedWifiPass);
}

void SettingsStore::load(const char* seedKey,
                         const char* seedWifiSsid,
                         const char* seedWifiPass) {
  bool needSave = false;
  auto defaults = [&] {
    _apiKey     = seedKey ? seedKey : "";
    _refresh    = cfg::DEFAULT_REFRESH_SECONDS;
    _symbolsCsv = cfg::DEFAULT_SYMBOLS;
    _dwell      = cfg::DEFAULT_DWELL_SECONDS;
    _scrollMs   = cfg::DEFAULT_SCROLL_MS;
    _brightness = cfg::DEFAULT_BRIGHTNESS;
    _wifiSsid   = seedWifiSsid ? seedWifiSsid : "";
    _wifiPass   = seedWifiPass ? seedWifiPass : "";
  };

  fs_lock::Guard g;
  if (!LittleFS.exists(cfg::SETTINGS_PATH)) {
    defaults();
    needSave = true;
  } else {
    File f = LittleFS.open(cfg::SETTINGS_PATH, "r");
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
      defaults();
      needSave = true;
    } else {
      _apiKey     = doc["api_key"]    | (seedKey ? seedKey : "");
      _refresh    = doc["refresh_s"]  | cfg::DEFAULT_REFRESH_SECONDS;
      _symbolsCsv = doc["symbols"]    | cfg::DEFAULT_SYMBOLS;
      _dwell      = doc["dwell_s"]    | cfg::DEFAULT_DWELL_SECONDS;
      _scrollMs   = doc["scroll_ms"]  | cfg::DEFAULT_SCROLL_MS;
      _brightness = doc["brightness"] | cfg::DEFAULT_BRIGHTNESS;
      _wifiSsid   = doc["wifi_ssid"]  | (seedWifiSsid ? seedWifiSsid : "");
      _wifiPass   = doc["wifi_pass"]  | (seedWifiPass ? seedWifiPass : "");
    }
  }

  if (_refresh < cfg::MIN_REFRESH_SECONDS) _refresh = cfg::MIN_REFRESH_SECONDS;
  _dwell      = clamp_u32(_dwell, cfg::MIN_DWELL_SECONDS, cfg::MAX_DWELL_SECONDS);
  _scrollMs   = (uint16_t)clamp_u32(_scrollMs, cfg::MIN_SCROLL_MS, cfg::MAX_SCROLL_MS);
  _brightness = (uint8_t)clamp_u32(_brightness, 0, cfg::MAX_BRIGHTNESS);
  if (needSave) save();
}

void SettingsStore::save() const {
  JsonDocument doc;
  doc["api_key"]    = _apiKey;
  doc["refresh_s"]  = _refresh;
  doc["symbols"]    = _symbolsCsv;
  doc["dwell_s"]    = _dwell;
  doc["scroll_ms"]  = _scrollMs;
  doc["brightness"] = _brightness;
  doc["wifi_ssid"]  = _wifiSsid;
  doc["wifi_pass"]  = _wifiPass;

  fs_lock::Guard g;
  File f = LittleFS.open(cfg::SETTINGS_PATH, "w");
  serializeJson(doc, f);
  f.close();
}

String SettingsStore::apiKey() const {
  xSemaphoreTake(_mu, portMAX_DELAY);
  String v = _apiKey;
  xSemaphoreGive(_mu);
  return v;
}

uint32_t SettingsStore::refreshSeconds() const {
  xSemaphoreTake(_mu, portMAX_DELAY);
  uint32_t v = _refresh;
  xSemaphoreGive(_mu);
  return v;
}

std::vector<String> SettingsStore::symbols() const {
  xSemaphoreTake(_mu, portMAX_DELAY);
  String csv = _symbolsCsv;
  xSemaphoreGive(_mu);
  return split_csv(csv);
}

uint32_t SettingsStore::dwellSeconds() const {
  xSemaphoreTake(_mu, portMAX_DELAY);
  uint32_t v = _dwell;
  xSemaphoreGive(_mu);
  return v;
}

uint16_t SettingsStore::scrollSpeedMs() const {
  xSemaphoreTake(_mu, portMAX_DELAY);
  uint16_t v = _scrollMs;
  xSemaphoreGive(_mu);
  return v;
}

uint8_t SettingsStore::brightness() const {
  xSemaphoreTake(_mu, portMAX_DELAY);
  uint8_t v = _brightness;
  xSemaphoreGive(_mu);
  return v;
}

String SettingsStore::wifiSsid() const {
  xSemaphoreTake(_mu, portMAX_DELAY);
  String v = _wifiSsid;
  xSemaphoreGive(_mu);
  return v;
}

String SettingsStore::wifiPass() const {
  xSemaphoreTake(_mu, portMAX_DELAY);
  String v = _wifiPass;
  xSemaphoreGive(_mu);
  return v;
}

void SettingsStore::setWifi(const String& ssid, const String& pass) {
  xSemaphoreTake(_mu, portMAX_DELAY);
  _wifiSsid = ssid;
  _wifiPass = pass;
  save();
  xSemaphoreGive(_mu);
}

void SettingsStore::update(const String& apiKey,
                           uint32_t refreshSeconds,
                           const String& symbolsCsv,
                           uint32_t dwellSeconds,
                           uint16_t scrollSpeedMs,
                           uint8_t  brightness) {
  xSemaphoreTake(_mu, portMAX_DELAY);
  _apiKey     = apiKey;
  _refresh    = refreshSeconds < cfg::MIN_REFRESH_SECONDS
                    ? cfg::MIN_REFRESH_SECONDS
                    : refreshSeconds;
  _symbolsCsv = symbolsCsv;
  _dwell      = clamp_u32(dwellSeconds, cfg::MIN_DWELL_SECONDS, cfg::MAX_DWELL_SECONDS);
  _scrollMs   = (uint16_t)clamp_u32(scrollSpeedMs, cfg::MIN_SCROLL_MS, cfg::MAX_SCROLL_MS);
  _brightness = (uint8_t)clamp_u32(brightness, 0, cfg::MAX_BRIGHTNESS);
  save();
  xSemaphoreGive(_mu);
}
