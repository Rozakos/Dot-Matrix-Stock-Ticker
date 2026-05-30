#pragma once

#include <Arduino.h>

#include <vector>

#include "../net/quote_store.h"

class IDisplay;
class SettingsStore;

// Drives the per-symbol display cycle: for each configured symbol it shows the
// name static on the top zone and scrolls "<price>  <arrow><change>%" on the
// bottom zone, advancing after the configured dwell. Depends only on IDisplay,
// so it is unaware of MD_Parola.
class Ticker {
 public:
  Ticker(IDisplay& display, QuoteStore& store, SettingsStore& settings);

  // (Re)initialise the cycle and push current tunables to the display.
  void begin();

  // Call frequently from the display task. Advances symbols on the dwell timer
  // and keeps the scroll animation running.
  void tick();

 private:
  void advance();
  void render(const Quote& q);
  void applySettings();  // throttled poll of dwell/speed/brightness

  IDisplay&      _display;
  QuoteStore&    _store;
  SettingsStore& _settings;

  std::vector<Quote> _quotes;
  size_t   _index            = 0;
  uint32_t _lastAdvance      = 0;
  uint32_t _lastSettingsPoll = 0;

  uint32_t _dwellSeconds = 3;
  uint16_t _speed        = 30;
  uint8_t  _brightness   = 6;
};
