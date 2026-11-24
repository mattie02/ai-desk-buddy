# Setup and operation

[Documentation home](Home.md) · [Hardware wiring](Hardware-and-Wiring.md)

## Prerequisites

- Wired classic ESP32 and audio modules using the documented two-supply bench arrangement.
- PlatformIO Core (`pio`) and Node.js 22 or newer. The development Mac used Node 24 and PlatformIO Core 6.2.0.
- A C++ compiler for the native timer test (`c++`; on macOS this normally comes from Command Line Tools).
- A 2.4 GHz Wi-Fi network shared by the Mac and ESP32, without wireless client isolation.
- An OpenAI API key and API billing only when enabling live AI replies. Local echo needs neither.

## Fresh checkout and local configuration

```sh
git clone https://github.com/mattie02/ai-desk-buddy.git
cd ai-desk-buddy/backend
npm ci
npm run setup
```

Setup generates `backend/.env` and `include/secrets.h` with matching random device tokens and owner-only file permissions. It attempts to fill in a unique Mac LAN IPv4 address. It refuses to overwrite either existing file.

Edit `include/secrets.h` locally:

| Setting | Value |
| --- | --- |
| `BUDDY_WIFI_SSID` | Optional legacy 2.4 GHz Wi-Fi name |
| `BUDDY_WIFI_PASSWORD` | Optional legacy Wi-Fi fallback; saved phone setup takes priority. Escape quotes/backslashes in a C++ fallback string |
| `BUDDY_SERVER_HOST` | Mac LAN IPv4 address, not `localhost`; recheck after DHCP/network changes |
| `BUDDY_SERVER_PORT` | 8765 unless changed on both sides |
| `BUDDY_DEVICE_TOKEN` | Generated token, matching `DEVICE_TOKEN` in `.env` |

Real credentials stay out of chat, commits, and documentation. The tracked `.env.example` and `secrets.example.h` contain empty credential placeholders. Build artifacts and generated dependency folders are also ignored by Git.

You can instead leave the Wi-Fi fallback fields blank and use [phone Wi-Fi setup](Wi-Fi-Setup.md). Hold the pad through EN/RESET to change an existing profile; keep holding until the OLED shows AP/PW instructions. Backend host/token configuration is still required.

## Backend settings

Edit `backend/.env` locally and restart the backend after changes:

| Setting | Purpose / default |
| --- | --- |
| `BACKEND_MODE` | Generated config starts with `echo`; use `realtime` for AI replies |
| `OPENAI_API_KEY` | Mac-only API credential; leave blank for echo |
| `DEVICE_TOKEN` | Authenticates the device and local timer control client |
| `HOST` / `PORT` | Listener address `0.0.0.0`, port 8765 |
| `OPENAI_REALTIME_MODEL` | Configured default `gpt-realtime-2.1` |
| `OPENAI_VOICE` | Configured default `marin` |
| `OUTPUT_GAIN` | Master PCM gain 0–1; example defaults to 0.06; tested bench setting is 0.5 |
| `ECHO_NORMALIZE` | Enabled unless `0`; corrects recorded echo levels, not AI replies |

Personality and device-action restrictions live in `backend/personality.txt`. Firmware never receives the OpenAI key. Model, voice, personality, and output gain are backend settings. A reconnect creates a new Realtime session and loses prior conversation context.

The device uses an authenticated plaintext LAN WebSocket. The Mac connects to OpenAI over TLS. Keep port 8765 on a trusted bench LAN; do not expose it to the internet. Allow Node's incoming connections if macOS requests that permission.

## Build and upload

From the repository root:

```sh
export PLATFORMIO_CORE_DIR="$PWD/.pio-core"
export PLATFORMIO_SETTING_ENABLE_TELEMETRY=no
pio run -e esp32dev -e esp32voice
pio device list
pio run -e esp32voice -t upload
pio device monitor -e esp32voice
```

Both environments target original ESP32 `esp32dev`. `esp32dev` is the default local bench diagnostic; **use `esp32voice` for Wi-Fi voice and timers**. The voice build still compiles with the empty example configuration, but shows Setup until local settings are filled in.

The checked development port is `/dev/cu.usbserial-0001` (CP2102). If yours differs, override `--upload-port` / `--port`, or edit `platformio.ini`. Close other serial monitors before upload. If connecting stalls, hold BOOT, tap EN/RESET, then release BOOT when writing starts. Firmware uploads and monitors use 115200 baud.

## First run: local echo

Set `BACKEND_MODE=echo`, then run from `backend/`:

```sh
npm start
```

Keep the Mac awake and the backend process running. After the boot tone and network connection, expect `Voice Ready (local echo)` and OLED Ready. Hold the pad and speak; release to hear the recording. Wait for Ready before another turn. Echo makes no OpenAI calls and saves no audio files.

The default audio gain is deliberately quiet. The accepted bench setting is `OUTPUT_GAIN=0.5`, with echo level correction enabled. Echo correction targets an 80% digital peak before master gain, caps the correction at 16×, avoids boosting near silence, and uses one gain per turn.

## Live AI replies

Add the API key locally, set `BACKEND_MODE=realtime`, and restart `npm start`. Do not put the key in firmware. Wait for the backend log `Realtime session configured` and device Ready, then hold, ask a question, and release. Reply speech is AI-generated.

Normal states: Listening → Thinking → Speaking → Ready. Holds are capped at 15 seconds. Less than 100 ms of captured samples produces a notice instead of a reply. Wait for playback completion; this prototype does not implement interrupting an ongoing AI reply. Reconnects discard an interrupted turn rather than resuming it.

## Timer and diagnostic commands

In another terminal in `backend/`:

```sh
npm run timer -- start 25m
npm run timer -- status
```

See [Focus timer](Focus-Timer.md) for acknowledgement and offline semantics. Timer starts/status do not generate an OpenAI response.

| Serial key | Voice build | Bench build |
| --- | --- | --- |
| `s` | Touch raw/stable state | Same |
| `m` | Toggle mic meter, suppressed during voice turns | Toggle mic meter |
| `i` | I²C scan when idle | I²C scan |
| `u` | Local timer state | Empty/uninitialized timer state |
| `w` | Disconnect and hold ESP32 Wi-Fi off | No action |
| `W` | Enable ESP32 reconnection | No action |
| `t` | No manual tone command | Replay boot tone |
| `d` | No local status demo | Thinking/Speaking demo and test tone |

Opening a serial port can reset this board. Open the monitor before starting a timer and leave it open through an offline test.

## Read API usage

From `backend/`, run `npm run usage` or `npm run usage -- --json`. These commands query the running Mac backend and make no API call. See [Usage and costs](Usage-and-Costs.md) for per-turn/session totals, pricing, and reset behavior.
