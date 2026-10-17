#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <vector>

struct Quote {
  String symbol;
  float  last      = NAN;
  float  changePct = NAN;
  bool   fresh     = false;  // true when last & changePct are both valid
  // millis() of the last successful fetch of THIS symbol. A symbol whose fetch
  // fails keeps its previous good price; the ticker marks it STALE once this
  // gets old (millis, not epoch, so an NTP sync can't fake an age).
  uint32_t updatedMs = 0;
  String marketState;
  float extPrice = NAN;
  float extChangePct = NAN;
};

// Thread-safe snapshot store. The NET task writes the latest quotes; the
// DISPLAY task reads consistent snapshots under the same mutex.
class QuoteStore {
 public:
  void begin();

  void setQuotes(std::vector<Quote> q, time_t epoch);

  // Snapshot copy under lock.
  std::vector<Quote> snapshot() const;
  time_t             lastUpdate() const;  // 0 until the first successful fetch

 private:
  mutable SemaphoreHandle_t _mu = nullptr;
  std::vector<Quote> _quotes;
  time_t             _lastUpdate = 0;
};
