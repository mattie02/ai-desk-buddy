# AI Desk Buddy documentation

**Snapshot: November 23, 2026 — spoken timer implementation; hardware voice acceptance pending.**

AI Desk Buddy is a working breadboard prototype: hold a capacitive touch pad, speak, release it, and hear an AI reply. An ESP32 drives the microphone, amplifier, speaker, OLED, and local focus countdown. A Mac handles network sessions, OpenAI credentials, voice, personality, and reply gain.

## What works

| Capability | Current evidence |
| --- | --- |
| Quiet boot tone | Heard through the included speaker |
| INMP441 capture | Quiet/speech level changes and intelligible echo verified |
| Touch hold/release | Paired edges verified; used for accepted voice turns |
| SSD1306 OLED | Visible status text and countdown confirmed |
| Local echo | Four logged turns completed after the capture fix |
| OpenAI Realtime voice | Audible AI replies confirmed |
| Buffered playback | User reported smooth playback after the stutter fix |
| Typed local focus timer | 10-second countdown and two chimes verified with ESP32 Wi-Fi held off |
| Automated checks | Last run: 26 tests passed; both firmware targets build |

Spoken timer tools and targeted typed cancellation are implemented; hardware voice acceptance is pending. **Not implemented:** timer persistence across reset/power loss and an enclosure/single-cable design.

## Find the right guide

- [Hardware and wiring](Hardware-and-Wiring.md): exact GPIO and power connections.
- [Setup and operation](Setup-and-Operation.md): run a fresh checkout, then echo and Realtime.
- [Architecture](Architecture.md): how the ESP32 and Mac divide the work.
- [Protocol](Protocol.md): messages, PCM formats, credits, and timer receipts.
- [Spoken timers](Spoken-Timers.md)
- [Focus timer](Focus-Timer.md): commands, offline behavior, and limitations.
- [Verification and troubleshooting](Verification-and-Troubleshooting.md): evidence, regressions, and practical checks.
- [Roadmap](Roadmap.md): next changes and acceptance criteria.
- [Maintaining the docs](Maintaining-the-Docs.md): source files and wiki publication.

## Everyday commands

Run the backend from the repository's `backend/` directory:

```sh
npm start
```

In a second terminal in the same directory:

```sh
npm run timer -- start 25m
npm run timer -- status
```

Wait for `Ready` before holding the pad for a voice turn. See [Spoken timers](Spoken-Timers.md) for voice commands and validation status.

## Project boundaries

This is a local-network prototype intended to inform a later HAL-inspired build. It is not an always-listening assistant. Voice and personality are backend settings, not firmware constants. Voice service needs the Mac and network; an accepted focus countdown does not.

Repository: [mattie02/ai-desk-buddy](https://github.com/mattie02/ai-desk-buddy).
