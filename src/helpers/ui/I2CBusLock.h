#pragma once

#if defined(NRF52_PLATFORM) && defined(CARDKB_I2C)
#include <Arduino.h>

extern void* meshcore_i2c_bus_mutex;

struct I2CBusLock {
  I2CBusLock() {
    if (meshcore_i2c_bus_mutex) {
      xSemaphoreTake((SemaphoreHandle_t)meshcore_i2c_bus_mutex, portMAX_DELAY);
    }
  }

  ~I2CBusLock() {
    if (meshcore_i2c_bus_mutex) {
      xSemaphoreGive((SemaphoreHandle_t)meshcore_i2c_bus_mutex);
    }
  }
};
#else
struct I2CBusLock { };
#endif
