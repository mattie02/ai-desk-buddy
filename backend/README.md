# Mac voice backend

Milestone 3 implementation: ESP32 push-to-talk over a local WebSocket, bridged on the Mac to OpenAI Realtime. Voice, personality, model, and reply gain live here. The ESP32 never receives an OpenAI API key.

The proven `esp32dev` target remains the offline bench diagnostic. The separate `esp32voice` target uses Wi-Fi and 24 kHz audio. Build success and mocked tests do not establish audible end-to-end behavior; run the echo bench check first.

## Local setup

Node.js 22 or newer is required (this Mac has Node 24). Dependencies have already been installed and `.env` plus `../include/secrets.h` have already been generated for this workspace. Both files are gitignored and created with owner-only permissions. The matching device token is generated locally; do not paste it or your Wi-Fi password into chat.

For a fresh checkout only:

```sh
cd /Users/absolutezero/Projects/ai-desk-buddy/backend
npm ci
npm run setup
```

Setup refuses to overwrite either existing configuration file. It generates a matching random token in each file and fills in a unique Mac LAN IPv4 address when available. If the files already exist, edit them directly instead of rerunning setup.

Edit `include/secrets.h` in the project root:

- Fill in `BUDDY_WIFI_SSID` and `BUDDY_WIFI_PASSWORD` for a 2.4 GHz network. Escape quotes/backslashes if they appear in a C++ string.
- Check `BUDDY_SERVER_HOST`: it must be the Mac's LAN IPv4 address, not localhost. Setup found `192.168.1.43` on this Mac; it can change with DHCP.
- Leave port 8765 and the generated token in place. The token must match `DEVICE_TOKEN` in `backend/.env`.

Keep the Mac and ESP32 on the same LAN without wireless client isolation. If macOS asks whether Node may accept incoming network connections, allow it for this bench server. The device-to-Mac connection is an authenticated **plaintext LAN** WebSocket: use a trusted bench network and do not expose port 8765 to the internet. Mac-to-OpenAI uses TLS.

## First: local echo, no API key

The generated `.env` starts with `BACKEND_MODE=echo`. Leave `OPENAI_API_KEY` blank for this check. The backend buffers at most 15 seconds of a turn in memory, then streams it back after release; it saves no audio files.

Start the backend in one terminal:

```sh
cd /Users/absolutezero/Projects/ai-desk-buddy/backend
npm start
```

In another terminal, close any existing serial monitor with Ctrl+C before uploading:

```sh
cd /Users/absolutezero/Projects/ai-desk-buddy
export PLATFORMIO_CORE_DIR="$PWD/.pio-core"
export PLATFORMIO_SETTING_ENABLE_TELEMETRY=no
pio run -e esp32voice -t upload
pio device monitor -e esp32voice
```

Expected sequence:

1. Boot tone and OLED startup, then `Offline`/`Connecting` while Wi-Fi/backend connects.
2. Serial prints `Voice Ready (local echo)`; OLED shows `Ready`, footer `Local echo test`.
3. Hold the pad and speak a short phrase. OLED shows `Listening`.
4. Release. OLED shows `Thinking`, then `Speaking` as your recorded phrase plays, then `Ready` after DMA drains and the backend accepts completion.

Try three turns. Verify understandable speech at the right speed, no missing tail, no repeated audio, and one reply per release. `Thinking` can be too brief to see in echo mode. Very short taps (less than 100 ms of samples) produce a notice and return to Ready without playback. Holds end automatically at 15 seconds. Wait until Ready before starting another turn; this first pass does not implement interrupting an ongoing reply.

Reply volume defaults to `OUTPUT_GAIN=0.06` on the Mac. This bench setup now uses `OUTPUT_GAIN=0.5` and `ECHO_NORMALIZE=1` at the user's request. Echo recordings are corrected toward an 80% digital peak before applying the master gain, with a maximum 16× correction. Near-silent recordings are not boosted, and one correction factor applies to the whole turn to avoid changes between packets. This correction applies only to recorded echo audio, not generated Realtime replies. The master gain accepts 0–1. Change `.env` and restart the backend to adjust it. The local boot tone retains its original 1% peak setting.


