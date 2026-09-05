/*
 * HT_HC01.h
 *
 * Arduino hardware driver for the Heltec HT-HC01 Wi-Fi HaLow module
 * (Morse Micro MM6108, IEEE 802.11ah, 902-928 MHz).
 *
 * Scope of this library
 * ---------------------
 * The HT-HC01 is a *radio* module: it contains the MM6108 transceiver, PA,
 * LNA and crystal, but no application MCU and no AT-command firmware.
 * The 802.11ah MAC/driver stack runs on the HOST and is provided by
 * Morse Micro (mm-iot-sdk / esp-halow / morse-driver) together with the
 * chip firmware and the board configuration file (BCF) published by Heltec.
 *
 * This library implements everything the host hardware side needs:
 *
 *   - control-pin management: RESET_N, WAKE, BUSY
 *   - the documented reset / boot sequencing
 *   - the power-save wake handshake (WAKE out, BUSY in)
 *   - SPI bus transport on the module's SDIO-alternate pins
 *   - the SPI_INT chip-to-host interrupt line
 *
 * It is intended both for bare-metal hardware bring-up / testing and as
 * the porting layer ("mmhal") when integrating the Morse Micro stack.
 * See extras/PORTING.md in this library for the mapping.
 *
 * Electrical notes (HT-HC01 datasheet Rev 1.0):
 *   - VBAT / VDD_FEM: 3.0-3.6 V.  VDDIO 1.62-3.6 V, must not exceed VBAT,
 *     and must match the host MCU's IO voltage.
 *   - All SDIO/SPI bus lines require 10k-100k pull-ups per SDIO 2.0 spec.
 *   - Unused GPIO and JTAG pins: 10k pull-down to GND.
 *   - Host requirements for SPI: full-duplex, DMA-backed transfers and
 *     level-triggered interrupts recommended; up to 50 MHz clock.
 */

#ifndef HT_HC01_H
#define HT_HC01_H

#include <Arduino.h>
#include <SPI.h>

/* ------------------------------------------------------------------ */
/* Module pad reference (physical pad number on the HT-HC01)          */
/* Purely informational - use these when drawing up your wiring.      */
/* ------------------------------------------------------------------ */
namespace HTHC01Pad {
enum : uint8_t {
  // Control
  RESET_N   = 35,  // input, active LOW - system reset
  WAKE      = 36,  // input - wake from hibernate / power save
  BUSY      = 34,  // output (MM_GPIO_0) - "Wi-Fi BUSY" / awake indication
  // SPI (alternate function of the SDIO bus pins)
  SPI_SCK   = 18,  // MM_SDIO_CLK
  SPI_MOSI  = 21,  // MM_SDIO_CMD
  SPI_MISO  = 17,  // MM_SDIO_D0
  SPI_CS    = 22,  // MM_SDIO_D3
  SPI_INT   = 16,  // MM_SDIO_D1 - chip-to-host interrupt in SPI mode
  SDIO_D2   = 23,  // unused in SPI mode (pull up)
  // UARTs (alternate functions, "pending software support" per datasheet)
  UART0_TX  = 29,  // MM_GPIO_3 - debug console out
  UART0_RX  = 30,  // MM_GPIO_2
  UART1_TX  = 15,  // MM_GPIO_7
  UART1_RX  = 24,  // MM_GPIO_6
  // Power
  VBAT      = 25,
  VDD_FEM   = 32,
  VDDIO     = 19,
  ANT       = 38,
};
}

class HT_HC01 {
public:
  static const int8_t PIN_UNUSED = -1;

  struct Config {
    // Control pins (host GPIO numbers). RESET_N is required; WAKE and BUSY
    // are only needed if you use power save / hibernate.
    int8_t pinResetN = PIN_UNUSED;  // -> pad 35, active LOW
    int8_t pinWake   = PIN_UNUSED;  // -> pad 36
    int8_t pinBusy   = PIN_UNUSED;  // <- pad 34 (MM_GPIO_0)
    // SPI-mode pins handled by this driver. SCK/MOSI/MISO themselves are
    // configured on the SPIClass you pass to attachSPI().
    int8_t pinCs     = PIN_UNUSED;  // -> pad 22 (SDIO_D3)
    int8_t pinIrq    = PIN_UNUSED;  // <- pad 16 (SDIO_D1 / SPI_INT)

