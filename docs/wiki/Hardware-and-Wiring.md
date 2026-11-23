# Hardware and wiring

[Documentation home](Home.md)

## Parts and identification

| Part | Identification and confidence |
| --- | --- |
| ESP32 USB-C development board | Classic ESP-WROOM-32 module confirmed from a close-up; upload identified ESP32-D0WD-V3 revision 3.1 |
| USB-to-UART bridge | CP2102, VID:PID `10C4:EA60`, serial `0001`; observed Mac port `/dev/cu.usbserial-0001` |
| Amplifier | MAX98357A I²S mono breakout and included speaker; audible playback verified |
| Microphone | Supplied product images identify INMP441 (`441` can marking); working I²S capture verified on the physical unit |
| Touch module | Common TTP223-style module; exact IC marking remains unreadable. Active-high momentary behavior verified |
| OLED | ELEGOO 0.96-inch 128×64 yellow/blue I²C module; listing specifies SSD1306 and 3.3 V / 5 V supplies; scan found `0x3C` |
| Separate USB-C breakout | Headers VBUS, GND, D+, D−, CC1, CC2; listing advertises onboard 5.1 kΩ CC resistors |

The dev board's USB-C socket connects through an onboard UART bridge. It is not a native-USB ESP32-S2/S3/C3 board.

## Complete bench wiring

Disconnect both USB supplies before changing wires. GPIO numbers refer to the board labels, not physical header positions.

| Module pin | Connection |
| --- | --- |
| Amp V / VIN | Breadboard 5 V rail from separate USB-C breakout VBUS |
| Amp G / GND | Common ground |
| Amp BCL / BCLK | ESP32 D26 / GPIO26 |
| Amp LRC / LRCLK | ESP32 D25 / GPIO25 |
| Amp DIN | ESP32 D27 / GPIO27 |
| Speaker | Amp's two-wire speaker socket **or** across amp + and − terminals |
| Mic VDD | ESP32 3V3 |
| Mic GND | Common ground |
| Mic SCK | D26 / GPIO26, shared with amp BCLK |
| Mic WS | D25 / GPIO25, shared with amp LRCLK |
| Mic SD | D34 / GPIO34, separate from amp data |
| Mic L/R | GND, selecting the left channel |
| Touch VCC | ESP32 3V3 |
| Touch GND | Common ground |
| Touch I/O | D33 / GPIO33 |
| OLED VCC | ESP32 3V3 |
| OLED GND | Common ground |
| OLED SDA | D21 / GPIO21 |
| OLED SCL | D22 / GPIO22 |
| USB-C breakout GND | Common ground including ESP32 GND and amp GND |

Keep **both speaker leads off ground**: the amplifier's speaker output is differential. Use one speaker connection method. Keep the microphone's acoustic port unobstructed.

Leave the touch module's A/B solder jumpers open (`AB=00`) for the pictured module's active-high momentary mode. Its output should be LOW at rest, HIGH for the hold, and LOW on release. Leave the pad untouched briefly after power-up for calibration. Leave amplifier gain/shutdown straps at their working bench defaults.

## Two-supply power arrangement

1. Connect the ESP32's own USB-C socket to the Mac for power, programming, and logs.
2. Power the separate USB-C breakout from a separate 5 V USB supply; its VBUS powers the amp.
3. Join grounds, but do not join the two 5 V power paths.
4. Leave ESP32 VIN/5V and the breakout's D+, D−, CC1, and CC2 header pins open in this setup.

The advertised CC resistors are onboard the breakout; the CC header pins need no additional bench wiring. Do not connect breakout USB data directly to a classic ESP32 GPIO. A single-cable enclosure needs its exact power paths, USB-to-UART bridge, and UART0 connections designed separately.

## Audio and display buses

| Setting | `esp32dev` bench | `esp32voice` voice/timer |
| --- | --- | --- |
| I²S controller | I2S0, master TX+RX | I2S0, master TX+RX |
| Serial format | Philips I²S, stereo 32-bit slots | Philips I²S, stereo 32-bit slots |
| Word/sample clock | 16 kHz | 24 kHz |
| Bits per stereo frame | 64 | 64 |
| Nominal BCLK | 1.024 MHz | 1.536 MHz |
| MCLK | Not connected | Not connected |
| OLED scan clock | 100 kHz | 100 kHz |
| OLED frame transfer | 100 kHz | 400 kHz, returning to 100 kHz afterward |

The mic supplies signed 24-bit data within the 32-bit slots. Slot0 was the active DMA slot with L/R grounded in this setup. Network PCM conversion is separate from this hardware framing. Identical left/right output samples feed the mono amplifier.

## References

- [PlatformIO classic ESP32 target](https://docs.platformio.org/en/stable/boards/espressif32/esp32dev.html)
- [Pinned Espressif32 release](https://github.com/platformio/platform-espressif32/releases/tag/v6.12.0)
- [ESP-IDF 4.4.7 I²S driver](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-reference/peripherals/i2s.html)
- [MAX98357A/B datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX98357A-MAX98357B.pdf)
- [TDK INMP441 datasheet](https://product.tdk.com/system/files/dam/doc/product/sw_piezo/mic/mems-mic/data_sheet/inmp441.pdf)
- [Tontek touch family](https://www.tontek.com.tw/product/product2?idx=243&lang=en)
- [ELEGOO OLED listing](https://www.amazon.com/dp/B0D2RMQQHR)