## Then: Realtime replies

Create/use an API key with access to the configured Realtime model and API billing. Add it **locally** to `backend/.env` as `OPENAI_API_KEY=...`, change `BACKEND_MODE=realtime`, then restart `npm start`. Do not put the key in `secrets.h`, command arguments, source control, logs, or chat. An API account's model access/billing can differ from a ChatGPT subscription.

The default model is `gpt-realtime-2.1` and voice is `marin`, matching the current official examples checked during implementation. Both are configured in `.env`. Personality and conversational rules are in `personality.txt`; edit and restart to apply. A reconnect creates a fresh OpenAI session and loses previous conversation context. Typed timer commands are available through the local CLI. No voice timer tools are enabled yet, and the prompt explicitly prevents claiming that a timer was started from speech.

With `Voice Ready (Realtime)`, hold the pad, say a short question, release, and hear the answer. The mic streams only during an accepted hold; no automatic voice-activity trigger runs. Reply audio comes back incrementally; the full reply is not buffered on the ESP32.

Expected OLED transitions are Listening → Thinking → Speaking → Ready. The user should be informed that replies use an AI-generated voice. Prompts, model, voice, and gain remain backend settings.

## Protocol and limits

- Device endpoint: `ws://MAC_LAN_IP:8765/buddy`; one authenticated device per backend process.
- First device control: `{ "type": "hello", "token": "...", "sample_rate": 24000 }`. Backend sends Ready only after local echo is available or Realtime acknowledges session configuration.
- Device sends `begin` with a monotonically increasing integer `turn`, binary PCM frames while held, then `end` with the same turn on release. Audio is signed little-endian PCM16, mono, 24 kHz. Input packets are up to 2048 bytes (1024 samples, approximately 43 ms), with a shorter final packet on release; the server rejects frames over 4096 bytes.
- Realtime uses `session.update` with automatic turn detection disabled; received PCM is base64-encoded into `input_audio_buffer.append`. Release commits input. Only after `input_audio_buffer.committed` does the backend send `response.create`.
- `response.output_audio.delta` is decoded and attenuated on the Mac. Reply chunks are at most 1024 bytes. The new firmware advertises a 16-packet credit window; older firmware defaults to one. A dedicated playback task drains a bounded 32-packet queue into I²S independently of Wi-Fi. Playback starts after about 213 ms of PCM is buffered (or end-of-audio / a 500 ms startup fallback), and consumed-packet credits are batched back to the Mac from the network loop.
- After `response.done` and dispatching all queued reply bytes, the backend sends `finish`, even if consumption credits are outstanding. This lets replies shorter than the prebuffer start without deadlocking. ESP32 drains its queue and DMA with silence, sends any final batched credits, then returns `playback_done`. The backend rejects completion while credits are outstanding. Backend then permits the next turn. An API event marking generation complete is not treated as audible playback complete.
- Maximum input 15 seconds; maximum generated audio 30 seconds; turn/reply buffers and deadlines are bounded. A slow consumer, malformed messages, API error, missing acknowledgement, or Wi-Fi drop aborts the session rather than silently dropping samples or replaying a partial turn.
- WebSocket heartbeat detects stale connections. Device reconnects automatically, but an interrupted hold is discarded; release the pad and start a fresh turn once Ready. Keys, audio payloads, and transcripts are not logged; logs contain connection/error codes and duration/completion counts.

Voice firmware uses Philips I²S, 24 kHz word clock, stereo 32-bit slots (64 clocks per frame; BCLK 1.536 MHz). It takes the previously verified mic slot0 and converts high-aligned PCM24 into PCM16 separately from hardware slot configuration. Reply PCM16 is expanded into both 32-bit output slots. Eight 128-frame DMA blocks cover approximately 43 ms at 24 kHz; OLED transfers use 400 kHz during a frame update. The bench target keeps its proven 16 kHz/100 kHz settings. Microphone DMA is drained by a dedicated priority-2 capture task, independently of OLED updates and network sends. A 64-block PCM16 queue gives approximately 341 ms of bounded network headroom. On release, the producer stops tagging the turn, queued audio and the partial network packet are flushed, then input is committed. Actual capture-task stalls beyond the DMA margin or queue overflow abort the turn; ordinary main-loop send delays do not. The idle voice meter uses PCM16 samples; the bench target retains its original PCM24 meter. The mic meter is suppressed during turns. Local `t`/`d` audio demos are available only in the bench target, so they cannot accidentally capture a tone into a network turn. Clock-rate changes and network timing still need actual hardware verification.

