#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <WebSocketsClient.h>
#include <Preferences.h>
#include <atomic>
#include <functional>
#include <vector>

// Call begin/loop/virtualWrite on one task. Async HTTP commands are queued;
// hardware callbacks run on the loop task, never the network event task.
class RozakosIoT {
 public:
  using Writer = std::function<const char*(JsonVariantConst)>;
  using Command = std::function<const char*(const String&, JsonVariantConst)>;
  void identity(const char* name, const char* type = "generic_esp32");
  void pin(const char* name, Writer writer = nullptr);
  void onCommand(Command callback) { _command = callback; }
  void onValidate(Command callback) { _validate = callback; }
  void onSnapshot(std::function<void(JsonDocument&)> callback) { _snapshot = callback; }
  void onSerialRequest(std::function<bool(JsonVariantConst)> callback) { _serialRequest = callback; }
  template<typename T> void virtualWrite(const char* pinName, T value) {
    if (_mutex) xSemaphoreTake(_mutex, portMAX_DELAY);
    _values[pinName] = value;
    if (_mutex) xSemaphoreGive(_mutex);
  }
  void begin(AsyncWebServer* existingServer = nullptr, bool manageWifi = true);
  void loop();
  bool cloudConnected() { return _welcomed && _cloud.isConnected(); }
  bool enrolled() const { return !_key.isEmpty(); }
  bool pairingActive() const { return _pairUntil && int32_t(_pairUntil - millis()) > 0; }
  uint32_t pairingRemainingMs() const { return pairingActive() ? _pairUntil - millis() : 0; }
  const String& setupCode() const { return _code; }
  const String& hardwareId() const { return _hardwareId; }
  void openPairing(); // Invoke only from a physical button, display menu or USB.
  // Optional shared secret for local /state, /command and the port-81 WebSocket.
  // Clients send it as the X-Device-Password header (WebSocket: ?token=...).
  // Without it the local protocol trusts every client on the LAN.
  void localSecret(const char* secret) { _localSecret = secret ? secret : ""; }
 private:
  struct Pin { String name; Writer write; };
  struct Work { String pin, json; };
  // A cloud claim runs on its own short-lived task so TLS never blocks loop().
  // Only status/response cross tasks; loop() applies the result.
  struct Claim { String body; int status = 0; String response; std::atomic<bool> done{false}; };
  Claim* _claim = nullptr;
  String _localSecret;
  std::vector<Pin> _pins;
  std::vector<Work> _commands;
  Command _command;
  Command _validate;
  std::function<void(JsonDocument&)> _snapshot;
  std::function<bool(JsonVariantConst)> _serialRequest;
  JsonDocument _values;
  AsyncWebServer* _http = nullptr;
  AsyncWebServer _wsServer{81};
  AsyncWebSocket _local{"/"};
  WebSocketsClient _cloud;
  Preferences _prefs;
  SemaphoreHandle_t _mutex = nullptr;
  String _name, _type, _hardwareId, _code, _key, _deviceId, _ticket, _receipt;
  String _ssid, _password, _job = "idle", _error, _line;
  bool _manageWifi = true, _welcomed = false, _cloudStarted = false, _overflow = false;
  bool _wifiPending = false;
  uint32_t _wifiApplyAt = 0;
  bool _buttonOpened = false, _buttonReset = false;
  uint32_t _pairUntil = 0, _lastPush = 0, _lastClaim = 0, _buttonSince = 0, _lastWifi = 0;
  unsigned _badCodes = 0;
  void snapshot(JsonDocument& doc);
  const char* execute(const String& pinName, JsonVariantConst value);
  bool authorize(AsyncWebServerRequest* request, JsonVariantConst body);
  bool localAllowed(AsyncWebServerRequest* request);
  bool save();
  void startClaim();
  void finishClaim();
  static void claimTask(void* argument);
  void serialLine(const String& line);
};
