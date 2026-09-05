/*
 * HT_HC01 SPITransport
 *
 * Demonstrates the SPI transport layer to the HT-HC01 (MM6108) on the
 * module's SDIO-alternate pins. This example only exercises the bus and
 * dumps whatever the chip clocks back; meaningful register access and
 * networking require the Morse Micro host driver on top of this layer
 * (see extras/PORTING.md).
 *
 * Example wiring (ESP32 DevKit - all IO 3.3 V, add 10k-100k pull-ups on
 * every SDIO/SPI bus line per the datasheet):
 *
 *   ESP32 GPIO 4   -> pad 35  RESET_N
 *   ESP32 GPIO 5   -> pad 36  WAKE
 *   ESP32 GPIO 6   <- pad 34  BUSY
 *   ESP32 GPIO 12  -> pad 18  SPI_SCK  (SDIO_CLK)
 *   ESP32 GPIO 11  -> pad 21  SPI_MOSI (SDIO_CMD)
 *   ESP32 GPIO 13  <- pad 17  SPI_MISO (SDIO_D0)
 *   ESP32 GPIO 10  -> pad 22  SPI_CS   (SDIO_D3)
 *   ESP32 GPIO 7   <- pad 16  SPI_INT  (SDIO_D1)
 */

#include <HT_HC01.h>
#include <SPI.h>

const int8_t PIN_SCK  = 12;
const int8_t PIN_MOSI = 11;
const int8_t PIN_MISO = 13;

volatile bool chipIrqFlag = false;
#if defined(ARDUINO_ARCH_ESP32)
void IRAM_ATTR onChipIrq() { chipIrqFlag = true; }
#else
void onChipIrq() { chipIrqFlag = true; }
#endif

static HT_HC01::Config makeConfig() {
  HT_HC01::Config cfg;
  cfg.pinResetN = 4;
  cfg.pinWake   = 5;
  cfg.pinBusy   = 6;
  cfg.pinCs     = 10;
  cfg.pinIrq    = 7;
  return cfg;
}

HT_HC01 halow(makeConfig());

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println(F("HT-HC01 SPI transport demo"));

  if (!halow.begin()) {
    Serial.println(F("begin() failed"));
    while (true) delay(1000);
  }

#if defined(ARDUINO_ARCH_ESP32)
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI);   // route the bus to our pins
#else
  SPI.begin();                              // hardware-fixed SPI pins
#endif
  halow.attachSPI(SPI, 4000000UL);          // start slow for bring-up
  halow.attachChipInterrupt(onChipIrq, RISING);

  // Clock out a few idle bytes and show what comes back. On a healthy bus
  // with the module held in reset MISO floats high (pull-up) -> 0xFF;
  // anything else hints at a wiring or pull-up problem.
  uint8_t rx[8];

  halow.holdInReset();
  halow.select();
  halow.readBytes(rx, sizeof(rx));
  halow.deselect();
  Serial.print(F("In reset:  "));
  printHex(rx, sizeof(rx));

  halow.releaseReset();
  halow.select();
  halow.readBytes(rx, sizeof(rx));
  halow.deselect();
  Serial.print(F("Running:   "));
  printHex(rx, sizeof(rx));

  Serial.println(F("Transport layer ready. Hand this object to your"));
  Serial.println(F("Morse Micro HAL port for real communication."));
}

void loop() {
  if (chipIrqFlag) {
    chipIrqFlag = false;
    Serial.println(F("SPI_INT asserted by module"));
  }
  delay(1);
}

void printHex(const uint8_t *buf, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (buf[i] < 0x10) Serial.print('0');
    Serial.print(buf[i], HEX);
    Serial.print(' ');
  }
  Serial.println();
}
