#include "RozakosIoT.h"
#include "RozakosRoots.h"
#include <AsyncJson.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <esp_system.h>
#include <time.h>
#include <mbedtls/sha256.h>

namespace {
constexpr const char* HOST = "iot.rozakos.eu";
String randomHex() {
  char out[65];
  for (size_t i = 0; i < 8; ++i) snprintf(out + i * 8, 9, "%08x", esp_random());
  return String(out);
}
String sha256Hex(const String& value) {
  uint8_t digest[32];
  mbedtls_sha256(reinterpret_cast<const unsigned char*>(value.c_str()), value.length(), digest, 0);
  char out[65];
  for (size_t i = 0; i < 32; ++i) snprintf(out + i * 2, 3, "%02x", digest[i]);
  return String(out);
}
bool sameSecret(const String& a, const String& b) {
  if (a.length() != b.length()) return false;
  uint8_t diff = 0;
  for (size_t i = 0; i < a.length(); ++i) diff |= uint8_t(a[i]) ^ uint8_t(b[i]);
  return diff == 0;
}
bool validPin(const String& pin) { return pin.length() > 0 && pin.length() <= 64; }
bool primitive(JsonVariantConst value) {
  return value.is<bool>() || value.is<const char*>() || value.is<double>();
}
void jsonReply(AsyncWebServerRequest* req, JsonDocument& doc, int status = 200) {
  auto* response = req->beginResponseStream("application/json");
  response->setCode(status); response->addHeader("Cache-Control", "no-store");
  serializeJson(doc, *response); req->send(response);
}
}

