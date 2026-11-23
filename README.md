# AI Desk Buddy — audio and touch bring-up

The original ESP-WROOM-32 plays a quiet boot tone through a MAX98357A and reports INMP441 microphone levels over serial. Speaker playback passed the user bench check. Microphone level response also passed the user bench check; touch press/release also passed the user bench check. OLED is identified as SSD1306 and responds at I²C address 0x3C; Live Wi-Fi voice replies are verified, and the ESP32-owned typed focus timer passed the offline completion test. The default bench build does not store or transmit microphone audio; the separate voice build streams accepted holds to the configured Mac backend.

For Wi-Fi push-to-talk setup, local echo testing, and OpenAI configuration, see [the Mac backend guide](backend/README.md). The default `esp32dev` build remains the local bench diagnostic; `esp32voice` is a separate target.

To start a focus timer from another Mac terminal while the backend is running:

```sh
cd /Users/absolutezero/Projects/ai-desk-buddy/backend
npm run timer -- start 25m
npm run timer -- status
```

The ESP32 must accept the start before success is reported. It then owns the countdown, OLED display, and completion chimes even if Wi-Fi disconnects. See [timer usage and limits](backend/TIMERS.md). Voice timer commands are the next integration step.

## Wire with both USB supplies disconnected

Use the ESP32 board's GPIO labels, not physical header positions. `D26` means GPIO26, and similarly for D25 and D27.

| From | To |
| --- | --- |
| ESP32 GPIO26 / D26 | Amp BCL / BCLK |
| ESP32 GPIO25 / D25 | Amp LRC / LRCLK |
| ESP32 GPIO27 / D27 | Amp DIN |
| Separate USB-C breakout VBUS | Breadboard 5 V rail → amp V / VIN |
| Breakout GND | Common ground rail → ESP32 GND and amp G / GND |
| Speaker two-wire plug | Amp speaker socket **or** speaker across amp + and − screw terminals |

Keep **both speaker leads off ground**: the amplifier has a differential output. Use one speaker connection method, not two speakers. Do not connect the speaker directly to ESP32 GPIOs.

Power the ESP32 through its own onboard USB-C port from the Mac for programming and logs. Power the breakout from a separate 5 V USB supply. Connect their grounds as above, but leave ESP32 VIN/5V and the breakout's D+, D−, CC1, and CC2 header pins open. Do not join the two 5 V power paths. The breakout's advertised onboard CC resistors require no header wiring in this setup.

Add the identified mic using the microphone wiring section below. Add the touch module using the touch wiring section below. OLED wiring is listed below; the ELEGOO listing specifies SSD1306 and 3.3 V / 5 V supply support. Use 3V3 with the ESP32. Leave optional amp gain and shutdown/channel pins at the breakout's factory defaults for this first test. If the board has an SD/EN pin and is silent, confirm that exact breakout's enable wiring before changing it; a held-low shutdown pin mutes the amp.

## Build, upload, listen

Install PlatformIO Core if `pio` is unavailable. The project pins Espressif32 platform **6.12.0**, which uses Arduino-ESP32 **2.0.17** and the ESP-IDF 4.4 legacy I²S driver. The target is `esp32dev` (classic ESP32), not an S2, S3, or C3. First build needs internet access for tools.

```sh
cd /Users/absolutezero/Projects/ai-desk-buddy
# Keep PlatformIO state and downloaded tools within this workspace.
export PLATFORMIO_CORE_DIR="$PWD/.pio-core"
export PLATFORMIO_SETTING_ENABLE_TELEMETRY=no
pio run -e esp32dev
pio device list
```

After checking the wiring, connect both supplies. The project sets upload and monitor ports to `/dev/cu.usbserial-0001`, the confirmed CP2102 USB-to-UART bridge (VID:PID `10C4:EA60`, serial `0001`). Upload identified the connected chip as ESP32-D0WD-V3 revision 3.1. If macOS assigns a different port later, override it using the commands below or update `platformio.ini`.

```sh
pio run -e esp32dev -t upload --upload-port /dev/cu.usbserial-0001
pio device monitor --port /dev/cu.usbserial-0001 --baud 115200
```

If upload stalls at `Connecting...`, hold BOOT, tap EN/RESET, then release BOOT when writing begins. Close other serial monitors before uploading. In the monitor, press EN/RESET to capture the boot log and hear the tone. Or send `t` (lowercase or uppercase; line ending is ignored) to replay without resetting. Exit the monitor with Ctrl+C.

