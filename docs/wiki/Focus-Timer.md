# Focus timer

[Documentation home](Home.md) · [Protocol](Protocol.md)

The Mac sends a typed command; the ESP32 owns the countdown, display, and completion chime. A successful CLI response is emitted only after the ESP32 accepts the command. Spoken start/status/cancel tools are implemented; see [Spoken timers](Spoken-Timers.md) for behavior and validation status.

With the backend running, use another terminal:

```sh
cd ai-desk-buddy/backend
npm run timer -- start 25m
npm run timer -- status
```

For a quick bench test:

```sh
npm run timer -- start 10s
```

Durations accept whole seconds, minutes, or hours (`10s`, `25m`, `1h`); a number without a suffix means seconds. The valid range is 1 second through 24 hours. Only one timer runs at a time. A second start is rejected while a timer is running; it does not replace or extend the countdown.

The JSON result has `ok:true` and `code:device_confirmed` only after a matching device receipt. It includes the request ID, original timer ID, duration, remaining seconds, and running/completed state. If the connection or acknowledgement is lost after dispatch, the result says `outcome:unknown`: the timer may have started. Check the OLED or query status before starting a new command.

The optional third argument reuses a request ID: `npm run timer -- start 25m REQUEST-ID`. Repeating the most recently accepted ID with the same duration is idempotent within the current ESP32 boot, including after completion; it never restarts that timer. A different duration with the same ID is rejected. This is a single most-recent-command record, not a persistent history of every request ID.

## Local behavior

The clock uses the ESP32's 64-bit monotonic timer. A dedicated task checks the deadline every 20 ms, independently of Wi-Fi, OpenAI, and the main network loop. Remaining time is shown on the OLED. During a voice turn, voice status remains in the large area with the remaining time in the header.

At expiry the ESP32 shows `Time up` and the local playback task plays two chimes. An alarm takes priority over an ongoing voice reply; any interrupted voice session reconnects afterward. A network outage cannot cancel the countdown or require a remote command to play the alarm. Device status and completion notifications to the Mac are best effort while offline; query status after reconnecting for the current state.

The timer survives Wi-Fi/backend outages, **not ESP32 reset or power loss**. Opening a serial monitor may reset this board, so open it before starting a bench timer and leave it open until the test finishes.

## Offline bench controls

In the ESP32 serial monitor (voice build only):

- `w`: disconnect ESP32 Wi-Fi and hold reconnection off. Other network devices and the Mac's Wi-Fi are unaffected.
- `W`: enable ESP32 reconnection again.
- `u`: print local timer state over serial.

Start a 10-second timer from the Mac, confirm device acceptance, send `w`, and verify the OLED continues counting and the chimes sound. Expected serial markers include:

```text
TIMER accepted locally: REQUEST-ID, 10 seconds
WIFI held OFF for offline timer test; timer and local alarm continue
TIMER finished locally: REQUEST-ID
TIMER completion tone played locally
TIMER status: running=0 completed=1 remaining=0 seconds
```

Then send `W`, wait for Ready, and query `npm run timer -- status`. The expected state is completed with zero remaining time. The timer does not restart when Wi-Fi returns.

## Implementation and checks

The control CLI connects to the loopback-only `/control` WebSocket endpoint using the local device token. The server forwards requests to the authenticated timer-capable ESP32, correlates receipts by request ID, and enforces a five-second receipt deadline. It does not infer acceptance from sending the command or from an AI response. The OpenAI API key is never sent to the ESP32 or control client.

`npm test` runs 26 checks, including the firmware clock compiled natively: deadlines, duplicate IDs, busy rejection, 64-bit clock boundaries, one-shot completion, device rejection, invalid input, and lost acknowledgements. Both PlatformIO targets build. Hardware offline completion acceptance **passed** on 2026-11-23. The ESP32 accepted a 10-second command and the Mac received `device_confirmed`. About two seconds later ESP32 Wi-Fi was disconnected and held off. Serial showed the countdown continue from 8 to 0, local expiry, and a completed two-chime sequence. After reconnecting, a typed status query returned completed=true, running=false, and zero remaining seconds for the same timer ID. The user confirmed the second run’s countdown, Time up display, and both chimes. The initial run exposed a corrupted receipt caused by borrowed temporary string storage; receipt strings now have owned storage, and the acknowledged offline retest passed.

## Targeted cancellation

Run `npm run timer -- status` and then `npm run timer -- cancel TIMER-ID` with the returned ID. Success requires a matching cancellation receipt. Cancellation before expiry suppresses that timer’s completion; at the deadline expiry wins. A stale timer ID cannot cancel a newer countdown.
