# Roadmap

[Documentation home](Home.md)

## Completed prototype milestones

1. Classic ESP32 PlatformIO/Arduino project, low-volume boot tone, documented wiring, successful build/upload, audible speaker check.
2. Identified/verified microphone, momentary touch behavior, OLED address/controller/supply information, and visible status.
3. Mac backend, held audio streaming, local echo, Realtime replies, backend-only API key/voice/personality, capture/playback buffering, listening checks.
4. Typed start/status timer commands, device acknowledgement before success, local OLED countdown/chimes, and Wi-Fi-disconnected completion test.

## Completed: spoken timer tools

Spoken start/status/cancel and targeted typed cancellation are implemented. See [Spoken timers](Spoken-Timers.md) for behavior. Both firmware builds and 36 automated tests pass. The user confirmed that spoken start, time-left status, and cancellation all worked, with smooth audio.

Bench acceptance passed for spoken start/status/cancel and smooth audio. The updated firmware also passed physical targeted cancellation and Wi-Fi-disconnected completion checks. Busy and lost-receipt behavior are covered by automated tests; those fault outcomes have not been separately confirmed by listening on hardware.

## Reliability checks (deferred)

A later repeated-turn test exposed microphone queue overflows, playback gaps, and an overlong-reply disconnect. Capture batch draining, larger/refilling playback buffers, packet coalescing, and graceful reply limits are implemented; the user confirmed three successful holds and smooth playback in the hardware retest, with zero logged playback gaps. Longer-hold and network-stress checks remain. See [verification](Verification-and-Troubleshooting.md).

Run longer network outages and repeated voice turns, including busy starts and lost-receipt recovery. Record actual results and confirm there are no false success claims, timer restarts, or audio regressions. Network stress testing is deferred at the user’s request.

## Implemented: API usage and cost reporting

The backend counts final-response usage for silent planning and spoken replies, applies a verified pricing snapshot, and provides `npm run usage` for turn/session/process totals. Missing usage and unknown prices remain explicitly incomplete. All 36 tests pass. Live verification also passed: the user confirmed hearing the reply and the authenticated usage report returned a complete estimate. See [Usage and costs](Usage-and-Costs.md).

## Current: Wi-Fi setup from a phone

Add a local setup flow so changing Wi-Fi does not require editing `secrets.h` or reflashing. The firmware implementation and 36 automated checks are complete; phone/hardware acceptance is pending.

1. Hold the touch pad during startup to explicitly enter setup mode.
2. Show a temporary, password-protected access point's name and setup instructions on the OLED.
3. Connect a phone and use a local browser page to select a 2.4 GHz network and enter its password.
4. Test the new connection before replacing the working configuration, save successful settings in device storage, and close setup mode.

Define a timeout, retry path, and recovery flow for invalid credentials or an unavailable network. Keep credentials out of logs and source control. Preserve normal short boot behavior and avoid triggering setup during ordinary push-to-talk holds. Choose the provisioning implementation against the pinned ESP32 framework before coding.

Acceptance: provision a fresh device, change networks without reflashing, reject an incorrect password with a useful message, recover access to setup, retain credentials across restart, and verify voice/audio and local timers after setup.

## Following: remote backend hosting

Move the existing Node backend to an always-running remote server so the Mac is no longer required for voice replies. API credentials, model, voice, personality, and usage reporting remain backend-owned; accepted countdowns and completion chimes remain on the ESP32. This is planned, not deployed.

- Select hosting, a stable hostname, and a deployment/restart/logging approach.
- Replace the trusted-LAN plaintext device link with `wss://` and certificate validation; define certificate trust and clock initialization on the ESP32.
- Retain device authentication and provide a way to rotate/revoke device credentials. Keep backend secrets on the server and admin controls protected; do not expose the current loopback control endpoint as a public admin interface.
- Configure the device's server destination independently of its Wi-Fi credentials.
- Validate WebSocket proxy timeouts, reconnect behavior, internet latency/jitter, audio buffering, and memory headroom for TLS on the original ESP32.

Acceptance: voice replies work with the Mac off, invalid certificates/credentials are rejected, interrupted turns recover without replay, device receipts still govern timer success, and an accepted timer finishes after internet/server loss. Record the added hosting cost separately from API estimates. Choose a provider and budget before provisioning a paid service.

Network stress testing remains deferred; remote-hosting acceptance will require targeted internet connection and audio checks when that milestone is undertaken.

## Later work

| Area | Work still needed |
| --- | --- |
| Timer durability | Decide whether reset/power-loss persistence is required; add durable state/time source only if needed |
| Reliability | Longer network-outage and repeated-turn runs; distinguish provider gaps from device underruns |
| Cost visibility | Process-local usage/cost reporting is implemented; persistent history and account-wide billing are not included |
| Operation | Optional managed backend startup and clearer recovery UX; currently `npm start` must remain running |
| Interaction | Decide whether barge-in is wanted; currently a reply cannot be interrupted by a fresh touch |
| Hardware | Confirm exact touch IC; document the exact dev board VIN/USB power path and UART0 bridge wiring |
| Enclosure | Speaker/mic placement, acoustic isolation, strain relief, touch surface and thermal/power planning |
| Single cable | Design and verify power/USB routing; the separate breakout is not a native-USB bridge for classic ESP32 |
| Personality | Refine backend prompt/voice for the eventual HAL-inspired build |

Changing hardware pins, I²S rates/slots, or playback/capture flow requires repeating the relevant bench checks. A successful unit test alone cannot establish audible quality or safe physical wiring.