You should hear one approximately 0.6 second A4 beep after a 1.5 second boot delay. The digital amplitude starts at **1% peak (−40 dBFS)** with 20 ms fade-in/out. Actual loudness depends on the breakout gain and speaker. If it is too quiet, first check the wiring and enable state, then adjust `kPeak` in `src/main.cpp` conservatively and rebuild/upload.

Expected application output (ROM boot messages may precede it):

```text
AI Desk Buddy - OLED bring-up
TOUCH GPIO33 raw=LOW stable=RELEASED
Touch test: hold pad, then release. Expect one PRESSED and one RELEASED per hold.
Commands: s = touch status, m = mic meter (starts OFF), t = test tone.
Commands: i = repeat I2C scan, d = local Thinking/Speaking demo.
I2C scan: SDA=21 SCL=22, 100 kHz, 7-bit addresses
I2C device at 0x3C (OLED candidate; controller not identified)
I2C scan complete: 1 device(s), 0 bus error(s)
OLED initialized: SSD1306 128x64 at 0x3C. Confirm visible text.
OLED status: Starting (local bench test)
I2S ready: BCLK=26 LRCLK=25 DOUT=27, 16000 Hz, stereo 32-bit Philips I2S
Mic input: DIN=34, INMP441 L/R strapped to GND (left)
OLED status: Speaking (local bench test)
Tone: 440 Hz, 600 ms, 1% peak (-40 dBFS)
Tone data sent. Confirm sound at the speaker.
Ready. Touch logging ON; send m for microphone levels. No audio is stored or sent.
OLED status: Ready (local bench test)
```

`Tone data sent` confirms only that the driver accepted the samples; it cannot detect an amplifier or prove that the speaker made sound. Errors are printed with the failed I²S operation and ESP error name.

## Audio format and troubleshooting

The ESP32 is I²S master: Philips I²S, 16 kHz word clock, two 32-bit slots (64 BCLKs per frame; nominal BCLK 1.024 MHz), separate data output on GPIO27, no MCLK. Both channels carry the same signed 32-bit PCM, so left/right/average mono selection produces the tone. DMA is followed by silence so the tone drains and does not repeat stale audio. Clocks and zero samples continue after the beep.

TX and RX use one I²S peripheral with shared BCLK/WS and separate data pins. Capture reads both 32-bit slots and removes the low eight bits to obtain signed 24-bit microphone samples. Handle later network PCM conversion independently from these hardware slots.

If logs say data was sent but there is no sound:

1. Confirm amp power is 5 V and all grounds share continuity; check for split breadboard rails.
2. Check GPIO26 → BCL, GPIO25 → LRC, GPIO27 → DIN. Check for reversed headers or loose wires.
3. Confirm speaker connection, amp enable state, and actual MAX98357A marking. A MAX98357B uses a different serial alignment.
4. If available, inspect LRCLK and BCLK with a logic analyzer using the nominal rates above.

## Verification record

- Build: **passed** with `pio run -e esp32dev`, PlatformIO Core 6.2.0 and the pinned platform/framework above. Both `esp32dev` and `esp32voice` build successfully; the voice transport passed end-to-end local echo testing; Realtime session configuration is accepted; audible AI replies have been confirmed by the user. Artifact: `.pio/build/esp32dev/firmware.bin`.
- Upload: **passed** through `/dev/cu.usbserial-0001` with PlatformIO/esptool; flash hashes verified and automatic reset completed. User confirmed amp wiring follows the table.
- Runtime serial check: **passed** for microphone firmware initialization and repeated I²S reads. Captured slot0 RMS ranged from −69.8 to −65.4 dBFS, with varying peaks; slot1 was zero. No I²S errors appeared in the usable capture. This confirms incoming sample data, not speech intelligibility or a quiet-versus-speech response.
- Microphone bench acceptance: **passed** using the user-provided quiet-versus-speech serial log. Slot0 quiet RMS was roughly −68 to −74 dBFS; speech rose to roughly −28 to −43 dBFS, repeatedly returning to the quiet floor. Highest observed peak was −16.7 dBFS (no full-scale clipping indicated). Slot1 remained zero. This verifies signal response; recorded speech intelligibility has not yet been tested.
- Audible hardware playback: **verified by user** — a short tone was heard after reset. Milestone 1 bench acceptance passed.
- Touch firmware: **built and uploaded**, flash hashes verified. Serial boot confirmed `TOUCH GPIO33 raw=LOW stable=RELEASED` and successful audio initialization. User log confirmed eight paired press/release events, with reported holds of 1712, 1784, 1216, 704, 376, 144, 176, and 176 ms. Momentary press/release check passed; the log alone cannot establish whether every shorter event was an intentional touch. A longer continuous hold remains useful before final push-to-talk integration.

