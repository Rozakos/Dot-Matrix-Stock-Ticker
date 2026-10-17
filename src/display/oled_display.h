#pragma once
#ifdef DISPLAY_OLED
#include <Adafruit_SSD1306.h>
#include "display.h"

class OledDisplay : public IDisplay {
 public:
  void begin() override;
  void setBrightness(uint8_t intensity) override;
  void setScrollSpeed(uint16_t speed) override { _speed = speed; }
  void setZone(uint8_t zone, const String& text, bool scroll) override;
  bool animate() override;
 private:
  Adafruit_SSD1306 _oled{128, 64, &Wire, -1};
  String _text[2];
  bool _scroll[2] = {false, false};
  bool _ready = false;
  bool _dirty = true;
  uint16_t _speed = 30;
  uint32_t _frameMs = 0;
  int16_t _x = 128;
};
#endif
