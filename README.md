# HT_HC01 — Arduino driver for the Heltec HT-HC01 Wi-Fi HaLow module

Arduino hardware driver for the [Heltec HT-HC01](https://heltec.org/project/ht-hc01/),
a Wi-Fi HaLow (IEEE 802.11ah) radio module based on the **Morse Micro MM6108**:
902–928 MHz, 1/2/4/8 MHz channels, up to 32.5 Mbps, up to 21 dBm, 1–2 km range.

## What this library is (and is not)

The HT-HC01 is a **raw radio module**. It has no application MCU and no
AT-command firmware — the 802.11ah MAC/driver stack runs on the *host*
processor and is provided by Morse Micro, together with chip firmware and
Heltec's board configuration file (BCF).

This library implements the complete **host hardware layer** for the module:

- Control-pin management and reset/boot sequencing (`RESET_N`)
- Power-save handshake: `WAKE` output, `BUSY` (MM_GPIO_0) input
- SPI transport on the module's SDIO-alternate pins, with the CS framing
  style the Morse Micro HAL expects
- The `SPI_INT` chip-to-host interrupt line

Use it to bring up, test and power-manage the module from any Arduino
board, and as the porting layer when integrating the Morse Micro stack for
actual networking — see [extras/PORTING.md](extras/PORTING.md).

It does **not** by itself join networks or move packets: that requires
Morse Micro's driver + firmware (closed firmware blobs, host MAC library)
on top of this layer. If you want a plug-and-play serial module with AT
commands instead, that product is the
[HT-HC02](https://heltec.org/project/ht-hc02/).

## Installation

Copy the `HT_HC01` folder into your Arduino `libraries` directory (or
*Sketch → Include Library → Add .ZIP Library* on a zip of it), then
`#include <HT_HC01.h>`.

## Wiring

All IO is 3.3 V. Power VBAT + VDD_FEM from 3.0–3.6 V able to source
~250 mA peaks (transmit at 21 dBm); VDDIO must match the host IO voltage
and not exceed VBAT.

| HT-HC01 pad | Signal | Direction | Host connection |
|---|---|---|---|
| 35 | RESET_N | host → module | any GPIO (required) |
| 36 | WAKE | host → module | any GPIO (for power save) |
| 34 | BUSY (MM_GPIO_0) | module → host | any GPIO (for power save) |
| 18 | SPI_SCK (SDIO_CLK) | host → module | SPI SCK |
| 21 | SPI_MOSI (SDIO_CMD) | host → module | SPI MOSI |
| 17 | SPI_MISO (SDIO_D0) | module → host | SPI MISO |
| 22 | SPI_CS (SDIO_D3) | host → module | any GPIO |
| 16 | SPI_INT (SDIO_D1) | module → host | interrupt-capable GPIO |
| 25 / 32 / 19 | VBAT / VDD_FEM / VDDIO | power | 3.3 V |
| 1–3, 12, 20, 26, 31, 37 | GND | power | GND |

Datasheet hardware rules:

- 10k–100k pull-ups on **all** SDIO/SPI bus lines (SDIO 2.0 requirement).
- 10k pull-downs to GND on the JTAG pads and every unused GPIO
  (floating pins raise VDDIO leakage).

## Quick start

```cpp
#include <HT_HC01.h>
#include <SPI.h>

HT_HC01::Config cfg;              // build the pin map for your board
// cfg.pinResetN = 4; cfg.pinWake = 5; cfg.pinBusy = 6;
// cfg.pinCs = 10;    cfg.pinIrq = 7;

HT_HC01 halow(cfg);

void setup() {
  halow.begin();                  // GPIO setup + hard reset
  SPI.begin(12, 13, 11);          // ESP32: SCK, MISO, MOSI on your pins
  halow.attachSPI(SPI, 12000000); // ≤50 MHz; start conservative

  halow.select();                 // one CS frame, any number of transfers
  uint8_t buf[4];
  halow.readBytes(buf, sizeof(buf));
  halow.deselect();

  halow.allowSleep();             // drop the hardware wake request
  halow.wakeAndWait(100);         // re-assert WAKE, wait for BUSY
}
```

## API overview

| Method | Purpose |
|---|---|
| `begin()` / `end()` | GPIO setup + hard reset / release resources |
| `hardReset()`, `holdInReset()`, `releaseReset()` | reset sequencing (`resetHoldMs`, `bootDelayMs` configurable) |
| `wakeAssert()`, `wakeDeassert()`, `allowSleep()` | wake-request line |
| `wakeAndWait(timeout)` | wake handshake against BUSY |
| `isBusyAsserted()`, `waitForBusy(state, timeout)` | busy/awake indication |
| `attachSPI(spi, hz, mode)`, `setClock(hz)` | bind an SPI bus |
| `select()` / `deselect()` | CS framing (opens/closes the SPI transaction) |
| `transferByte()`, `transfer()`, `writeBytes()`, `readBytes()` | full-duplex transfers inside a frame |
| `attachChipInterrupt(isr, mode)`, `irqAsserted()` | SPI_INT line |

Polarities (`wakeActiveHigh`, `busyActiveHigh`) and reset timings are
adjustable through `HT_HC01::Config`.

## Examples

- **HardwareTest** — wiring/power smoke test: reset, watch BUSY, wake handshake.
- **PowerModes** — exercise reset/sleep/wake states for current measurements.
- **SPITransport** — raw SPI bus checkout and chip-interrupt demo.

## Module power modes (datasheet)

| Mode | VBAT current (typ) | Notes |
|---|---|---|
| Hibernate | 0.05 µA | powered off, waits for WAKE |
| Deep sleep | 1 µA | RC oscillator running |
| Snooze | 27 µA | memory retained, wake timer |
| Listen (8 MHz) | 37 mA | |
| TX MCS0 @21 dBm (8 MHz) | 78 mA + 147 mA on VDD_FEM | |

Sleep-state entry is controlled by the chip firmware via the Morse Micro
driver; this library provides the WAKE/BUSY hardware handshake around it.

## References

- [Heltec HT-HC01 product page](https://heltec.org/project/ht-hc01/) ·
  [wiki](https://wiki.heltec.org/docs/devices/wifi-halow/ht-hc01/) ·
  [datasheet](https://resource.heltec.cn/download/HT-HC01/Datasheet/HT-HC01.pdf) ·
  [reference designs (SPI/SDIO)](https://resource.heltec.cn/download/HT-HC01/Reference_design) ·
  [BCF](https://resource.heltec.cn/download/HT-HC01/BCF)
- Morse Micro: [mm-iot-sdk](https://github.com/MorseMicro/mm-iot-sdk) ·
  [esp-halow](https://github.com/MorseMicro/esp-halow) ·
  [morse-driver](https://github.com/MorseMicro/morse-driver) ·
  [morse-firmware](https://github.com/MorseMicro/morse-firmware)

## License

MIT — see [LICENSE](LICENSE). Datasheet facts © Heltec Automation.
