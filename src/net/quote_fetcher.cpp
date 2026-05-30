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

namespace {

bool fetchAndParse(const String& url, const String& token,
                   const JsonDocument& filter, JsonDocument& doc) {
  WiFiClientSecure client;
  client.setInsecure();              // server cert not pinned on-device
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

// GET /stock/{symbol} -> { last, change_pct, ... }
bool fetchQuotes(SettingsStore& settings, QuoteStore& store) {
  auto syms = settings.symbols();
  if (syms.empty()) return false;
  String token = settings.apiKey();
  if (!token.length()) {
    log_w("no api token configured");
    return false;
  }

  JsonDocument filter;
  filter["last"]       = true;
  filter["change_pct"] = true;

  std::vector<Quote> out;
  out.reserve(syms.size());

  for (const auto& sym : syms) {
    Quote q;
    q.symbol = sym;

    String url = String(cfg::API_BASE) + "/stock/" + sym;
    JsonDocument doc;
    if (fetchAndParse(url, token, filter, doc)) {
      q.last      = doc["last"]       | NAN;
      q.changePct = doc["change_pct"] | NAN;
      q.fresh     = !isnan(q.last) && !isnan(q.changePct);
      if (q.fresh) {
        log_i("[%s] %.2f (%+.2f%%)", sym.c_str(), q.last, q.changePct);
      } else {
        log_w("[%s] missing last/change_pct", sym.c_str());
      }
    }
    out.push_back(q);
    // Gentle on the API, and a yield point so a long symbol list never
    // starves the rest of the NET task between blocking TLS round-trips.
    vTaskDelay(pdMS_TO_TICKS(150));
  }

  time_t now;
  time(&now);
  store.setQuotes(std::move(out), now);
  return true;
}

}  // namespace fetcher
