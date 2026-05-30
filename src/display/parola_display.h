#pragma once

#include <MD_MAX72xx.h>
#include <MD_Parola.h>

#include "display.h"

// IDisplay backed by MD_Parola driving the 8-module FC-16 chain as two
// horizontal zones (top = modules 0-3, bottom = modules 4-7) over hardware
// SPI. See config.h for the wiring contract.
class ParolaDisplay : public IDisplay {
 public:
  ParolaDisplay();

  void begin() override;
  void setBrightness(uint8_t intensity0to15) override;
  void setScrollSpeed(uint16_t frameDelayMs) override;
  void setZone(uint8_t zone, const String& text, bool scroll) override;
  bool animate() override;

 private:
  MD_Parola _p;
  // MD_Parola keeps the char* we hand it, so the backing String must outlive
  // the animation — hence one persistent String per zone.
  String   _zoneText[2];
  bool     _scroll[2]  = {false, false};
  uint16_t _speed      = 30;
  uint8_t  _intensity  = 6;
};
