/*
 * HT_HC01 HardwareTest
 *
 * Bring-up / smoke test for the Heltec HT-HC01 Wi-Fi HaLow module.
 * Verifies the control-pin wiring: performs a hard reset and watches the
 * BUSY line through the wake handshake. No SPI traffic is exchanged.
 *
 * Example wiring (ESP32 DevKit, adjust to your board - all IO is 3.3 V):
 *
 *   ESP32 GPIO 4   -> HT-HC01 pad 35  RESET_N
 *   ESP32 GPIO 5   -> HT-HC01 pad 36  WAKE
 *   ESP32 GPIO 6   <- HT-HC01 pad 34  BUSY (MM_GPIO_0)
 *   3V3            -> pads 25 (VBAT), 32 (VDD_FEM), 19 (VDDIO)
 *   GND            -> all GND pads
 *
 * Datasheet notes: tie unused GPIO/JTAG pads to GND with 10k pull-downs,
 * and pull the SDIO bus pads up with 10k-100k even when unused.
 */

#include <HT_HC01.h>

const int8_t PIN_RESET_N = 4;
const int8_t PIN_WAKE    = 5;
const int8_t PIN_BUSY    = 6;

HT_HC01 halow(PIN_RESET_N, PIN_WAKE, PIN_BUSY);

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println(F("HT-HC01 hardware test"));

  if (!halow.begin()) {
    Serial.println(F("begin() failed - RESET_N pin not configured"));
    while (true) delay(1000);
  }
  Serial.println(F("Module reset and released."));

  // After reset with WAKE asserted, the module should come up and (once
  // its firmware would be loaded by a host driver) drive BUSY. On a bare
  // module without a host stack, BUSY behavior tells you the pin wiring
  // and power are sound.
  Serial.print(F("BUSY after boot: "));
  Serial.println(halow.isBusyAsserted() ? F("asserted") : F("deasserted"));

  Serial.println(F("Testing wake handshake..."));
  halow.allowSleep();
  delay(100);
  Serial.print(F("BUSY with WAKE deasserted: "));
  Serial.println(halow.isBusyAsserted() ? F("asserted") : F("deasserted"));

  if (halow.wakeAndWait(200)) {
    Serial.println(F("Wake handshake OK (or no BUSY pin configured)."));
  } else {
    Serial.println(F("Wake handshake timed out - normal on a bare module"));
    Serial.println(F("with no host driver loaded; check wiring if the"));
    Serial.println(F("Morse Micro stack is running and this still fails."));
  }
}

void loop() {
  // Report BUSY transitions so you can watch the pin live.
  static bool last = false;
  bool now = halow.isBusyAsserted();
  if (now != last) {
    Serial.print(F("BUSY -> "));
    Serial.println(now ? F("asserted") : F("deasserted"));
    last = now;
  }
  delay(10);
}
