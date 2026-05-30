#include "ticker.h"

#include <math.h>

#include "../display/display.h"
#include "../settings/settings_store.h"

Ticker::Ticker(IDisplay& display, QuoteStore& store, SettingsStore& settings)
    : _display(display), _store(store), _settings(settings) {}

void Ticker::begin() {
  _dwellSeconds = _settings.dwellSeconds();
  _speed        = _settings.scrollSpeedMs();
  _brightness   = _settings.brightness();
  _display.setScrollSpeed(_speed);
  _display.setBrightness(_brightness);

  _quotes.clear();
  _index            = 0;
  _lastAdvance      = 0;  // forces an immediate advance on the first tick
  _lastSettingsPoll = 0;
}

void Ticker::tick() {
  applySettings();

  const uint32_t now     = millis();
  const uint32_t dwellMs = _dwellSeconds * 1000UL;
  if (_quotes.empty() || now - _lastAdvance >= dwellMs) {
    advance();
    _lastAdvance = now;
  }

  _display.animate();
}

void Ticker::advance() {
  // Refresh the snapshot at the start of each full pass (or when we have none)
  // so prices stay stable within a cycle but pick up new data each lap.
  if (_quotes.empty() || _index + 1 >= _quotes.size()) {
    _quotes = _store.snapshot();
    _index  = 0;
  } else {
    ++_index;
  }

  if (_quotes.empty()) {
    _display.setZone(IDisplay::ZONE_TOP,    "----",    false);
    _display.setZone(IDisplay::ZONE_BOTTOM, "no data", true);
    return;
  }
  render(_quotes[_index]);
}

void Ticker::render(const Quote& q) {
  _display.setZone(IDisplay::ZONE_TOP, q.symbol, false);

  String bottom;
  if (q.fresh) {
    // Monochrome can't color-code, so the change direction is an arrow glyph.
    char arrow = (q.changePct >= 0.0f) ? glyph::UP : glyph::DOWN;
    char buf[48];
    snprintf(buf, sizeof(buf), "%.2f  %c%.2f%%",
             q.last, arrow, fabsf(q.changePct));
    bottom = buf;
  } else {
    bottom = "no data";
  }
  _display.setZone(IDisplay::ZONE_BOTTOM, bottom, true);
}

void Ticker::applySettings() {
  const uint32_t now = millis();
  if (now - _lastSettingsPoll < 1000) return;
  _lastSettingsPoll = now;

  _dwellSeconds = _settings.dwellSeconds();

  uint16_t sp = _settings.scrollSpeedMs();
  if (sp != _speed) {
    _speed = sp;
    _display.setScrollSpeed(_speed);
  }
  uint8_t br = _settings.brightness();
  if (br != _brightness) {
    _brightness = br;
    _display.setBrightness(_brightness);
  }
}
