# Spoken focus timers

[Documentation home](Home.md) · [Focus timer](Focus-Timer.md)

With the updated voice firmware and Realtime backend, wait for Ready, hold the pad, speak, and release:

- “Start a two-minute focus timer.”
- “How much time is left?”
- “Cancel the timer.”

Use one action per turn. Give an explicit duration from 1 second through 24 hours. A running timer is never replaced by a new start. Cancellation stops a countdown; it does not silence a completion alarm that has already expired.

## Spoken durations

Time-left replies use hours, minutes, and seconds, skipping zero units. For example, 104 seconds becomes “1 minute and 44 seconds”; 3605 seconds becomes “1 hour and 5 seconds”; exactly 60 seconds becomes “1 minute”. The backend computes `remaining_spoken` and `duration_spoken` from the device receipt; personality instructions use those phrases. A completed or cancelled timer is described by state instead of as an active countdown.

## Receipt before speech

The backend first requests a silent, text-only planning response with three validated function schemas: `start_focus_timer`, `get_focus_timer`, and `cancel_focus_timer`. It waits for the complete function call in `response.done`; partial argument deltas never execute commands.

Start and status share the terminal client's correlated request/receipt path. Cancel first reads ESP32 state, then sends a cancellation targeted to that exact timer ID. A delayed cancel cannot stop a later timer. Firmware advertises `timer_cancel_support:1`; voice tools are enabled only with this capability and timer support.

Only after the receipt or explicit failure does the backend submit `function_call_output` and request the audible answer with tools disabled. Planning audio is never forwarded to the speaker. Ordinary questions also pass through this short silent planning step, which adds a model response and some latency/cost. API credentials, voice, personality, and speech gain remain on the Mac.

A five-second missing-receipt deadline produces an unknown outcome, not success or an automatic retry. Device disconnect after dispatch also leaves an uncertain outcome; an interrupted voice turn may end without a spoken answer. Check the OLED and query status after reconnecting. Repeated model response events do not repeat the action. Multiple tool calls in a single plan are rejected for clarification.

## Cancellation rules

Successful cancellation sets `cancelled:true`, `running:false`, `completed:false`, and zero remaining seconds. Repeating a cancellation for the same still-recorded timer is idempotent. Repeating that timer's original start ID does not revive it. New start IDs can start another countdown afterward. At or after the deadline, expiry wins and the local alarm still plays. All records remain RAM-only.

The terminal also supports targeted cancellation:

```sh
npm run timer -- status
npm run timer -- cancel TIMER-ID
```

Replace `TIMER-ID` with the ID returned by status. This explicit target prevents an old terminal command from cancelling a new timer.

## Validation

The implementation passes 35 automated checks, including delayed/lost receipts, spoken start/status/cancel routing, malformed arguments, cancellation targeting, expiry boundaries, duplicate handling, and audio completion regressions. Both firmware targets build. On November 23, 2026, the user confirmed that spoken start, time-left status, and cancellation all worked and audio was smooth. This physical listening check is separate from the automated mocks.

Implementation follows the [OpenAI Realtime function-calling documentation](https://developers.openai.com/api/docs/guides/realtime-conversations#function-calling), checked November 23, 2026.