## Verification

```sh
cd /Users/absolutezero/Projects/ai-desk-buddy/backend
npm test
```

The protocol tests use real localhost WebSockets and a mocked Realtime connection: ordered PCM/gain/playback completion, short taps, invalid authentication, unexpected playback acknowledgements, manual turn/commit sequencing, and recovery after upstream errors. They do not contact OpenAI or incur API charges.

Both firmware environments build. All sixteen tests pass (including the native firmware-clock test), covering timer receipts and low-level speech correction, clipping, delayed batched playback credits, short replies, and rejecting premature completion. The generated echo-mode configuration also passed a listener start/stop smoke check on port 8765 with matching device tokens verified without displaying them.

The voice firmware uploaded with flash verification. After the capture timing fix, four physical turns (2.04, 3.41, 0.13, and 2.98 seconds of received audio) each completed playback and returned to Ready. The user confirmed that Listening holds and audible playback works. No capture errors appeared in that retest; startup reported 210,892 bytes of free heap.

The user has now configured an API key locally. The backend is running in **Realtime mode** under the Codex task session, and OpenAI acknowledged the session configuration for `gpt-realtime-2.1` with voice `marin`. The user confirmed that an audible AI reply works and the device returns to Ready, passing milestone 3 end-to-end acceptance. Typed focus timer commands are implemented; see [TIMERS.md](TIMERS.md) for usage and offline behavior. Spoken timer tools are not connected yet.

## References checked for this integration

- [OpenAI developer quickstart](https://developers.openai.com/api/docs/quickstart)
- [Realtime WebSocket connection](https://developers.openai.com/api/docs/guides/realtime-websocket)
- [Realtime conversations and push-to-talk](https://developers.openai.com/api/docs/guides/realtime-conversations)
- [Realtime API event reference](https://developers.openai.com/api/reference/resources/realtime)

The optional OpenAI Developers plugin can provide deeper implementation-documentation help in Codex; it is not required to run this backend.

## Current bench observation

The initial Offline report was traced to a reproducible firmware abort during a hold (`audio upload too slow`), not invalid saved Wi-Fi settings. The post-fix physical retest passed; see the verification record above.

## Capture timing fix

A physical hold reproduced `Voice: audio upload too slow` in serial diagnostics: the original main-loop 32 ms timing guard was too strict for normal Wi-Fi sends. The capture worker and ~43 ms network packets replace that guard. Both firmware targets build, and the backend tests pass with full packets plus a final partial packet. Post-fix physical playback passed four turns and was confirmed by the user.

## Echo loudness adjustment

The first raw-microphone gain increase from 0.06 to 0.24 remained too quiet according to the user. Whole-turn echo level correction and master gain 0.5 are now applied. Digital-level tests pass, and the user confirmed the volume is good after this change.

## Playback stutter fix

The user supplied IMG_1946.MOV showing intermittent playback stutter. The previous playback callback wrote one ~21 ms packet to I²S and synchronously acknowledged it before the next packet could arrive. That exposed the speaker feed to Wi-Fi round-trip delays. The updated dedicated playback task, ~213 ms prebuffer, 16-packet credit window, and batched credits decouple playback from network timing. The worker never polls touch or sends WebSocket messages; those stay on the main loop. Playback queue waits longer than DMA headroom are counted and reported on completion for diagnostics. Both firmware targets build and all eleven backend tests pass. The updated firmware uploaded with flash verification, negotiated playback window 16, and completed a live Realtime reply. The user confirmed playback is smooth. Diagnostics reported one queue wait (262 ms), so this bench pass does not establish zero network/provider gaps in every condition; it confirms the reported stutter improved in the physical retest. Startup free heap was 173,292 bytes.
