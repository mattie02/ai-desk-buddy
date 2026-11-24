# AI Desk Buddy

A breadboard voice companion built with an original ESP32, an I²S microphone and speaker amplifier, a capacitive touch pad, and an OLED. Hold the pad to speak, release it, and hear an OpenAI Realtime reply through the speaker.

The first device action is a focus timer controlled from the Mac terminal. Its countdown, display, and completion chimes run on the ESP32 and finish even if Wi-Fi disconnects. Spoken start/status/cancel tools are implemented; see the validation status in the [spoken timer guide](docs/wiki/Spoken-Timers.md).

## Documentation

Read the [GitHub wiki](https://github.com/mattie02/ai-desk-buddy/wiki) or the versioned [documentation home](docs/wiki/Home.md).

| Guide | Contents |
| --- | --- |
| [Hardware and wiring](docs/wiki/Hardware-and-Wiring.md) | Parts, GPIO map, power arrangement, module identification |
| [Setup and operation](docs/wiki/Setup-and-Operation.md) | Fresh checkout, local configuration, build, upload, echo, AI replies |
| [Architecture](docs/wiki/Architecture.md) | ESP32 tasks, Mac backend, audio buffers, timer ownership |
| [Protocol](docs/wiki/Protocol.md) | Audio flow, playback credits, timer requests and receipts |
| [Spoken timers](docs/wiki/Spoken-Timers.md) | Voice commands and receipt-before-speech behavior |
| [Focus timer](docs/wiki/Focus-Timer.md) | Commands, offline test, limits and failure semantics |
| [Verification and troubleshooting](docs/wiki/Verification-and-Troubleshooting.md) | Proven bench results, automated checks, fixes and remaining limits |
| [Roadmap](docs/wiki/Roadmap.md) | Next work, including spoken timer tools |
| [Maintaining the docs](docs/wiki/Maintaining-the-Docs.md) | Updating and publishing the wiki from its versioned sources |

## Current state

As of November 23, 2026:

- Boot tone, microphone capture, touch press/release, and OLED status are bench verified.
- Local echo and live AI replies are bench verified.
- Dedicated capture/playback tasks and buffered playback resolved the reported disconnect and stutter issues in physical retests.
- A typed 10-second timer completed with ESP32 Wi-Fi held off; the user confirmed the countdown and both chimes. Its completed state remained after reconnecting.
- Both PlatformIO targets build, and the last verification run passed all **26 tests**.
- Spoken timer tools and targeted typed cancellation are implemented; the user confirmed spoken start/status/cancel and smooth audio. Persistence across power loss is not implemented.

## Quick start

Prerequisites: PlatformIO Core, Node.js 22 or newer, a 2.4 GHz Wi-Fi network, and the wired hardware. The native timer test also requires a C++ compiler.

```sh
git clone https://github.com/mattie02/ai-desk-buddy.git
cd ai-desk-buddy/backend
npm ci
npm run setup
```

Edit the generated `include/secrets.h` and `backend/.env` locally. Setup creates matching device tokens and starts with local echo mode. See the [setup guide](docs/wiki/Setup-and-Operation.md) before powering or uploading.

Start the backend from `backend/` with `npm start`. In another terminal, from the project root:

```sh
export PLATFORMIO_CORE_DIR="$PWD/.pio-core"
export PLATFORMIO_SETTING_ENABLE_TELEMETRY=no
pio run -e esp32voice -t upload
pio device monitor -e esp32voice
```

The default `esp32dev` target is the offline bench diagnostic; `esp32voice` is the Wi-Fi voice and timer build. Close other serial monitors before uploading.

## Start a focus timer

With the backend running, use another terminal in `backend/`:

```sh
npm run timer -- start 25m
npm run timer -- status
```

Success is reported only after ESP32 acceptance. One timer can run at a time. It survives Wi-Fi/backend outages, **not device reset or power loss**. See [timer behavior](docs/wiki/Focus-Timer.md).

## Configuration stays local

The OpenAI API key, model, voice, personality, and reply gain belong on the Mac. Wi-Fi credentials and the device token are in the local firmware configuration. Real `.env` files, `include/secrets.h`, dependency folders, and build artifacts are gitignored; tracked examples contain no credentials.

The ESP32-to-Mac link is an authenticated plaintext LAN WebSocket; the Mac-to-OpenAI link uses TLS. Use a trusted bench LAN and do not expose port 8765 to the internet. AI replies use a generated voice. The backend does not save audio files or log transcripts.

## Tests

```sh
cd backend
npm test
```

The tests use localhost WebSockets, a mocked Realtime connection, and the firmware timer clock compiled natively. They do not call OpenAI. Physical playback and offline timer acceptance are separate bench checks documented in the [verification guide](docs/wiki/Verification-and-Troubleshooting.md).
