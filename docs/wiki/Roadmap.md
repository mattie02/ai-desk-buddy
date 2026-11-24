# Roadmap

[Documentation home](Home.md)

## Completed prototype milestones

1. Classic ESP32 PlatformIO/Arduino project, low-volume boot tone, documented wiring, successful build/upload, audible speaker check.
2. Identified/verified microphone, momentary touch behavior, OLED address/controller/supply information, and visible status.
3. Mac backend, held audio streaming, local echo, Realtime replies, backend-only API key/voice/personality, capture/playback buffering, listening checks.
4. Typed start/status timer commands, device acknowledgement before success, local OLED countdown/chimes, and Wi-Fi-disconnected completion test.

## Current milestone: spoken timer tools

Spoken start/status/cancel and targeted typed cancellation are implemented. See [Spoken timers](Spoken-Timers.md) for behavior. Both firmware builds and 26 automated tests pass. Hardware acceptance of the new voice commands is pending.

Acceptance still requires spoken start/status/cancel, truthful busy/uncertain outcomes, countdown survival after Wi-Fi loss, and a smooth-audio regression check. The previously verified offline countdown remains a separate earlier bench result.

After this acceptance, the next reliability work is longer outages and repeated voice turns, followed by usage/cost reporting from actual API responses.

## Later work

| Area | Work still needed |
| --- | --- |
| Timer durability | Decide whether reset/power-loss persistence is required; add durable state/time source only if needed |
| Reliability | Longer network-outage and repeated-turn runs; distinguish provider gaps from device underruns |
| Cost visibility | Add usage/cost reporting from actual API responses rather than relying on rough estimates |
| Operation | Optional managed backend startup and clearer recovery UX; currently `npm start` must remain running |
| Interaction | Decide whether barge-in is wanted; currently a reply cannot be interrupted by a fresh touch |
| Hardware | Confirm exact touch IC; document the exact dev board VIN/USB power path and UART0 bridge wiring |
| Enclosure | Speaker/mic placement, acoustic isolation, strain relief, touch surface and thermal/power planning |
| Single cable | Design and verify power/USB routing; the separate breakout is not a native-USB bridge for classic ESP32 |
| Personality | Refine backend prompt/voice for the eventual HAL-inspired build |

Changing hardware pins, I²S rates/slots, or playback/capture flow requires repeating the relevant bench checks. A successful unit test alone cannot establish audible quality or safe physical wiring.
