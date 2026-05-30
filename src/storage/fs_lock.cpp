#include "fs_lock.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace fs_lock {

namespace {
SemaphoreHandle_t g_mu = nullptr;
}  // namespace

void lock() {
  if (!g_mu) g_mu = xSemaphoreCreateRecursiveMutex();
  xSemaphoreTakeRecursive(g_mu, portMAX_DELAY);
}

void unlock() {
  if (g_mu) xSemaphoreGiveRecursive(g_mu);
}

}  // namespace fs_lock
