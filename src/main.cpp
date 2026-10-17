#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp32-hal.h>   // disableCore0WDT
#include <time.h>

#include "config.h"
#ifdef DISPLAY_OLED
#include "display/oled_display.h"
#else
#include "display/parola_display.h"
#endif
#include "net/captive_portal.h"
#include "net/iot_bridge.h"
#include "net/quote_fetcher.h"
#include "net/quote_store.h"
#include "net/web_admin.h"
#include "net/wifi_mgr.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
constexpr const char* WIFI_SSID = "";
constexpr const char* WIFI_PASS = "";
constexpr const char* API_TOKEN_SEED = "";
#endif
#include "settings/settings_store.h"
#include "ticker/ticker.h"

namespace {

#ifdef DISPLAY_OLED
OledDisplay g_display;
#else
ParolaDisplay g_display;
#endif
SettingsStore g_settings;
QuoteStore    g_quoteStore;

// ---- DISPLAY task (core 1) -------------------------------------------------
// Owns all SPI/MD_Parola access. Validates wiring with a HELLO banner on
// boot, then drives a status banner until the first quotes land, then hands
// off to the symbol ticker.
void displayTask(void*) {
  g_display.begin();
  g_display.setBrightness(g_settings.brightness());
  g_display.setScrollSpeed(g_settings.scrollSpeedMs());

  // Wiring validation FIRST: scroll HELLO across both zones for a few seconds
  // so a miswired chain / wrong module type is obvious before anything else.
  g_display.setZone(IDisplay::ZONE_TOP,    "HELLO", true);
  g_display.setZone(IDisplay::ZONE_BOTTOM, "HELLO", true);
  uint32_t bootUntil = millis() + 4000;
  while (millis() < bootUntil) {
    g_display.animate();
    vTaskDelay(pdMS_TO_TICKS(5));
  }

  Ticker ticker(g_display, g_quoteStore, g_settings);

  enum class Mode { None, Setup, Pair, Connecting, Run };
  Mode mode = Mode::None;

  for (;;) {
    if (iot_bridge::pairingRemainingMs() > 110000 && !iot_bridge::setupCode().isEmpty()) {
      if (mode != Mode::Pair) {
        mode = Mode::Pair;
        g_display.setZone(IDisplay::ZONE_TOP, "IOT SETUP", false);
        g_display.setZone(IDisplay::ZONE_BOTTOM, iot_bridge::setupCode(), false);
      }
      g_display.animate();
    } else if (wifi_mgr::apActive()) {
      // First-run setup: tell the user which AP to join.
      if (mode != Mode::Setup) {
        mode = Mode::Setup;
        g_display.setZone(IDisplay::ZONE_TOP,    "WIFI", false);
        g_display.setZone(IDisplay::ZONE_BOTTOM,
                          String("JOIN ") + wifi_mgr::apSsid(), true);
      }
      g_display.animate();
    } else if (g_quoteStore.lastUpdate() == 0) {
      // Connected/connecting but no data yet.
      if (mode != Mode::Connecting) {
        mode = Mode::Connecting;
        g_display.setZone(IDisplay::ZONE_TOP,    "WIFI", false);
        g_display.setZone(IDisplay::ZONE_BOTTOM, "CONNECTING", true);
      }
      g_display.animate();
    } else {
      if (mode != Mode::Run) {
        mode = Mode::Run;
        ticker.begin();
      }
      ticker.tick();
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// ---- NET task (core 0) -----------------------------------------------------
// WiFi join, captive-portal setup, and the blocking HTTPS quote fetch loop.
void netTask(void*) {
  // TLS handshakes block this core for seconds, starving IDLE0 and tripping
  // the task watchdog. Detaching IDLE0 from the WDT is the canonical fix for
  // tasks that make blocking network calls outside loop().
  disableCore0WDT();

  wifi_mgr::begin(g_settings);

  bool ap_mode_was = wifi_mgr::apActive();
  if (ap_mode_was) captive_portal::begin(g_settings);

  bool sta_services_up = false;
  auto bringUpStaServices = [&] {
    if (sta_services_up) return;
    // NTP first — configTime() resets TZ to UTC internally, so our local TZ
    // must be applied AFTER it.
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    setenv("TZ", cfg::TIME_TZ, 1);
    tzset();
    web_admin::begin(&g_settings, &g_quoteStore);
    sta_services_up = true;
  };

  if (wifi_mgr::connected()) bringUpStaServices();

  uint32_t lastFetch  = 0;
  uint32_t settingsRevision = g_settings.revision();
  uint8_t  deadCycles = 0;   // consecutive fetch cycles with no HTTP response at all
  for (;;) {
    captive_portal::loop();

    if (wifi_mgr::connected()) {
      bringUpStaServices();
      if (settingsRevision != g_settings.revision()) {
        settingsRevision = g_settings.revision();
        lastFetch = 0;
      }
      if (lastFetch == 0 ||
          millis() - lastFetch > g_settings.refreshSeconds() * 1000UL) {
        if (fetcher::fetchQuotes(g_settings, g_quoteStore)) {
          lastFetch = millis();
        } else {
          // Retry sooner than a full interval after a failed batch.
          lastFetch = millis() - (g_settings.refreshSeconds() * 1000UL) + 5000;
        }
        // lwIP can wedge with WiFi still associated: every connect then times
        // out forever. Nothing but a reboot recovers it (same watchdog as
        // CYD-Stock-Ticker); any HTTP status, even 401, resets the count.
        if (fetcher::sawHttpResponseThisCycle()) {
          deadCycles = 0;
        } else if (++deadCycles >= 10) {
          log_e("[net] no HTTP response for %u fetch cycles; rebooting", deadCycles);
          delay(200);
          ESP.restart();
        }
      }
    } else if (wifi_mgr::apActive() && !ap_mode_was) {
      // Reconnect failed and we fell back to AP — re-arm the portal.
      ap_mode_was = true;
      captive_portal::begin(g_settings);
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(50);
  log_i("%s %s starting", cfg::DEVICE_NAME, cfg::FIRMWARE_VERSION);

  if (!LittleFS.begin(true)) {
    log_e("LittleFS mount failed");
  }
  g_settings.begin(API_TOKEN_SEED, WIFI_SSID, WIFI_PASS);
  g_quoteStore.begin();

  xTaskCreatePinnedToCore(displayTask, "disp", 6144,  nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(netTask,     "net",  12288, nullptr, 1, nullptr, 0);
}

void loop() {
  vTaskDelete(nullptr);
}
