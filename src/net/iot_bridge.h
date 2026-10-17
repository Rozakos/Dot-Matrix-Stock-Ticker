#pragma once
#include <Arduino.h>
class AsyncWebServer;
class SettingsStore;
class QuoteStore;
namespace iot_bridge {
void begin(AsyncWebServer& server, SettingsStore& settings, QuoteStore& quotes);
void loop();
String setupCode();
bool pairingActive();
uint32_t pairingRemainingMs();
}
