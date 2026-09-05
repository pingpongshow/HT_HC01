# Getting real Wi-Fi HaLow networking on the HT-HC01

The HT-HC01 contains only the Morse Micro MM6108 radio. The 802.11ah
MAC, driver and chip firmware run on the **host**, supplied by Morse
Micro. This library is the *hardware layer underneath* that stack; this
document explains how the two fit together.

## The software stack

```
+--------------------------------------------------+
| Your application (sockets, MQTT, HTTP, ...)      |
+--------------------------------------------------+
| IP stack (LwIP / FreeRTOS+TCP)                   |
+--------------------------------------------------+
| Morse Micro morselib / mmwlan  (802.11ah MAC)    |  <- mm-iot-sdk / esp-halow
| + MM6108 chip firmware + Heltec BCF              |
+--------------------------------------------------+
| mmhal_wlan porting layer (SPI, RESET, WAKE/BUSY) |  <- this library maps here
+--------------------------------------------------+
| HT-HC01 module (MM6108)                          |
+--------------------------------------------------+
```

## Which Morse Micro repository to use

| Host | Repository | Notes |
|---|---|---|
| ESP32 family (incl. Arduino-as-IDF-component) | [MorseMicro/esp-halow](https://github.com/MorseMicro/esp-halow) | ESP-IDF port of mm-iot-sdk, also on the IDF Component Registry. The most practical route to full networking on ESP32. |
| STM32 / FreeRTOS | [MorseMicro/mm-iot-sdk](https://github.com/MorseMicro/mm-iot-sdk) | Reference MCU SDK ("morselib" MAC + driver). |
| Zephyr | [MorseMicro/mm-iot-zephyr](https://github.com/MorseMicro/mm-iot-zephyr) | West manifest. |
| Linux (Raspberry Pi, OpenWrt) | [MorseMicro/morse-driver](https://github.com/MorseMicro/morse-driver) | Full-featured SDIO/SPI kernel driver. |

You will also need:

- **Chip firmware**: [MorseMicro/morse-firmware](https://github.com/MorseMicro/morse-firmware)
  (also bundled inside the SDKs).
- **Board configuration file (BCF)** for the HT-HC01's RF front end:
  <https://resource.heltec.cn/download/HT-HC01/BCF>. Use Heltec's BCF, not a
  Morse Micro evaluation-board one — it encodes this module's PA/regulatory
  calibration.

## Mapping this library onto the mmhal_wlan porting layer

The MCU SDKs isolate all board specifics in a `mmhal_wlan` HAL. The names
below are representative of mm-iot-sdk's HAL (check the header of the SDK
version you vendored — signatures move between releases):

| mmhal_wlan responsibility | HT_HC01 method |
|---|---|
| Hard reset the transceiver | `hardReset()` / `holdInReset()` + `releaseReset()` |
| Assert / release the wake line | `wakeAssert()` / `wakeDeassert()` |
| Read the busy/awake indication | `isBusyAsserted()`, `waitForBusy()` |
| Assert / deassert SPI chip select | `select()` / `deselect()` |
| Transfer bytes on SPI (full duplex) | `transfer()`, `writeBytes()`, `readBytes()` |
| Register the chip's interrupt handler (SPI_INT / SDIO_D1) | `attachChipInterrupt()` |
| Busy-pin interrupt (power save) | attach your own `attachInterrupt()` on the BUSY GPIO |

Implementation notes:

- The HAL controls CS independently of individual transfers (one frame may
  span several transfer calls). That is why `select()`/`deselect()` are
  exposed separately and why the transfer primitives do not touch CS.
- Morse Micro recommends DMA-backed, full-duplex SPI. The portable
  byte-loop in `HT_HC01::transfer()` is fine for bring-up; for real
  throughput replace it with your platform's DMA transfer
  (e.g. `spi_device_transmit` on ESP-IDF, HAL_SPI_TransmitReceive_DMA on
  STM32) while keeping the same CS framing.
- Bus limit is 50 MHz; standard SPI tops out around 25 Mbps of WLAN
  throughput per the datasheet. Start at 4-12 MHz and raise the clock once
  the link is stable.
- SDIO is the higher-throughput alternative (up to 50 MHz, 4-bit), but no
  Arduino core exposes a usable SDIO host API for this - use the Linux
  driver or ESP-IDF for SDIO.

## If you just want AT commands over UART

Heltec's **HT-HC02** is this radio plus a host MCU preloaded with AT-command
firmware — it is the plug-and-play serial option
(<https://heltec.org/project/ht-hc02/>). The HT-HC01's own UART pads
(UART0 on MM_GPIO_3/2, UART1 on MM_GPIO_7/6) are marked "pending software
support" in the datasheet and are not a control interface today.
