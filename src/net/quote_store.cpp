#include "quote_store.h"

void QuoteStore::begin() {
  _mu = xSemaphoreCreateMutex();
}

void QuoteStore::setQuotes(std::vector<Quote> q, time_t epoch) {
  xSemaphoreTake(_mu, portMAX_DELAY);
  _quotes = std::move(q);
  _lastUpdate = epoch;
  xSemaphoreGive(_mu);
}

std::vector<Quote> QuoteStore::snapshot() const {
  xSemaphoreTake(_mu, portMAX_DELAY);
  std::vector<Quote> copy = _quotes;
  xSemaphoreGive(_mu);
  return copy;
}

time_t QuoteStore::lastUpdate() const {
  xSemaphoreTake(_mu, portMAX_DELAY);
  time_t v = _lastUpdate;
  xSemaphoreGive(_mu);
  return v;
}
