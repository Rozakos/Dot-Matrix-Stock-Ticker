#include "local_protocol.h"
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include "../config.h"
#include "../settings/settings_store.h"
#include "quote_store.h"
#include "web_admin_page.h"

namespace {
void sendJson(AsyncWebServerRequest* req, const JsonDocument& doc) {
  auto* response = req->beginResponseStream("application/json");
  response->addHeader("Cache-Control", "no-store");
  serializeJson(doc, *response);
  req->send(response);
}
void sendSettings(AsyncWebServerRequest* req, SettingsStore& settings) {
  JsonDocument doc;
  auto symbols = doc["symbols"].to<JsonArray>();
  for (const auto& symbol : settings.symbols()) symbols.add(symbol);
  doc["refresh_s"] = settings.refreshSeconds();
  doc["max_symbols"] = cfg::MAX_SYMBOLS;
  doc["has_api_key"] = !settings.apiKey().isEmpty();
  doc["brightness"] = settings.brightness();
  doc["dwell_s"] = settings.dwellSeconds();
  doc["scroll_ms"] = settings.scrollSpeedMs();
  sendJson(req, doc);
}
}

namespace local_protocol {
bool validSymbol(const String& symbol) {
  if (symbol.isEmpty() || symbol.length() > 12) return false;
  for (char c : symbol) {
    if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
          c == '.' || c == '-' || c == '^')) return false;
  }
  return true;
}

void begin(AsyncWebServer& server, SettingsStore& settings, QuoteStore& store) {
  String mac = WiFi.macAddress();
  String suffix = mac;
  suffix.replace(":", "");
  suffix.toLowerCase();
  const String id = String(cfg::DEVICE_PREFIX) + suffix;
  server.on("/api/device-info", HTTP_GET, [id, mac](AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["id"] = id;
    doc["name"] = cfg::DEVICE_NAME;
    doc["type"] = cfg::DEVICE_TYPE;
    doc["fw"] = cfg::FIRMWARE_VERSION;
    doc["mac"] = mac;
    doc["webUiPath"] = "/settings";
    auto caps = doc["capabilities"].to<JsonArray>();
    for (const char* cap : {"web_ui", "settings", "settings_api", "state"}) caps.add(cap);
    sendJson(req, doc);
  });
  server.on("/api/state", HTTP_GET, [&settings, &store](AsyncWebServerRequest* req) {
    JsonDocument doc;
    auto quotes = doc["quotes"].to<JsonArray>();
    const auto snapshot = store.snapshot();
    const uint32_t staleMs = max(300000UL, settings.refreshSeconds() * 4000UL);
    for (const auto& symbol : settings.symbols()) {
      auto item = quotes.add<JsonObject>();
      item["symbol"] = symbol;
      item["price"] = nullptr;
      item["change_pct"] = nullptr;
      item["fresh"] = false;
      for (const auto& quote : snapshot) {
        if (quote.symbol != symbol || !quote.fresh) continue;
        item["price"] = quote.last;
        item["change_pct"] = quote.changePct;
        item["fresh"] = uint32_t(millis() - quote.updatedMs) <= staleMs;
        if (!quote.marketState.isEmpty()) doc["market_state"] = quote.marketState;
        if (isfinite(quote.extPrice)) item["ext_price"] = quote.extPrice;
        if (isfinite(quote.extChangePct)) item["ext_change_pct"] = quote.extChangePct;
        break;
      }
    }
    doc["updated"] = store.lastUpdate();
    sendJson(req, doc);
  });
  server.on("/api/settings", HTTP_GET, [&settings](AsyncWebServerRequest* req) {
    sendSettings(req, settings);
  });
  auto* handler = new AsyncCallbackJsonWebHandler("/api/settings",
      [&settings](AsyncWebServerRequest* req, JsonVariant& body) {
    auto error = [&](const char* message) {
      JsonDocument doc;
      doc["error"] = message;
      String json;
      serializeJson(doc, json);
      req->send(400, "application/json", json);
    };
    if (!body.is<JsonObject>()) { error("Expected a JSON object"); return; }
    auto obj = body.as<JsonObject>();
    String csv = web_admin_page::symbols_csv(settings);
    if (obj["symbols"].isUnbound() == false) {
      if (!obj["symbols"].is<JsonArray>()) { error("symbols must be an array"); return; }
      auto symbols = obj["symbols"].as<JsonArray>();
      if (symbols.size() < 1 || symbols.size() > cfg::MAX_SYMBOLS) {
        error("Use 1 to 16 symbols"); return;
      }
      csv = "";
      std::vector<String> seen;
      for (JsonVariant value : symbols) {
        if (!value.is<const char*>()) { error("Invalid symbol"); return; }
        String symbol = value.as<String>();
        symbol.trim(); symbol.toUpperCase();
        if (!validSymbol(symbol)) { error("Invalid symbol"); return; }
        for (const auto& previous : seen) {
          if (previous == symbol) { error("Duplicate symbol"); return; }
        }
        seen.push_back(symbol);
        if (!csv.isEmpty()) csv += ',';
        csv += symbol;
      }
    }
    uint32_t refresh = settings.refreshSeconds();
    if (!obj["refresh_s"].isUnbound()) {
      if (!obj["refresh_s"].is<uint32_t>() || obj["refresh_s"].as<uint32_t>() < 15 ||
          obj["refresh_s"].as<uint32_t>() > 3600) {
        error("refresh_s must be an integer from 15 to 3600"); return;
      }
      refresh = obj["refresh_s"];
    }
    String token = settings.apiKey();
    if (!obj["api_key"].isUnbound()) {
      if (!obj["api_key"].is<const char*>() || obj["api_key"].as<String>().length() > 256) {
        error("Invalid api_key"); return;
      }
      token = obj["api_key"].as<String>();
      token.trim();
    }
    settings.update(token, refresh, csv, settings.dwellSeconds(),
                    settings.scrollSpeedMs(), settings.brightness());
    sendSettings(req, settings);
  });
  handler->setMethod(HTTP_POST);
  handler->setMaxContentLength(2048);
  server.addHandler(handler);
  if (MDNS.begin(id.c_str())) {
    MDNS.addService("rozakos", "tcp", 80);
    MDNS.addServiceTxt("rozakos", "tcp", "id", id);
    MDNS.addServiceTxt("rozakos", "tcp", "type", cfg::DEVICE_TYPE);
    MDNS.addServiceTxt("rozakos", "tcp", "name", cfg::DEVICE_NAME);
    MDNS.addServiceTxt("rozakos", "tcp", "fw", cfg::FIRMWARE_VERSION);
    MDNS.addServiceTxt("rozakos", "tcp", "mac", mac);
    MDNS.addServiceTxt("rozakos", "tcp", "path", "/api/device-info");
  }
}
}
