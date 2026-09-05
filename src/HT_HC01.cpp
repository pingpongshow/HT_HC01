/*
 * HT_HC01.cpp - implementation. See HT_HC01.h for scope and wiring notes.
 */

#include "HT_HC01.h"

HT_HC01::HT_HC01(const Config &cfg)
    : _cfg(cfg), _spiSettings(12000000UL, MSBFIRST, SPI_MODE0) {}

HT_HC01::HT_HC01(int8_t pinResetN, int8_t pinWake, int8_t pinBusy)
    : _spiSettings(12000000UL, MSBFIRST, SPI_MODE0) {
  _cfg.pinResetN = pinResetN;
  _cfg.pinWake   = pinWake;
  _cfg.pinBusy   = pinBusy;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                          */
/* ------------------------------------------------------------------ */

bool HT_HC01::begin() {
  if (_cfg.pinResetN == PIN_UNUSED) {
    return false;
  }

  // Drive RESET_N asserted from the very first moment the pin becomes an
  // output, so the module boots exactly once, under our control.
  digitalWrite(_cfg.pinResetN, LOW);
  pinMode(_cfg.pinResetN, OUTPUT);
  digitalWrite(_cfg.pinResetN, LOW);

  if (_cfg.pinWake != PIN_UNUSED) {
    // Boot with a wake request present so the module does not immediately
    // drop into power save before the host driver takes over.
    writePin(_cfg.pinWake, _cfg.wakeActiveHigh);
    pinMode(_cfg.pinWake, OUTPUT);
  }
  if (_cfg.pinBusy != PIN_UNUSED) {
    pinMode(_cfg.pinBusy, INPUT);
  }
  if (_cfg.pinCs != PIN_UNUSED) {
    digitalWrite(_cfg.pinCs, HIGH);   // CS idle high
    pinMode(_cfg.pinCs, OUTPUT);
    digitalWrite(_cfg.pinCs, HIGH);
  }
  if (_cfg.pinIrq != PIN_UNUSED) {
    pinMode(_cfg.pinIrq, INPUT);
  }

  hardReset();
  return true;
}

void HT_HC01::end() {
  detachChipInterrupt();
  if (_inTransaction && _spi != nullptr) {
    deselect();
  }
  _spi = nullptr;
}

/* ------------------------------------------------------------------ */
/* Reset control                                                      */
/* ------------------------------------------------------------------ */

void HT_HC01::hardReset() {
  holdInReset();
  delay(_cfg.resetHoldMs);
  releaseReset();
}

void HT_HC01::holdInReset() {
  writePin(_cfg.pinResetN, false);   // RESET_N is active low
}

void HT_HC01::releaseReset() {
  writePin(_cfg.pinResetN, true);
  delay(_cfg.bootDelayMs);
}

/* ------------------------------------------------------------------ */
/* Power save / hibernate handshake                                   */
/* ------------------------------------------------------------------ */

void HT_HC01::wakeAssert() {
  writePin(_cfg.pinWake, _cfg.wakeActiveHigh);
}

void HT_HC01::wakeDeassert() {
  writePin(_cfg.pinWake, !_cfg.wakeActiveHigh);
}

bool HT_HC01::wakeAndWait(uint32_t timeoutMs) {
  wakeAssert();
  if (_cfg.pinBusy == PIN_UNUSED) {
    return true;
  }
  return waitForBusy(true, timeoutMs);
}

bool HT_HC01::isBusyAsserted() const {
  if (_cfg.pinBusy == PIN_UNUSED) {
    return false;
  }
  bool level = (digitalRead(_cfg.pinBusy) == HIGH);
  return _cfg.busyActiveHigh ? level : !level;
}

bool HT_HC01::waitForBusy(bool asserted, uint32_t timeoutMs) const {
  if (_cfg.pinBusy == PIN_UNUSED) {
    return false;
  }
  uint32_t start = millis();
  while (isBusyAsserted() != asserted) {
    if (millis() - start > timeoutMs) {
      return false;
    }
    yield();
  }
  return true;
}

/* ------------------------------------------------------------------ */
/* SPI transport                                                      */
/* ------------------------------------------------------------------ */

void HT_HC01::attachSPI(SPIClass &spi, uint32_t clockHz, uint8_t dataMode) {
  _spi = &spi;
  _spiSettings = SPISettings(clockHz, MSBFIRST, dataMode);
}

void HT_HC01::setClock(uint32_t clockHz) {
  // Preserve the mode currently in use (SPISettings has no getters, so we
  // rebuild with the driver default unless attachSPI() is called again).
  _spiSettings = SPISettings(clockHz, MSBFIRST, SPI_MODE0);
}

void HT_HC01::select() {
  if (_spi == nullptr) return;
  if (!_inTransaction) {
    _spi->beginTransaction(_spiSettings);
    _inTransaction = true;
  }
  writePin(_cfg.pinCs, false);
}

void HT_HC01::deselect() {
  if (_spi == nullptr) return;
  writePin(_cfg.pinCs, true);
  if (_inTransaction) {
    _spi->endTransaction();
    _inTransaction = false;
  }
}

uint8_t HT_HC01::transferByte(uint8_t b) {
  if (_spi == nullptr) return 0xFF;
  return _spi->transfer(b);
}

void HT_HC01::transfer(const uint8_t *tx, uint8_t *rx, size_t len) {
  if (_spi == nullptr) return;
  for (size_t i = 0; i < len; i++) {
    uint8_t out = (tx != nullptr) ? tx[i] : 0xFF;
    uint8_t in = _spi->transfer(out);
    if (rx != nullptr) {
      rx[i] = in;
    }
  }
}

void HT_HC01::writeBytes(const uint8_t *tx, size_t len) {
  transfer(tx, nullptr, len);
}

void HT_HC01::readBytes(uint8_t *rx, size_t len, uint8_t fill) {
  if (_spi == nullptr) return;
  for (size_t i = 0; i < len; i++) {
    rx[i] = _spi->transfer(fill);
  }
}

/* ------------------------------------------------------------------ */
/* Chip interrupt                                                     */
/* ------------------------------------------------------------------ */

bool HT_HC01::attachChipInterrupt(void (*isr)(), int mode) {
  if (_cfg.pinIrq == PIN_UNUSED) {
    return false;
  }
  int irq = digitalPinToInterrupt(_cfg.pinIrq);
  if (irq < 0) {
    return false;
  }
  attachInterrupt(irq, isr, mode);
  _irqAttached = true;
  return true;
}

void HT_HC01::detachChipInterrupt() {
  if (_irqAttached && _cfg.pinIrq != PIN_UNUSED) {
    detachInterrupt(digitalPinToInterrupt(_cfg.pinIrq));
  }
  _irqAttached = false;
}

bool HT_HC01::irqAsserted() const {
  if (_cfg.pinIrq == PIN_UNUSED) {
    return false;
  }
  return digitalRead(_cfg.pinIrq) == HIGH;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

void HT_HC01::writePin(int8_t pin, bool level) const {
  if (pin != PIN_UNUSED) {
    digitalWrite(pin, level ? HIGH : LOW);
  }
}
