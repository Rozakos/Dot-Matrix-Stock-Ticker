#pragma once

#include <Arduino.h>

// Abstraction over the physical display so the ticker logic never touches
// MD_Parola directly. The concrete ParolaDisplay drives the 2x4 MAX7219 FC-16
// chain; a different implementation could be dropped in for a bench test or a
// different panel without changing the ticker.
//
// Layout is two horizontal zones:
//   ZONE_TOP    — modules 0-3 (top row)    — the symbol
//   ZONE_BOTTOM — modules 4-7 (bottom row)  — the price / change
class IDisplay {
 public:
  static constexpr uint8_t ZONE_TOP    = 0;
  static constexpr uint8_t ZONE_BOTTOM = 1;

  virtual ~IDisplay() {}

  // Initialise hardware. Call once from the display task (keeps SPI on one core).
  virtual void begin() = 0;

  virtual void setBrightness(uint8_t intensity0to15) = 0;
  virtual void setScrollSpeed(uint16_t frameDelayMs) = 0;

  // Set a zone's content. scroll=false -> static, centered (no animation);
  // scroll=true -> scrolls right-to-left and repeats. Resets that zone's
  // animation. `text` may contain GLYPH_UP / GLYPH_DOWN (see below).
  virtual void setZone(uint8_t zone, const String& text, bool scroll) = 0;

  // Drive the animation. Call frequently from the display task. Returns true
  // on the frame the bottom (scrolling) zone completes a full pass.
  virtual bool animate() = 0;
};

// Control codes the ticker embeds in bottom-row text; ParolaDisplay registers
// custom up/down arrow glyphs at these font positions in begin(). They never
// appear in normal symbol/price text, so they are safe to repurpose.
namespace glyph {
inline constexpr char UP   = '\x01';
inline constexpr char DOWN = '\x02';
}  // namespace glyph
