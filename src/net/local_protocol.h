#pragma once
#include <Arduino.h>
class AsyncWebServer;
class SettingsStore;
class QuoteStore;

namespace local_protocol {
bool validSymbol(const String& symbol);
void begin(AsyncWebServer& server, SettingsStore& settings, QuoteStore& quotes);
}
