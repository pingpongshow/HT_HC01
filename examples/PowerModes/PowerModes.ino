/*
 * HT_HC01 PowerModes
 *
 * Exercises the hardware side of the HT-HC01's power management:
 * hold-in-reset (lowest possible board power), the WAKE/BUSY power-save
 * handshake, and hard reset recovery.
 *
 * The module's snooze / deep sleep / hibernate states (0.05-27 uA on
 * VBAT per the datasheet) are entered by the chip firmware under control
 * of the Morse Micro host driver; from the hardware side this sketch can
 * only request wake (WAKE) and observe the result (BUSY). Use it with an
 * ammeter on VBAT to characterize your board.
 *
 * Wiring: same as the HardwareTest example.
 */

#include <HT_HC01.h>

HT_HC01 halow(/* RESET_N */ 4, /* WAKE */ 5, /* BUSY */ 6);

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println(F("HT-HC01 power mode exerciser"));
  halow.begin();
}

void loop() {
  Serial.println(F("\n--- Held in reset (measure baseline current) ---"));
  halow.holdInReset();
  halow.allowSleep();
  delay(5000);

  Serial.println(F("--- Released, WAKE deasserted (module may sleep) ---"));
  halow.releaseReset();
  delay(5000);
  Serial.print(F("BUSY: "));
  Serial.println(halow.isBusyAsserted() ? F("asserted") : F("deasserted"));

  Serial.println(F("--- WAKE asserted ---"));
  bool awake = halow.wakeAndWait(500);
  Serial.print(F("Wake handshake: "));
  Serial.println(awake ? F("BUSY responded") : F("no BUSY response (expected without host driver)"));
  delay(5000);
}
