#include "quote_fetcher.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <time.h>
#include <vector>

#include "../config.h"
#include "../settings/settings_store.h"
#include "quote_store.h"
#include "root_ca.h"

namespace {

bool g_sawResponse = false;

bool fetchAndParse(const String& url, const String& token,
                   const JsonDocument& filter, JsonDocument& doc) {
  WiFiClientSecure client;
  client.setCACert(ROOT_CA);
  HTTPClient http;
  http.setTimeout(10000);
  http.setReuse(false);
  if (!http.begin(client, url)) return false;

  http.useHTTP10(true);                            // avoid chunked transfer
  http.addHeader("Accept-Encoding", "identity");   // avoid gzip/deflate
  // Cloudflare bot-fight rejects empty/default User-Agents — required.
  http.addHeader("User-Agent", cfg::API_USER_AGENT);
  if (token.length()) {
    http.addHeader("Authorization", String("Bearer ") + token);
  }

  int code = http.GET();
  if (code > 0) g_sawResponse = true;
  if (code != 200) {
    log_w("HTTP %d %s", code, url.c_str());
    http.end();
    return false;
  }

  DeserializationError err = deserializeJson(
      doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    log_w("json parse: %s", err.c_str());
    return false;
  }
  return true;
}

}  // namespace

namespace fetcher {

// One batch request (max 16) shares the server's quote cache and poller.
bool fetchQuotes(SettingsStore& settings, QuoteStore& store) {
  auto syms = settings.symbols();
  String token = settings.apiKey();
  if (syms.empty() || !token.length()) {
    // Nothing to fetch is a configuration state, not a dead network: report
    // it as "responded" so the watchdog never reboot-loops an unset device.
    g_sawResponse = true;
    if (!token.length()) log_w("no api token configured");
    return false;
  }

  // Certificate validity needs a real clock. NTP is started at WiFi connect;
  // waiting for it is not a network failure and must not trip the watchdog.
  if (time(nullptr) < 1700000000) {
    g_sawResponse = true;
    return false;
  }

  JsonDocument filter;
  auto fields = filter["quotes"][0].to<JsonObject>();
  for (const char* key : {"symbol", "last", "change_pct", "market_state",
                          "pre_market", "pre_market_change_pct",
                          "post_market", "post_market_change_pct"}) fields[key] = true;

  g_sawResponse = false;
  const std::vector<Quote> previous = store.snapshot();
  size_t fetched = 0;

  std::vector<Quote> out;
  out.reserve(syms.size());

  String url = String(cfg::API_BASE) + "/stocks?symbols=";
  for (size_t i = 0; i < syms.size() && i < cfg::MAX_SYMBOLS; ++i) {
    if (i) url += ',';
    String encoded = syms[i];
    encoded.replace("^", "%5E");
    url += encoded;
  }
  JsonDocument doc;
  const bool ok = fetchAndParse(url, token, filter, doc);

  for (const auto& sym : syms) {
    Quote q;
    q.symbol = sym;

    if (ok) for (JsonObject item : doc["quotes"].as<JsonArray>()) {
      if (item["symbol"].as<String>() != sym) continue;
      q.last      = item["last"].is<float>() ? item["last"].as<float>() : NAN;
      q.changePct = item["change_pct"].is<float>() ? item["change_pct"].as<float>() : NAN;
      q.fresh     = isfinite(q.last) && isfinite(q.changePct);
      q.marketState = item["market_state"] | "";
      const char* priceKey = q.marketState == "PRE" ? "pre_market" : "post_market";
      const char* changeKey = q.marketState == "PRE" ? "pre_market_change_pct" : "post_market_change_pct";
      q.extPrice = item[priceKey].is<float>() ? item[priceKey].as<float>() : NAN;
      q.extChangePct = item[changeKey].is<float>() ? item[changeKey].as<float>() : NAN;
      if (q.fresh) {
        q.updatedMs = millis();
        ++fetched;
        log_i("[%s] %.2f (%+.2f%%)", sym.c_str(), q.last, q.changePct);
      } else {
        log_w("[%s] missing last/change_pct", sym.c_str());
      }
      break;
    }
    if (!q.fresh) {
      // Keep the last good price rather than blanking it on a transient error.
      for (const auto& p : previous) {
        if (p.symbol == sym && p.fresh) { q = p; break; }
      }
    }
    out.push_back(q);
  }

  time_t now = fetched ? time(nullptr) : store.lastUpdate();
  store.setQuotes(std::move(out), now);
  return fetched > 0;
}

bool sawHttpResponseThisCycle() { return g_sawResponse; }

}  // namespace fetcher