- OLED status firmware: **built and uploaded**, flash hashes verified. Serial capture confirmed SSD1306 initialization at 0x3C and the local demo sequence Thinking → Speaking → Ready, without I²S errors. User confirmed the screen works, completing the OLED bench check.
- Voice firmware: built and uploaded with flash verification. ESP32 authenticated to the running Mac backend in local echo mode at 24 kHz. After separating microphone capture from network sends, four physical echo turns completed playback and returned to Ready; the user confirmed Listening holds and playback works. The user subsequently added an API key locally, and OpenAI accepted the Realtime session configuration. The user confirmed audible AI replies work, passing milestone 3 end-to-end acceptance.

- Playback stutter fix: separate I²S playback worker, ~213 ms prebuffer, and a negotiated 16-packet window are built and uploaded. All eleven backend tests pass. User confirmed a live AI reply plays smoothly after the fix; the OLED returned to Ready.

- Focus timer: both builds and all sixteen checks pass. Corrected firmware uploaded with flash verification. A 10-second timer continued and chimed with ESP32 Wi-Fi held off; completed state survived reconnection. The user confirmed the display and both chimes. Typed starts are confirmed only after the device receipt.

## INMP441 microphone wiring and test

The supplied product images identify an INMP441 breakout (can marked `441`, listing names INMP441). Visible board labels are `SCK`, `WS`, `L/R`, `SD`, `VDD`, and `GND`. Capture on the actual unit has passed the level-response bench check below.

With both USB supplies disconnected, add:

| Microphone pin | ESP32 / connection |
| --- | --- |
| VDD | ESP32 3V3, never the 5 V amp rail |
| GND | Common ground |
| SCK | GPIO26 / D26, shared with amp BCL |
| WS | GPIO25 / D25, shared with amp LRC |
| SD | GPIO34 / D34, separate from amp DIN on GPIO27 |
| L/R | GND, selecting the left I²S channel; do not leave floating |