    // Signal polarities. The datasheet fixes RESET_N as active low; WAKE
    // and BUSY polarities follow the Morse Micro reference design
    // (active high) but are configurable in case of external inversion.
    bool wakeActiveHigh = true;
    bool busyActiveHigh = true;

    // Reset timing. The datasheet gives no explicit figures, so these are
    // deliberately conservative; tighten them once your board is proven.
    uint16_t resetHoldMs = 10;   // time RESET_N is held asserted
    uint16_t bootDelayMs = 50;   // wait after releasing RESET_N
  };

  explicit HT_HC01(const Config &cfg);
  HT_HC01(int8_t pinResetN,
          int8_t pinWake = PIN_UNUSED,
          int8_t pinBusy = PIN_UNUSED);

  /* -------------------------------------------------------------- */
  /* Lifecycle                                                      */
  /* -------------------------------------------------------------- */

  // Configures the control GPIOs and performs a hard reset. Returns false
  // if no RESET_N pin was configured.
  bool begin();

  // Releases the interrupt handler and tri-states nothing else (the module
  // keeps running); call holdInReset() first if you want it stopped.
  void end();

  /* -------------------------------------------------------------- */
  /* Reset control                                                  */
  /* -------------------------------------------------------------- */

  void hardReset();      // assert RESET_N, wait resetHoldMs, release, wait bootDelayMs
  void holdInReset();    // assert RESET_N and leave it asserted
  void releaseReset();   // release RESET_N and wait bootDelayMs

  /* -------------------------------------------------------------- */
  /* Power save / hibernate handshake                               */
  /* -------------------------------------------------------------- */

  void wakeAssert();
  void wakeDeassert();

  // Assert WAKE and wait for the module to report awake on BUSY.
  // Returns true on success, or immediately true (after asserting WAKE)
  // when no BUSY pin is configured. Returns false on timeout.
  bool wakeAndWait(uint32_t timeoutMs = 100);

  // Deassert WAKE, allowing the firmware to enter its sleep states
  // (snooze / deep sleep / hibernate are managed by the Morse Micro
  // driver; without it this only removes the hardware wake request).
  void allowSleep() { wakeDeassert(); }

  // Raw BUSY pin state, polarity-corrected: true = module reports busy/awake.
  bool isBusyAsserted() const;

  // Wait for BUSY to reach the given asserted state. True on success.
  bool waitForBusy(bool asserted, uint32_t timeoutMs) const;

  /* -------------------------------------------------------------- */
  /* SPI transport                                                  */
  /* -------------------------------------------------------------- */

  // Attach an SPI bus. The SPIClass must already have had begin() called
  // (on ESP32, SPI.begin(sck, miso, mosi) with your chosen pins).
  // clockHz defaults well below the module's 50 MHz limit; raise it once
  // signal integrity is verified. dataMode is SPI_MODE0 per the Morse
  // Micro host recommendation.
  void attachSPI(SPIClass &spi,
                 uint32_t clockHz = 12000000UL,
                 uint8_t dataMode = SPI_MODE0);
  void setClock(uint32_t clockHz);

  // Manual chip-select framing. select() also opens an SPI transaction,
  // deselect() closes it, so arbitrary multi-transfer frames are possible.
  void select();
  void deselect();

  // Full-duplex primitives. All of these must be wrapped in
  // select()/deselect() by the caller - matching the framing style the
  // Morse Micro HAL expects (CS is controlled independently of transfers).
  uint8_t transferByte(uint8_t b);
  void    transfer(const uint8_t *tx, uint8_t *rx, size_t len);
  void    writeBytes(const uint8_t *tx, size_t len);
  void    readBytes(uint8_t *rx, size_t len, uint8_t fill = 0xFF);

  /* -------------------------------------------------------------- */
  /* Chip interrupt (SPI_INT / SDIO_D1)                             */
  /* -------------------------------------------------------------- */

  // Attach a host interrupt to the SPI_INT line. Returns false when no
  // IRQ pin is configured or the pin does not support interrupts.
  bool attachChipInterrupt(void (*isr)(), int mode = RISING);
  void detachChipInterrupt();
  bool irqAsserted() const;   // current level of the INT line (true = HIGH)

  const Config &config() const { return _cfg; }

private:
  void writePin(int8_t pin, bool level) const;

  Config      _cfg;
  SPIClass   *_spi = nullptr;
  SPISettings _spiSettings;
  bool        _irqAttached = false;
  bool        _inTransaction = false;
};

#endif  // HT_HC01_H
