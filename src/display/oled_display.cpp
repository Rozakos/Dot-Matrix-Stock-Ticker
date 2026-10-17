#ifdef DISPLAY_OLED
#include "oled_display.h"
#include <Wire.h>

void OledDisplay::begin() {
  Wire.begin(21, 22);
  for (uint8_t address : {0x3c, 0x3d}) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      _ready = _oled.begin(SSD1306_SWITCHCAPVCC, address, false, false);
      log_i("[oled] SSD1306 address=0x%02x ready=%d", address, _ready);
      break;
    }
  }
  if (!_ready) log_e("[oled] SSD1306 not found on SDA=21 SCL=22");
  _oled.setTextWrap(false);
  _oled.setTextColor(SSD1306_WHITE);
}
void OledDisplay::setBrightness(uint8_t intensity) {
  if (_ready) {
    _oled.ssd1306_command(SSD1306_SETCONTRAST);
    _oled.ssd1306_command(uint8_t(min(intensity, uint8_t(15)) * 17));
  }
}
void OledDisplay::setZone(uint8_t zone, const String& text, bool scroll) {
  if (zone > 1) return;
  _text[zone] = text;
  _text[zone].replace(String(glyph::UP), "+");
  _text[zone].replace(String(glyph::DOWN), "-");
  _scroll[zone] = scroll;
  if (zone == 1) _x = 128;
  _dirty = true;
}
bool OledDisplay::animate() {
  if (!_ready || (!_dirty && millis() - _frameMs < _speed)) return false;
  _frameMs = millis();
  _dirty = false;
  _oled.clearDisplay();
  _oled.setTextSize(2);
  _oled.setCursor(max(0, (128 - int(_text[0].length()) * 12) / 2), 8);
  _oled.print(_text[0]);
  _oled.setTextSize(1);
  const int width = _text[1].length() * 6;
  const bool scrolling = _scroll[1] && width > 128;
  _oled.setCursor(scrolling ? _x : max(0, (128 - width) / 2), 42);
  _oled.print(_text[1]);
  _oled.display();
  if (scrolling && --_x < -width) { _x = 128; return true; }
  return !scrolling;
}
#endif
