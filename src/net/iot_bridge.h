#pragma once
class AsyncWebServer;
class SettingsStore;
class QuoteStore;
namespace iot_bridge {
void begin(AsyncWebServer& server, SettingsStore& settings, QuoteStore& quotes);
void loop();
}