The [TDK INMP441 datasheet](https://invensense.tdk.com/wp-content/uploads/2015/02/INMP441.pdf) specifies 1.8–3.3 V nominal supply, 24-bit signed I²S output, and 64 bit clocks per stereo frame. Our 16 kHz / two 32-bit slot format supplies those clocks. Follow the GPIO mapping above rather than the seller's example GPIO14/15/32 mapping. Keep the small acoustic port unobstructed. The current firmware captures microphone samples, and the user has verified its response to speech with the serial meter.

Open the serial monitor at 115200 baud and send `m` to enable the mic meter (it starts off for a clearer touch test). About twice per second, `MIC` lines show RMS, peak (both in dBFS), and nonzero sample counts for `slot0` and `slot1`. RMS removes the DC component. The meter floors very small values at −120 dBFS. Slot numbers identify DMA positions rather than asserting a left/right memory ordering; only the left channel is driven by this mic strap. An inactive slot may contain undefined data because the mic releases SD outside its selected channel.

Compare a few seconds of quiet with speech near the mic. The active slot should change clearly and repeatedly with speech (for example −65 dBFS becoming −35 dBFS, rather than staying constant). These numbers are illustrative, not pass thresholds. All-zero samples, fixed full-scale values, or nonzero counts alone do not prove capture works. If both slots stay at the floor, recheck 3V3, ground, SD → GPIO34, shared clocks, and L/R → GND with power off. Do not infer success just because the I²S read call succeeds.

Send `m` to pause/resume logging and `t` to replay the speaker tone. RX is continuously drained while logging is paused; readings during the tone and a short settling interval afterward are discarded. This prevents acoustic feedback; captured sound is never routed live to the speaker. Acceptance requires a repeatable quiet-versus-speech change, with no I²S read errors.

After microphone response is confirmed, proceed to independent touch verification and OLED status. Later work will keep the API key, voice, and personality on the Mac backend, with countdown ownership and completion sound on the ESP32.

## Touch module wiring and test

The supplied listing image matches a common TTP223-style module; the IC marking is not readable, so the exact part remains unconfirmed. Visible pins are GND, I/O, and VCC. The [Tontek TTP223 family specification](https://www.tontek.com.tw/product/product2?idx=243&lang=en) includes 3.3 V operation (2.0–5.5 V range for TTP223-BA6). Use ESP32 3V3 so the output is at ESP32 logic voltage.

Disconnect both USB supplies before wiring:

| Touch module pin | Connection |
| --- | --- |
| VCC | ESP32 3V3 |
| GND | Common ground |
| I/O | GPIO33 / D33 |

For the pictured module, leave both A and B solder jumpers open (AB=00): momentary, active-high output according to the supplied listing. Follow the printed labels rather than assuming header orientation. Leave the touch surface untouched for a few seconds after power-up. The required bench result is LOW idle, HIGH throughout a hold, and LOW again on release, repeated across several holds. A toggling output that stays HIGH after release is unsuitable until the jumper setting is corrected. The touch diagnostic configures GPIO33 as a digital input with a weak pulldown and filters changes until stable for 25 ms. It logs edges independently of the mic meter; it does not start a network turn or store audio yet. Touch handling continues even if audio initialization fails. The diagnostic tone is a blocking action, so test touch holds while the tone is idle.

Open `pio device monitor` using the workspace PlatformIO core as above. Leave the pad untouched at boot. Expect `TOUCH GPIO33 raw=LOW stable=RELEASED`. Hold for 2–3 seconds, then release:

```text
TOUCH PRESSED (GPIO33 HIGH)
TOUCH RELEASED (GPIO33 LOW), held 2500 ms
```

The duration is an example. Repeat at least three times, including a longer hold. Send `s` during a hold to check raw HIGH / stable PRESSED, and after release to check raw LOW / stable RELEASED. There should be one edge per press and release, no release during the hold, and no need for a second tap to release. Send `m` to toggle mic levels if needed, and `t` for the speaker test. The user supplied eight paired events, passing the basic momentary check. If any short events occurred while holding continuously, investigate that behavior before push-to-talk integration.

## OLED wiring and status display

Scan firmware build and upload **passed** on retry after the serial monitor was closed; flash hashes verified. Both boot and repeated scans found **one device at 0x3C**, with **zero bus errors**. Audio initialized and touch reported LOW / RELEASED. The subsequently supplied product listing specifies SSD1306 and 3.3 V / 5 V support. Visible display output was confirmed by the user.

The supplied photo labels a 0.96-inch 128×64 I²C OLED with pins GND, VCC, SCL, SDA. The [ELEGOO listing supplied by the user](https://www.amazon.com/dp/B0D2RMQQHR) identifies SSD1306 and specifies 3.3 V / 5 V supplies. User confirmed VCC is connected to ESP32 3V3. The panel has fixed yellow/blue regions, not software-selectable colors.

| OLED pin | Connection |
| --- | --- |
| VCC | ESP32 3V3 (user-confirmed connection) |
| GND | Common ground |
| SCL | GPIO22 / D22 |
| SDA | GPIO21 / D21 |

Firmware scans non-reserved 7-bit addresses at boot, at 100 kHz; send `i` over serial to repeat. The scan uses Wire with a 20 ms transaction timeout, and reports acknowledged addresses and bus errors. Addresses 0x3C/0x3D are labeled OLED candidates, not controller identifications. The board's printed 0x78/0x7A labels use the shifted address convention; Arduino Wire uses 0x3C/0x3D. After the boot scan, the firmware probes 0x3C and initializes the SSD1306 using Adafruit SSD1306 2.5.17, GFX 1.13.0, and BusIO 1.17.4. Missing OLED or allocation failure is logged without disabling the audio/touch diagnostics. This diagnostic scan temporarily blocks normal loop polling, so test touch holds separately from an active scan.

### Display check (`esp32dev` bench target)

Expect a small header in the yellow band and a large `Ready` label in the blue area after the boot tone. Holding the pad shows `Listening`; releasing returns to `Ready`. This is a local hardware check: samples are drained, not stored or sent to a backend. Send `t` for a test tone with `Speaking` on screen. Send `d` for the explicit local demo: `Thinking` for one second, then `Speaking` during a test tone, then `Ready`. The footer labels the demo as local, with no AI. A new touch press cancels a pending Thinking demo. Audio failures show `Audio err`. State changes are rendered once; full frame transfers at 100 kHz can briefly interrupt capture draining, so this remains diagnostic firmware, not the final streaming implementation.

Confirm the text is fully visible and each state follows these actions. Software initialization and serial status logs alone cannot verify actual pixels. The backend will later own real Thinking/reply events.

## References

- [PlatformIO classic ESP32 target](https://docs.platformio.org/en/stable/boards/espressif32/esp32dev.html)
- [Pinned platform release and framework versions](https://github.com/platformio/platform-espressif32/releases/tag/v6.12.0)
- [ESP-IDF 4.4 I²S driver](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-reference/peripherals/i2s.html)
- [MAX98357A/MAX98357B datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX98357A-MAX98357B.pdf)
