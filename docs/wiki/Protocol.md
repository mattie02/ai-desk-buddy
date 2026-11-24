# Protocol

[Documentation home](Home.md) · [Architecture](Architecture.md)

## Connections and trust

| Connection | Endpoint | Protection |
| --- | --- | --- |
| ESP32 → Mac | `ws://MAC_LAN_IP:8765/buddy` | Device token, trusted LAN; plaintext WebSocket |
| Timer CLI → Mac | `ws://127.0.0.1:8765/control` | Loopback-address check and same local device token |
| Mac → OpenAI | Realtime WebSocket | TLS and Mac-only API key |

The backend allows one authenticated device. Binary/text payloads from clients are capped at 4096 bytes. The device accepts reply packets no larger than 1024 bytes and rejects fragmented backend frames. Tokens shown below are placeholders, never real credentials.

## Device handshake

```json
{
  "type": "hello",
  "token": "GENERATED_DEVICE_TOKEN",
  "sample_rate": 24000,
  "playback_window": 32,
  "timer_support": 1,
  "timer_cancel_support": 1
}
```

The backend validates the token and sample rate. It accepts an integer playback window of 1–32; missing means legacy window 1. `timer_support:1` makes the device eligible for timer requests. `timer_cancel_support:1` additionally enables targeted cancellation and spoken tools. Ready is sent only after echo is available or Realtime acknowledges configuration.

```json
{"type":"ready","sample_rate":24000,"mode":"realtime"}
```

## Input turns

The device sends `{"type":"begin","turn":1}`, binary PCM chunks while held, and `{"type":"end","turn":1}` on release. Turn numbers increase within the connection's device session. PCM is signed 16-bit mono, little-endian, 24 kHz. Input is capped at 15 seconds. Too-short input returns a notice and Ready without generating a reply.

Status messages contain the current turn and one of Listening, Thinking, or Speaking. The backend sends Realtime `input_audio_buffer.append` events during the hold, with VAD disabled. On release it sends `input_audio_buffer.commit`; after `input_audio_buffer.committed`, it sends `response.create`.

## Reply audio and credits

The backend decodes `response.output_audio.delta` and applies the configured master gain. Echo mode additionally applies one level-correction factor to the entire recorded turn. It coalesces delta boundaries into full 1024-byte packets, retaining at most 1023 remainder bytes until more audio or end-of-audio. Only the final packet may be shorter.

Up to the advertised window may be in flight. The ESP32 callback enqueues data; a separate playback task writes it into I²S. The main loop sends batched consumed-packet credits:

```json
{"type":"ack","turn":1,"count":4}
```

A missing `count` means one for legacy devices. Acknowledgements must match the turn and cannot exceed outstanding credits. A credit indicates that a packet was consumed into I²S, not that its final sample was already audible.

After generation completes and all reply bytes have been dispatched, the backend sends `finish`, even with credits outstanding. This starts short replies that cannot fill the prebuffer. The device drains its playback queue and DMA, sends final credits, then reports completion:

```json
{"type":"playback_done","turn":1,"buffer_gaps":0,"max_gap_ms":0}
```

The backend rejects completion if credits remain outstanding. Only accepted completion returns the device to Ready. Playback diagnostics measure queue waits; they are not a direct measurement of the acoustic output. Provider pauses and network gaps can still occur.

## Typed timer control

The CLI opens `/control` and sends one authenticated request:

```json
{"type":"start_timer","token":"GENERATED_DEVICE_TOKEN","request_id":"UNIQUE_REQUEST_ID","seconds":1500}
```

The backend validates a 1–64 character request ID (letters, digits, underscore, hyphen), whole seconds from 1 to 86400, loopback access, and the token. It forwards the request without the token to the timer-capable device. A `timer_status` request uses the same correlation field and omits seconds.

The ESP32 starts the local clock **before** replying:

```json
{
  "type":"timer_result",
  "request_id":"UNIQUE_REQUEST_ID",
  "accepted":true,
  "duplicate":false,
  "code":"accepted",
  "timer_id":"UNIQUE_REQUEST_ID",
  "seconds":1500,
  "remaining_seconds":1500,
  "running":true,
  "completed":false
}
```

Only a matching device receipt produces `ok:true` and `code:device_confirmed` at the CLI. A busy/invalid command produces a rejection. Status is also taken from a device receipt, not a Mac-estimated countdown.

| Failure | Meaning |
| --- | --- |
| `timer_device_offline`, `outcome:not_sent` | No connected timer-capable device; command not dispatched |
| `command_pending` | Matching request is already waiting, or pending-command capacity is full |
| `device_ack_timeout`, `outcome:unknown` | No receipt within five seconds; timer may have started |
| `device_disconnected`, `outcome:unknown` | Connection lost after dispatch; timer may have started |
| `device_send_failed`, `outcome:unknown` | Transport failure after dispatch attempt |
| Device `timer_already_running` | A new timer was not accepted; existing countdown continues |

The backend does not automatically retry an uncertain start. Check status/OLED first. The most recently accepted ID is idempotent within the ESP32 boot: same ID/duration does not restart even a completed timer; a different duration for that ID is invalid. This is not a persistent request history.

The device can emit `timer_finished` while connected. Offline completion does not depend on delivery of that notification. State remains queryable after reconnecting until a reset or a newly accepted timer replaces it.

## Recovery and API scope

Heartbeat, acknowledgement, setup, input, and reply deadlines bound the session. Malformed messages and failed/stalled audio abort the turn. If generated audio exceeds the 30-second limit, the backend cancels further generation, sends a `reply_audio_limit` notice, and finishes the accepted audio using the usual credits/drain path instead of disconnecting. The answer can be cut short at this limit. Device reconnects create a new Realtime conversation. Opening a serial monitor can reset the ESP32, which also clears its RAM timer.

Spoken tools use a silent planning response, validated arguments, the same device receipt path, and a final audible response with tools disabled. See [Spoken timers](Spoken-Timers.md). Typed timer requests do not create an OpenAI response.

## Cancellation messages

A targeted request is `{"type":"cancel_timer","request_id":"CANCEL_REQUEST_ID","timer_id":"ORIGINAL_TIMER_ID"}` (plus the token for `/control`). The device replies with `timer_cancel_result`, matching request ID, `accepted`, `duplicate`, `code`, and the usual timer snapshot including `cancelled`. Accepted cancellation must match the target ID and report running=false, cancelled=true. Rejected codes include `timer_changed`, `timer_not_running`, and `timer_elapsed`. Older firmware without the capability receives no cancellation command.
