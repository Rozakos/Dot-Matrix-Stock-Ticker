#include "parola_display.h"

#include "../config.h"

namespace {

// Custom 5-wide glyphs for the change-direction arrows, registered into the
// MD_Parola font at glyph::UP / glyph::DOWN. Format: data[0] = column count,
// then one byte per column; within a column the LSB (bit 0) is the TOP row.
//
// If a wiring test shows the arrows upside-down (i.e. the panel maps LSB to
// the bottom row), swap kUpArrow <-> kDownArrow below — one-line fix.
//
//   up:   ..#..      down:  ..#..
//         .###.             ..#..
//         #####             ..#..
//         ..#..             ..#..
//         ..#..             #####
//         ..#..             .###.
//         ..#..             ..#..
uint8_t kUpArrow[]   = {5, 0x04, 0x06, 0x7F, 0x06, 0x04};
uint8_t kDownArrow[] = {5, 0x10, 0x30, 0x7F, 0x30, 0x10};

}  // namespace

ParolaDisplay::ParolaDisplay()
    : _p(MD_MAX72XX::FC16_HW, cfg::DISPLAY_CS_PIN, cfg::DISPLAY_MAX_DEVICES) {}

void ParolaDisplay::begin() {
  _p.begin(2);  // two zones

  // Zone -> module range. DIN enters the top-left module, so modules 0-3 are
  // the top row and 4-7 the bottom row (confirmed wiring, no flip).
  _p.setZone(IDisplay::ZONE_TOP,    0, 3);
  _p.setZone(IDisplay::ZONE_BOTTOM, 4, 7);

  _p.setIntensity(_intensity);
  _p.setSpeed(_speed);

  // Register the arrow glyphs so the ticker can embed glyph::UP / glyph::DOWN.
  _p.addChar(glyph::UP,   kUpArrow);
  _p.addChar(glyph::DOWN, kDownArrow);

  _p.displayClear();
}

void ParolaDisplay::setBrightness(uint8_t intensity0to15) {
  _intensity = intensity0to15 > cfg::MAX_BRIGHTNESS ? cfg::MAX_BRIGHTNESS
                                                    : intensity0to15;
  _p.setIntensity(_intensity);
}

void ParolaDisplay::setScrollSpeed(uint16_t frameDelayMs) {
  _speed = frameDelayMs;
  // Only the bottom zone scrolls; speed is irrelevant to the static top zone.
  _p.setSpeed(IDisplay::ZONE_BOTTOM, _speed);
}

void ParolaDisplay::setZone(uint8_t zone, const String& text, bool scroll) {
  if (zone > IDisplay::ZONE_BOTTOM) return;
  _zoneText[zone] = text;
  _scroll[zone]   = scroll;
  const char* p = _zoneText[zone].c_str();

  if (scroll) {
    // Scroll in from the right and out to the left; pause 0 so a reset loops
    // straight back into another pass.
    _p.displayZoneText(zone, p, PA_LEFT, _speed, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
  } else {
    // Static, centered: print once and hold (no scrolling animation).
    _p.displayZoneText(zone, p, PA_CENTER, 0, 0, PA_PRINT, PA_NO_EFFECT);
  }
  _p.displayReset(zone);
}

bool ParolaDisplay::animate() {
  bool any = _p.displayAnimate();
  bool bottomCompleted = false;
  if (any) {
    for (uint8_t z = 0; z <= IDisplay::ZONE_BOTTOM; ++z) {
      if (!_p.getZoneStatus(z)) continue;  // this zone didn't just finish
      if (_scroll[z]) {
        if (z == IDisplay::ZONE_BOTTOM) bottomCompleted = true;
        _p.displayReset(z);  // loop the scroll
      }
      // Static zones: leave finished — resetting would re-run the print and
      // could flicker the held symbol.
    }
  }
  return bottomCompleted;
}
