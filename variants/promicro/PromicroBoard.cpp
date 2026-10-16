#include <Arduino.h>
#include <Wire.h>

#include "PromicroBoard.h"

// A CardKB can keep SDA asserted while its MCU is resetting at the same time
// as the nRF52.  The nRF TWIM peripheral has no bus-clear routine, so release
// the lines and provide the standard nine SCL clocks before enabling TWIM.
static void recoverI2CBus(uint8_t sda, uint8_t scl) {
    pinMode(sda, INPUT_PULLUP);
    pinMode(scl, INPUT_PULLUP);
    delayMicroseconds(5);

    for (uint8_t i = 0; i < 9 && digitalRead(sda) == LOW; i++) {
        pinMode(scl, INPUT_PULLUP);  // release SCL; allow clock stretching
        const uint32_t wait_start = micros();
        while (digitalRead(scl) == LOW && (uint32_t)(micros() - wait_start) < 1000) {}
        if (digitalRead(scl) == LOW) break;
        delayMicroseconds(5);
        digitalWrite(scl, LOW);     // preload low before switching to output
        pinMode(scl, OUTPUT);
        delayMicroseconds(5);
    }

    // Generate a STOP condition: SDA low -> SCL high -> SDA high.
    digitalWrite(sda, LOW);
    pinMode(sda, OUTPUT);
    delayMicroseconds(5);
    pinMode(scl, INPUT_PULLUP);
    delayMicroseconds(5);
    pinMode(sda, INPUT_PULLUP);
    delayMicroseconds(5);
}

void PromicroBoard::begin() {    
    NRF52Board::begin();
    btn_prev_state = HIGH;
  
    pinMode(PIN_VBAT_READ, INPUT);

    #ifdef BUTTON_PIN
      pinMode(BUTTON_PIN, INPUT_PULLUP);
    #endif

    #if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
      Wire.setPins(PIN_BOARD_SDA, PIN_BOARD_SCL);
      recoverI2CBus(PIN_BOARD_SDA, PIN_BOARD_SCL);
    #endif
    
    Wire.begin();
    // CardKB, RTC, and optional DRV2605 share this bus.  Keep the bus at the
    // conservative standard-mode rate for reliable operation on jumper wires.
    Wire.setClock(100000);

    pinMode(SX126X_POWER_EN, OUTPUT);
    digitalWrite(SX126X_POWER_EN, HIGH);
    delay(10);   // give sx1262 some time to power up
}