void RozakosIoT::identity(const char* name, const char* type) { _name = name; _type = type; }
void RozakosIoT::pin(const char* name, Writer writer) {
  if (!validPin(name) || _pins.size() >= 128) return;
  for (auto& item : _pins) if (item.name == name) { item.write = writer; return; }
  _pins.push_back({name, writer});
}
void RozakosIoT::snapshot(JsonDocument& doc) {
  if (_snapshot) _snapshot(doc);
  else { xSemaphoreTake(_mutex, portMAX_DELAY); doc.set(_values); xSemaphoreGive(_mutex); }
}
const char* RozakosIoT::execute(const String& name, JsonVariantConst value) {
  if (!validPin(name) || !primitive(value)) return "Expected a pin and primitive value";
  const char* error = "Unknown or read-only pin";
  if (_command) error = _command(name, value);
  else for (auto& item : _pins) if (item.name == name && item.write) { error = item.write(value); break; }
  if (!error) { xSemaphoreTake(_mutex, portMAX_DELAY); _values[name] = value; xSemaphoreGive(_mutex); }
  return error;
}
bool RozakosIoT::save() {
  JsonDocument doc;
  doc["key"] = _key; doc["deviceId"] = _deviceId;
  doc["ticket"] = _ticket; doc["receipt"] = _receipt;
  doc["ssid"] = _ssid; doc["password"] = _password;
  String json; serializeJson(doc, json);
  return _prefs.putString("config", json) == json.length();
}
void RozakosIoT::openPairing() {
  _badCodes = 0; _pairUntil = millis() + 120000;
  if (_manageWifi && _http) {
    WiFi.mode(WIFI_AP_STA);
    String ap = "Rozakos-Setup-" + _hardwareId.substring(_hardwareId.length() - 4);
    String password = "Rozakos-" + _code;
    WiFi.softAP(ap.c_str(), password.c_str());
  }
  Serial.printf("[Rozakos] Setup code: %s (2 minutes; hold BOOT for 3 seconds to reopen)\n", _code.c_str());
}
bool RozakosIoT::authorize(AsyncWebServerRequest* req, JsonVariantConst body) {
  if (!pairingActive() || _badCodes >= 5 || !body["code"].is<const char*>() || body["code"].as<String>() != _code) {
    ++_badCodes;
    req->send(403, "application/json", "{\"error\":\"Hold BOOT for 3 seconds and enter the displayed setup code.\"}");
    return false;
  }
  return true;
}
bool RozakosIoT::localAllowed(AsyncWebServerRequest* req) {
  if (_localSecret.isEmpty()) return true;
  if (req->hasHeader("X-Device-Password") && sameSecret(req->header("X-Device-Password"), _localSecret)) return true;
  req->send(401, "application/json", "{\"error\":\"Device password required\"}");
  return false;
}
void RozakosIoT::begin(AsyncWebServer* existingServer, bool manageWifi) {
  _manageWifi = manageWifi;
  if (_manageWifi) WiFi.mode(WIFI_AP_STA); // Initialize lwIP before either HTTP/WebSocket listener.
  _mutex = xSemaphoreCreateMutex();
  _prefs.begin("rozakos-iot", false);
  JsonDocument stored;
  deserializeJson(stored, _prefs.getString("config", "{}"));
  _key = stored["key"] | ""; _deviceId = stored["deviceId"] | "";
  _ticket = stored["ticket"] | ""; _receipt = stored["receipt"] | "";
  _ssid = stored["ssid"] | ""; _password = stored["password"] | "";
  if (!_ticket.isEmpty()) _job = "pending";
  char suffix[17]; snprintf(suffix, sizeof(suffix), "%012llx", ESP.getEfuseMac());
  _hardwareId = String("esp32-") + suffix;
  char code[9]; snprintf(code, sizeof(code), "%08u", esp_random() % 100000000);
  _code = code;
  pinMode(0, INPUT_PULLUP);
  if (_key.isEmpty()) openPairing();
  _http = existingServer ? existingServer : new AsyncWebServer(80);
  _http->on("/info", HTTP_GET, [this](AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["name"] = _name; doc["type"] = _type; doc["hardwareId"] = _hardwareId;
    doc["protocol"] = 1; doc["sdk"] = "0.2.1";
    auto pins = doc["pins"].to<JsonArray>(); for (const auto& item : _pins) pins.add(item.name);
    auto caps = doc["capabilities"].to<JsonArray>(); caps.add("enrollment"); caps.add("device-proof"); caps.add("local"); caps.add("cloud");
    if (!_localSecret.isEmpty()) caps.add("local-password");
    jsonReply(req, doc);
  });
  _http->on("/state", HTTP_GET, [this](AsyncWebServerRequest* req) {
    if (!localAllowed(req)) return;
    // Read the last published immutable snapshot, not application callbacks on the HTTP task.
    JsonDocument doc;
    xSemaphoreTake(_mutex, portMAX_DELAY); doc.set(_values); xSemaphoreGive(_mutex);
    jsonReply(req, doc);
  });
  auto* command = new AsyncCallbackJsonWebHandler("/command", [this](AsyncWebServerRequest* req, JsonVariant& body) {
    if (!localAllowed(req)) return;
    if (!body["pin"].is<const char*>() || !validPin(body["pin"].as<String>()) || !primitive(body["value"])) { req->send(400, "application/json", "{\"error\":\"Invalid command\"}"); return; }
    if (_validate) {
      const char* error = _validate(body["pin"].as<String>(), body["value"]);
      if (error) { JsonDocument doc; doc["error"] = error; jsonReply(req, doc, 400); return; }
    } else if (!_command) {
      bool writable = false;
      for (const auto& item : _pins) if (item.name == body["pin"].as<String>() && item.write) writable = true;
      if (!writable) { req->send(400, "application/json", "{\"error\":\"Unknown or read-only pin\"}"); return; }
    }
    String json; serializeJson(body["value"], json);
    xSemaphoreTake(_mutex, portMAX_DELAY);
    const bool full = _commands.size() >= 8;
    if (!full) _commands.push_back({body["pin"].as<String>(), json});
    xSemaphoreGive(_mutex);
    req->send(full ? 429 : 202, "application/json", full ? "{\"error\":\"Device busy\"}" : "{\"queued\":true}");
  });
  command->setMethod(HTTP_POST); command->setMaxContentLength(512); _http->addHandler(command);
  _http->on("/iot/setup", HTTP_GET, [this](AsyncWebServerRequest* req) {
    JsonDocument doc;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    doc["hardwareId"] = _hardwareId; doc["paired"] = !_key.isEmpty(); doc["connected"] = _welcomed;
    doc["pairingActive"] = pairingActive(); doc["job"] = _job; doc["error"] = _error;
    xSemaphoreGive(_mutex); jsonReply(req, doc);
  });
  // Step 1: the app (holding the physical code) asks for the hash of a fresh
  // device-generated receipt and binds it to the ticket over HTTPS. Only the
  // device knows the preimage, so a ticket copied off the LAN cannot be claimed.
  auto* receipt = new AsyncCallbackJsonWebHandler("/iot/receipt", [this](AsyncWebServerRequest* req, JsonVariant& body) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    if (!authorize(req, body)) { xSemaphoreGive(_mutex); return; }
    if (!_key.isEmpty() || _job == "pending") { xSemaphoreGive(_mutex); req->send(409, "application/json", "{\"error\":\"Already linked or busy\"}"); return; }
    _receipt = randomHex(); _ticket = ""; _job = "idle"; _error = "";
    JsonDocument doc; doc["receiptHash"] = sha256Hex(_receipt);
    xSemaphoreGive(_mutex); jsonReply(req, doc);
  });
  receipt->setMethod(HTTP_POST); receipt->setMaxContentLength(256); _http->addHandler(receipt);
  // Step 2: the ticket bound to that receipt.
  auto* enroll = new AsyncCallbackJsonWebHandler("/iot/enroll", [this](AsyncWebServerRequest* req, JsonVariant& body) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    if (!authorize(req, body)) { xSemaphoreGive(_mutex); return; }
    String token = body["token"] | "";
    bool valid = token.length() == 64;
    for (char c : token) if (!isxdigit(c) || (c >= 'A' && c <= 'F')) valid = false;
    if (!_key.isEmpty() || _job == "pending" || !valid) { xSemaphoreGive(_mutex); req->send(409, "application/json", "{\"error\":\"Already linked, busy, or invalid ticket\"}"); return; }
    if (_receipt.isEmpty()) { xSemaphoreGive(_mutex); req->send(409, "application/json", "{\"error\":\"Setup receipt missing or reset. Start linking again.\"}"); return; }
    _ticket = token; _job = "pending"; _error = ""; _lastClaim = 0;
    xSemaphoreGive(_mutex); req->send(202, "application/json", "{\"queued\":true}");
  });
  enroll->setMethod(HTTP_POST); enroll->setMaxContentLength(256); _http->addHandler(enroll);
  auto* wifi = new AsyncCallbackJsonWebHandler("/iot/wifi", [this](AsyncWebServerRequest* req, JsonVariant& body) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    if (!authorize(req, body)) { xSemaphoreGive(_mutex); return; }
    String ssid = body["ssid"] | "", password = body["password"] | "";
    if (!_manageWifi || ssid.isEmpty() || ssid.length() > 32 || password.length() > 63) { xSemaphoreGive(_mutex); req->send(400, "application/json", "{\"error\":\"Invalid Wi-Fi settings\"}"); return; }
    _ssid = ssid; _password = password; _wifiPending = true; _wifiApplyAt = millis() + 750;
    xSemaphoreGive(_mutex); req->send(202, "application/json", "{\"queued\":true}");
  });
  wifi->setMethod(HTTP_POST); wifi->setMaxContentLength(256); _http->addHandler(wifi);
  _local.onEvent([this](AsyncWebSocket*, AsyncWebSocketClient* peer, AwsEventType type, void*, uint8_t*, size_t) {
    if (type != WS_EVT_CONNECT) return;
    JsonDocument doc;
    xSemaphoreTake(_mutex, portMAX_DELAY); doc.set(_values); xSemaphoreGive(_mutex);
    for (JsonPair item : doc.as<JsonObject>()) {
      JsonDocument frame; frame["pin"] = item.key().c_str(); frame["value"] = item.value();
      String json; serializeJson(frame, json); peer->text(json);
    }
  });
  _local.setFilter([this](AsyncWebServerRequest* req) {
    return _localSecret.isEmpty() || (req->hasParam("token") && sameSecret(req->getParam("token")->value(), _localSecret));
  });
  _wsServer.addHandler(&_local); _wsServer.begin();
  _cloud.onEvent([this](WStype_t type, uint8_t* payload, size_t length) {
    if (type == WStype_DISCONNECTED) { _welcomed = false; return; }
    if (type != WStype_TEXT || length > 16384) return;
    JsonDocument doc;
    if (deserializeJson(doc, payload, length)) return;
    if (doc["type"] == "WELCOME") { _welcomed = true; _lastPush = 0; }
    if (doc["type"] == "COMMAND") {
      const char* error = execute(doc["pin"] | "", doc["value"]);
      JsonDocument ack; ack["type"] = "COMMAND_RESULT"; ack["pin"] = doc["pin"]; ack["ok"] = !error;
      if (error) ack["error"] = error;
      String json; serializeJson(ack, json); _cloud.sendTXT(json); _lastPush = 0;
    }
  });
  if (_manageWifi) {
    WiFi.mode(WIFI_AP_STA);
    if (!_ssid.isEmpty()) WiFi.begin(_ssid.c_str(), _password.c_str());
    String ap = "Rozakos-Setup-" + _hardwareId.substring(_hardwareId.length() - 4);
    String apPassword = "Rozakos-" + _code;
    WiFi.softAP(ap.c_str(), apPassword.c_str());
    Serial.printf("[Rozakos] Wi-Fi setup: %s, password %s, http://192.168.4.1\n", ap.c_str(), apPassword.c_str());
    _http->on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
      req->send(200, "text/html", R"HTML(<!doctype html><meta name="viewport" content="width=device-width"><title>Rozakos setup</title><style>body{font:18px sans-serif;max-width:420px;margin:40px auto;padding:20px;color:#2c2c3e}input,button{box-sizing:border-box;width:100%;padding:14px;margin:8px 0}button{background:#a5211e;color:white;border:0}</style><h1>Connect your device</h1><p>Enter a 2.4 GHz Wi-Fi network, then return your phone to that network and add the device in Rozakos IoT.</p><form id="f"><input id="ssid" placeholder="Wi-Fi name" required maxlength="32"><input id="password" type="password" placeholder="Wi-Fi password" maxlength="63"><input id="code" inputmode="numeric" placeholder="8-digit setup code" required maxlength="8"><button>Connect Wi-Fi</button></form><p id="message"></p><script>f.onsubmit=async e=>{e.preventDefault();try{let r=await fetch('/iot/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:ssid.value,password:password.value,code:code.value})});let j=await r.json();message.textContent=r.ok?'Settings received. Rejoin your home Wi-Fi and discover this device.':j.error}catch{message.textContent='Connection lost. Check Wi-Fi and try again.'}}</script>)HTML");
    });
    if (MDNS.begin(_hardwareId.c_str())) MDNS.addService("rozakos-iot", "tcp", 80);
  }
  if (!existingServer) _http->begin();
}
void RozakosIoT::startClaim() {
  JsonDocument doc;
  xSemaphoreTake(_mutex, portMAX_DELAY);
  if (_claim || _job != "pending" || _ticket.isEmpty()) { xSemaphoreGive(_mutex); return; }
  if (!save()) { _error = "Could not persist enrollment. Retry after checking device storage."; _job = "error"; xSemaphoreGive(_mutex); return; }
  doc["hardwareId"] = _hardwareId; doc["deviceType"] = _type;
  doc["token"] = _ticket; doc["receipt"] = _receipt;
  xSemaphoreGive(_mutex);
  auto pins = doc["pins"].to<JsonArray>(); for (const auto& item : _pins) pins.add(item.name);
  _claim = new Claim();
  serializeJson(doc, _claim->body);
  if (xTaskCreate(claimTask, "rozakos-claim", 12288, _claim, 1, nullptr) != pdPASS) {
    delete _claim; _claim = nullptr;
    xSemaphoreTake(_mutex, portMAX_DELAY); _error = "Device busy; retrying enrollment."; xSemaphoreGive(_mutex);
  }
}
void RozakosIoT::claimTask(void* argument) {
  auto* claim = static_cast<Claim*>(argument);
  {
    WiFiClientSecure tls; tls.setCACert(ROZAKOS_ROOTS);
    HTTPClient http; http.setTimeout(8000); http.setConnectTimeout(8000);
    http.begin(tls, String("https://") + HOST + "/api/v1/devices/enrollments/claim");
    http.addHeader("Content-Type", "application/json"); http.setUserAgent("RozakosIoT/0.2 ESP32");
    claim->status = http.POST(claim->body);
    if (claim->status > 0) claim->response = http.getString();
    http.end();
  } // TLS and HTTP objects are destroyed before the task deletes itself.
  claim->done.store(true);
  vTaskDelete(nullptr);
}
void RozakosIoT::finishClaim() {
  Claim* claim = _claim; _claim = nullptr;
  const int status = claim->status;
  JsonDocument reply;
  if (status > 0) deserializeJson(reply, claim->response);
  delete claim;
  xSemaphoreTake(_mutex, portMAX_DELAY);
  if (_job != "pending") { xSemaphoreGive(_mutex); return; } // Cleared by BOOT/USB reset meanwhile.
  if (status == 200 && reply["data"]["apiKey"].is<const char*>()) {
    _key = reply["data"]["apiKey"].as<String>(); _deviceId = reply["data"]["deviceId"].as<String>();
    String oldTicket = _ticket; _ticket = "";
    if (save()) { _job = "complete"; _pairUntil = 0; _error = ""; }
    else { _key = ""; _deviceId = ""; _ticket = oldTicket; _error = "Storage failed; retrying enrollment."; }
  } else if (status >= 400 && status < 500 && status != 408 && status != 429) {
    _error = reply["error"] | "Enrollment rejected. Start again in the app.";
    _job = "error"; _ticket = ""; _receipt = ""; save();
  } else _error = status == 429 ? "Cloud busy; retrying with the same enrollment." : "Cloud unavailable; retrying with the same enrollment.";
  xSemaphoreGive(_mutex);
}
void RozakosIoT::serialLine(const String& line) {
  if (line == "IOT_SETUP") { openPairing(); return; }
  if (line == "IOT_RESET_CLOUD") {
    _cloud.disconnect(); _cloudStarted = false; _welcomed = false;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _key = ""; _deviceId = ""; _ticket = ""; _receipt = ""; _job = "idle"; _error = "";
    save();
    xSemaphoreGive(_mutex);
    openPairing(); Serial.println("[Rozakos] Cloud credentials cleared; Wi-Fi and hardware settings preserved."); return;
  }
  if (!line.startsWith("{")) return;
  JsonDocument doc;
  if (deserializeJson(doc, line)) return;
  // A physically attached USB host may provision Wi-Fi without exposing it
  // in the sketch, telemetry or a network endpoint without authorization.
  if (_manageWifi && doc["wifi"].is<JsonObject>()) {
    String ssid = doc["wifi"]["ssid"] | "", password = doc["wifi"]["password"] | "";
    if (!ssid.isEmpty() && ssid.length() <= 32 && password.length() <= 63) {
      _ssid = ssid; _password = password; _wifiPending = true; _wifiApplyAt = millis() + 750;
      Serial.println("{\"wifi_queued\":true}");
    } else Serial.println("{\"error\":\"Invalid Wi-Fi settings\"}");
    return;
  }
  if (_serialRequest && _serialRequest(doc.as<JsonVariantConst>())) return;
  const char* error = execute(doc["pin"] | "", doc["value"]);
  JsonDocument reply;
  if (error) reply["error"] = error; else { reply["pin"] = doc["pin"]; reply["value"] = doc["value"]; }
  serializeJson(reply, Serial); Serial.println(); _lastPush = 0;
}
void RozakosIoT::loop() {
  if (!_mutex) return;
  if (digitalRead(0) == LOW) {
    if (!_buttonSince) _buttonSince = millis();
    if (!_buttonOpened && millis() - _buttonSince > 3000) { openPairing(); _buttonOpened = true; }
    if (!_buttonReset && millis() - _buttonSince > 10000) { serialLine("IOT_RESET_CLOUD"); _buttonReset = true; }
  } else { _buttonSince = 0; _buttonOpened = false; _buttonReset = false; }
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') { if (!_overflow) serialLine(_line); _line = ""; _overflow = false; }
    else if (c != '\r' && !_overflow) { if (_line.length() >= 1024) { _overflow = true; _line = ""; } else _line += c; }
  }
  std::vector<Work> work;
  xSemaphoreTake(_mutex, portMAX_DELAY); work.swap(_commands); xSemaphoreGive(_mutex);
  for (const auto& item : work) {
    JsonDocument value; deserializeJson(value, item.json);
    const char* error = execute(item.pin, value.as<JsonVariantConst>());
    JsonDocument result; result["pin"] = item.pin; result["ok"] = !error;
    if (error) result["error"] = error;
    String json; serializeJson(result, json); _local.textAll(json); _lastPush = 0;
  }
  // Let the HTTP 202 leave the interface before changing its Wi-Fi network.
  if (_wifiPending && int32_t(millis() - _wifiApplyAt) >= 0) {
    _wifiPending = false;
    if (save()) { WiFi.begin(_ssid.c_str(), _password.c_str()); _lastWifi = millis(); }
  }
  if (_manageWifi && WiFi.status() != WL_CONNECTED && !_ssid.isEmpty() && millis() - _lastWifi > 30000) {
    _lastWifi = millis(); WiFi.reconnect();
  }
  if (WiFi.status() == WL_CONNECTED) {
    if (time(nullptr) < 1700000000) { if (millis() - _lastWifi > 10000) { configTime(0, 0, "pool.ntp.org", "time.nist.gov"); _lastWifi = millis(); } }
    else {
      if (_claim && _claim->done.load()) finishClaim();
      xSemaphoreTake(_mutex, portMAX_DELAY);
      const bool pending = _job == "pending";
      const String key = _key;
      xSemaphoreGive(_mutex);
      if (pending && !_claim && (!_lastClaim || millis() - _lastClaim > 10000)) { _lastClaim = millis(); startClaim(); }
      if (!key.isEmpty() && !_cloudStarted) {
        _cloudStarted = true;
        _cloud.beginSslWithCA(HOST, 443, "/ws/device", ROZAKOS_ROOTS, "");
        String headers = "X-Device-Key: " + key + "\r\nUser-Agent: RozakosIoT/0.2";
        _cloud.setExtraHeaders(headers.c_str()); _cloud.setReconnectInterval(10000); _cloud.enableHeartbeat(20000, 5000, 2);
      }
      if (_cloudStarted) _cloud.loop();
    }
    if (_manageWifi && !pairingActive()) WiFi.softAPdisconnect(true);
  }
  if (!_lastPush || millis() - _lastPush >= 2000) {
    _lastPush = millis();
    JsonDocument doc; snapshot(doc);
    xSemaphoreTake(_mutex, portMAX_DELAY); _values.set(doc); xSemaphoreGive(_mutex);
    for (JsonPair item : doc.as<JsonObject>()) {
      JsonDocument frame; frame["pin"] = item.key().c_str(); frame["value"] = item.value();
      String json; serializeJson(frame, json); _local.textAll(json); Serial.println(json);
    }
    if (cloudConnected()) {
      JsonDocument frame; frame["type"] = "TELEMETRY"; frame["data"] = doc;
      String json; serializeJson(frame, json); if (json.length() <= 16384) _cloud.sendTXT(json);
    }
    _local.cleanupClients(4);
  }
}
