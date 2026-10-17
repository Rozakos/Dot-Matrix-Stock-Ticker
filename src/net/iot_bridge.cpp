#include "iot_bridge.h"
#include <RozakosIoT.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <ESPAsyncWebServer.h>
#include "../config.h"
#include "../settings/settings_store.h"
#include "local_protocol.h"
#include "quote_store.h"
#include "web_admin_page.h"
#ifdef USB_HTTP_DIAGNOSTICS
#include <HTTPClient.h>
#include <WiFi.h>
#endif

namespace {
RozakosIoT iot;
SettingsStore* settings = nullptr;
QuoteStore* quotes = nullptr;

void ioTask(void*) {
  for (;;) {
    iot_bridge::loop();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

#ifdef USB_HTTP_DIAGNOSTICS
// Physical USB bench access to the actual HTTP handlers, never an arbitrary URL.
void diagnosticHttp(JsonVariantConst input) {
  const String path = input["path"] | "";
  const String method = input["method"] | "GET";
  const bool allowed = path == "/api/device-info" || path == "/api/state" ||
      path == "/api/settings" || path == "/info" || path == "/state" || path == "/command";
  JsonDocument result;
  auto response = result["http_response"].to<JsonObject>();
  if (path == "/ws-test") {
    WiFiClient peer;
    peer.setTimeout(2000);
    bool upgraded = peer.connect(WiFi.localIP(), 81);
    if (upgraded) {
      peer.print("GET / HTTP/1.1\r\nHost: ticker\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n");
      upgraded = peer.readStringUntil('\n').indexOf("101") >= 0;
      while (peer.connected() && peer.readStringUntil('\n') != "\r") {}
    }
    JsonDocument snapshot;
    auto received = snapshot.to<JsonObject>();
    const uint32_t deadline = millis() + 2000;
    while (upgraded && peer.connected() && millis() < deadline && received.size() < 69) {
      if (peer.available() < 2) { delay(1); continue; }
      const int opcode = peer.read() & 15;
      int length = peer.read() & 127;
      if (length == 126) { length = peer.read() << 8; length |= peer.read(); }
      if (length >= 512 || opcode != 1) break;
      char payload[512];
      if (peer.readBytes(payload, length) != size_t(length)) break;
      JsonDocument sample;
      if (!deserializeJson(sample, payload, length) && sample["pin"].is<const char*>())
        received[sample["pin"].as<String>()] = sample["value"];
    }
    peer.stop();
    response["status"] = upgraded && received.size() == 69 ? 200 : 502;
    response["body"]["upgraded"] = upgraded;
    response["body"]["samples"] = snapshot;
  } else if (!allowed || (method != "GET" && method != "POST")) {
    response["status"] = 400;
    response["body"]["error"] = "Unsupported diagnostic request";
  } else {
    HTTPClient http;
    http.setTimeout(2000);
    http.begin("http://" + WiFi.localIP().toString() + path);
    int code;
    if (method == "POST") {
      String body; serializeJson(input["body"], body);
      http.addHeader("Content-Type", "application/json");
      code = http.POST(body);
    } else code = http.GET();
    response["status"] = code > 0 ? code : 502;
    JsonDocument body;
    if (code > 0 && !deserializeJson(body, http.getString())) response["body"] = body;
    else response["body"]["error"] = "Device HTTP request failed";
    http.end();
  }
  serializeJson(result, Serial); Serial.println();
}
#endif

void state(JsonDocument& doc) {
  doc["symbols_csv"] = web_admin_page::symbols_csv(*settings);
  doc["refresh_s"] = settings->refreshSeconds();
  doc["brightness"] = settings->brightness();
  doc["dwell_s"] = settings->dwellSeconds();
  doc["scroll_ms"] = settings->scrollSpeedMs();
  const auto symbols = settings->symbols();
  const auto snapshot = quotes->snapshot();
  const uint32_t staleMs = max(300000UL, settings->refreshSeconds() * 4000UL);
  for (size_t i = 0; i < cfg::MAX_SYMBOLS; ++i) {
    String suffix = String(i);
    doc["symbol_" + suffix] = i < symbols.size() ? symbols[i] : "";
    doc["price_" + suffix] = "";
    doc["change_pct_" + suffix] = "";
    doc["fresh_" + suffix] = false;
    if (i >= symbols.size()) continue;
    for (const auto& quote : snapshot) {
      if (quote.symbol != symbols[i] || !quote.fresh) continue;
      doc["price_" + suffix] = quote.last;
      doc["change_pct_" + suffix] = quote.changePct;
      doc["fresh_" + suffix] = uint32_t(millis() - quote.updatedMs) <= staleMs;
      break;
    }
  }
}

// Validate before committing so a rejected command never changes settings.
const char* command(JsonVariantConst body, bool apply = true) {
  if (!body.is<JsonObjectConst>() || !body["pin"].is<const char*>()) return "Expected pin and value";
  String pin = body["pin"].as<String>();
  String csv = web_admin_page::symbols_csv(*settings);
  uint32_t refresh = settings->refreshSeconds();
  uint32_t dwell = settings->dwellSeconds();
  uint32_t scroll = settings->scrollSpeedMs();
  uint32_t brightness = settings->brightness();
  if (pin == "symbols_csv") {
    if (!body["value"].is<const char*>() || body["value"].as<String>().length() > 207)
      return "symbols_csv must be a comma-separated watchlist";
    String input = body["value"].as<String>();
    csv = "";
    std::vector<String> seen;
    int start = 0;
    do {
      int end = input.indexOf(',', start);
      if (end < 0) end = input.length();
      String symbol = input.substring(start, end);
      symbol.trim(); symbol.toUpperCase();
      if (!local_protocol::validSymbol(symbol) || seen.size() >= cfg::MAX_SYMBOLS)
        return "Use 1 to 16 valid symbols";
      for (const auto& previous : seen) if (previous == symbol) return "Duplicate symbol";
      seen.push_back(symbol);
      if (!csv.isEmpty()) csv += ',';
      csv += symbol;
      start = end + 1;
    } while (start <= int(input.length()));
  } else {
    if (!body["value"].is<uint32_t>()) return "Value must be an integer";
    uint32_t value = body["value"];
    if (pin == "refresh_s" && value >= 15 && value <= 3600) refresh = value;
    else if (pin == "brightness" && value <= 15) brightness = value;
    else if (pin == "dwell_s" && value >= 1 && value <= 30) dwell = value;
    else if (pin == "scroll_ms" && value >= 10 && value <= 150) scroll = value;
    else return "Unknown/read-only pin or value out of range";
  }
  if (apply) settings->update(settings->apiKey(), refresh, csv, dwell, scroll, brightness);
  return nullptr;
}

} // namespace

namespace iot_bridge {
void begin(AsyncWebServer& server, SettingsStore& settingsStore, QuoteStore& quoteStore) {
  settings = &settingsStore;
  quotes = &quoteStore;
  iot.identity(cfg::DEVICE_NAME, cfg::DEVICE_TYPE);
  JsonDocument snapshot;
  state(snapshot);
  for (JsonPair item : snapshot.as<JsonObject>()) iot.pin(item.key().c_str());
  iot.onSnapshot(state);
  auto apply = [](const String& pin, JsonVariantConst value, bool commit) -> const char* {
    JsonDocument doc; doc["pin"] = pin; doc["value"] = value;
    return command(doc.as<JsonVariantConst>(), commit);
  };
  iot.onCommand([apply](const String& pin, JsonVariantConst value) { return apply(pin, value, true); });
  iot.onValidate([apply](const String& pin, JsonVariantConst value) { return apply(pin, value, false); });
#ifdef USB_HTTP_DIAGNOSTICS
  iot.onSerialRequest([](JsonVariantConst input) {
    if (!input["http"].is<JsonObjectConst>()) return false;
    diagnosticHttp(input["http"]); return true;
  });
#endif
  iot.begin(&server, false); // Keep the ticker's existing Wi-Fi/Home settings.
  xTaskCreatePinnedToCore(ioTask, "iot", 16384, nullptr, 1, nullptr, 0);
}
void loop() { iot.loop(); }
String setupCode() { return iot.setupCode(); }
bool pairingActive() { return iot.pairingActive(); }
uint32_t pairingRemainingMs() { return iot.pairingRemainingMs(); }
} // namespace iot_bridge
