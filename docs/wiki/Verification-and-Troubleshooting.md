# Verification and troubleshooting

[Documentation home](Home.md)

## Verified baseline

These are the recorded results through November 23, 2026. An automated build is not counted as audible/visible hardware verification.

| Area | Result and evidence |
| --- | --- |
| Builds | Both PlatformIO targets build; uploaded firmware hashes verified |
| Boot tone | User heard the short 440 Hz boot tone through the included speaker |
| Mic | Quiet approximately −68 to −74 dBFS RMS; speech approximately −28 to −43 dBFS; strongest supplied peak −16.7 dBFS; slot1 zero |
| Touch | Eight paired press/release events; subsequent accepted voice holds verified |
| OLED | Scan found one device at `0x3C` with zero bus errors; user confirmed visible status and countdown |
| Echo | Four logged turns of 2.04, 3.41, 0.13, and 2.98 seconds completed after the capture fix |
| AI replies | User confirmed live replies and return to Ready |
| Volume | User confirmed whole-turn echo correction plus `OUTPUT_GAIN=0.5` was good |
| Stutter retest | User reported smooth playback after the dedicated worker/window fix; one diagnostic queue wait of 262 ms was recorded |
| Timer | Device-confirmed 10-second start, Wi-Fi held off after about two seconds, local completion/two chimes, completed state after reconnect; user confirmed the second run |
| Automated tests | Last recorded run: all 16 checks passed |

This is bench acceptance, not a claim of zero gaps under all network/provider conditions. Enclosure acoustics, long-duration reliability, brownouts, reset persistence, and a single-cable power design remain separate work.

## Automated checks

From `backend/`:

```sh
npm ci
npm test
```

Tests cover authentication, ordered PCM and gain, silence/full-scale correction boundaries, short turns, delayed/batched playback credits, short-reply prebuffer completion, premature completion rejection, manual Realtime commit ordering, upstream recovery, timer receipts/rejection/status, invalid commands, and unknown outcome after acknowledgement loss. A native C++ test exercises the actual firmware clock's deadline, duplicate/busy rules, one-shot completion, invalid inputs, and 64-bit clock boundaries.

Tests use localhost and mocked Realtime; they do not spend OpenAI credits. To rebuild from the repository root:

```sh
export PLATFORMIO_CORE_DIR="$PWD/.pio-core"
pio run -e esp32dev -e esp32voice
```

## Problems found and fixed

### Touch caused Listening, then Offline

Serial reproduced `Voice: audio upload too slow`. The original main-loop 32 ms timing guard was too strict for ordinary Wi-Fi delays. A dedicated capture task now drains DMA into a 64-block queue, and input is sent in approximately 43 ms packets. Actual worker stalls or queue overflow still abort the turn instead of silently dropping input.

### Echo was too quiet

Raw microphone-level speech was attenuated again for playback. Raising master gain from 0.06 to 0.24 alone was insufficient. Whole-turn echo level correction and master gain 0.5 passed the user's listening check. The correction does not apply to AI replies, and the local boot/alarm tones have their own firmware amplitudes.

### Intermittent output stutter

The old stop-and-wait path wrote approximately 21 ms to I²S, then waited for a network acknowledgement before more data could arrive. The playback worker now has a bounded queue, approximately 213 ms initial prebuffer, 16-packet window, and batched consumption credits. This decouples audio output from network round trips. The user confirmed smooth playback in the retest.

### Timer started but the Mac reported unknown outcome

The first timer run exposed a corrupted receipt: a borrowed string referenced temporary snapshot storage. Receipt strings now have owned storage and a stable snapshot lifetime. The corrected run returned `device_confirmed` before the Wi-Fi cut and passed the full offline test. The original unknown-outcome result was intentionally not reported as a successful start.

## Troubleshooting table

| Symptom | Check |
| --- | --- |
| Upload says port busy | Exit other serial monitors with Ctrl+C; only one process can own the CP2102 port |
| Upload stays Connecting | Confirm port; hold BOOT, tap EN/RESET, release BOOT when writing begins |
| Setup on OLED | Fill local Wi-Fi, Mac host, and token in `include/secrets.h`; rebuild/upload `esp32voice` |
| Offline at startup | Allow time to join; confirm Mac IP, backend process, port 8765, same 2.4 GHz LAN, and firewall access |
| `device_already_connected` after reset | A stale socket may need heartbeat cleanup; allow reconnection before retrying |
| No sound | Confirm amp 5 V/common ground, GPIO26/25/27, speaker across differential output, and breakout enable state |
| Mic meter stays at floor | Power off and recheck 3V3, GPIO34 SD, shared clocks, L/R → GND, and acoustic opening |
| OLED blank | Confirm 3V3, SDA21/SCL22, `i` scan and address; the address alone does not identify the controller |
| Timer CLI returns unknown | It may have started. Check OLED/status; do not blindly issue a new ID |
| Timer CLI offline | Confirm new firmware advertises timer support and is connected; status requires Wi-Fi, countdown completion does not |
| Timer disappeared | Check for a reset, power loss, or opening the serial port; state is currently RAM-only |
| Voice timer request fails | Check that updated firmware advertises cancellation support; inspect receipt/result codes and see [Spoken timers](Spoken-Timers.md) |

## Logs and observation

Voice serial output shows touch edges, OLED states, worker setup, free heap, capture errors, and playback queue waits. The backend logs session/error codes, received duration, playback completion, and timer acceptance/completion. It does not log API keys, raw audio, or transcripts.

Useful serial controls are listed in [Setup and operation](Setup-and-Operation.md). For an offline timer acceptance test, follow [Focus timer](Focus-Timer.md). Avoid opening a second serial monitor during a running timer: it can reset the board and invalidate the test.

## Spoken timer implementation checks

November 23, 2026: both firmware targets build and 26 automated tests pass after adding cancellation and spoken tools. New checks cover receipt-before-speech ordering, actual device time-left routing, targeted cancel, delayed/lost acknowledgements, invalid arguments, duplicate response completion, and expiry winning at the cancellation boundary. These are automated/mocked results; the user subsequently confirmed all three spoken timer actions and smooth audio.

Physical cancellation retest: updated firmware uploaded with flash hash verification after one interrupted upload/retry. The backend received acceptance for a five-second timer, cancellation, and duplicate cancellation. Status after the original deadline remained cancelled=true, completed=false, running=false. Serial showed cancellation and no completion event for that timer. Realtime accepted the updated session configuration. The user subsequently confirmed audible spoken-tool acceptance and smooth playback.

Offline regression with the new firmware: an accepted 10-second timer continued with ESP32 Wi-Fi held off at five seconds remaining. Serial showed countdown to expiry and `TIMER completion tone played locally`. After enabling reconnection, a device-confirmed status receipt for the same timer reported completed=true, running=false, remaining_seconds=0. This is serial/device evidence; it does not add a new user-confirmed listening result.

Spoken duration formatting update: 27 automated checks pass, including 104 seconds → 1 minute and 44 seconds, hours with a zero minutes component, exact minutes/hours, singular/plural, and zero remaining time. The voice tool result carries the formatted phrase while preserving the original device seconds. The revised phrasing has not yet had a separate listening confirmation.

## Repeated-turn reliability regression

After the initial spoken-timer acceptance, the user reported renewed choppiness and failed holds. Serial captured repeated `network capture buffer overflow` failures. Backend/serial playback diagnostics recorded gaps up to 288 ms, including one reply with ten queue gaps. A separate overlong model reply caused `reply_too_long` and a reconnect. These later observations qualify the earlier smooth-playback bench result.

The main loop previously consumed only one mic block per pass, preventing backlog from shrinking after OLED/network delays. It now drains bounded batches and limits the first capture wait to 2 ms. Reply audio is coalesced into full packets, the playback window is 32 with a 48-packet queue, startup buffering is approximately 512 ms, and an underrun triggers refill before resuming. Overlong replies now stop at the existing 30-second cap and finish playback without a device reconnect. End-of-capture serial summaries report queue high-water, maximum audio-send time, RSSI, and free heap.

Both firmware targets build and all 29 automated tests pass, including tiny audio deltas, the larger bounded window, and overlong reply completion without a reconnect. Hardware short repeated-turn acceptance passed: the user confirmed all three holds worked and audio was smooth.

The physical retest received 2.64, 3.78, and 3.50 seconds of audio. All three replies completed and returned to Ready, each reporting zero playback buffer gaps and zero maximum queue wait. The third turn also received device-confirmed acceptance of a 180-second spoken timer. Capture queue high-water marks were 44/64, 55/64, and 23/64 blocks; maximum audio sends were 173, 178, and 123 ms, with RSSI between −57 and −56 dBm. No capture overflow appeared in those three turns. The queue reached 55/64 blocks, so longer-hold/network-stress testing remains useful; this short bench pass does not establish unlimited jitter tolerance.

## Usage reporting checks

The backend usage feature passes all 35 automated checks, including mixed cached audio/text pricing, unknown/missing usage, duplicate response receipts, cancelled/failed response charges, reconnect totals, bounded history, and authenticated loopback reporting. The live `npm run usage` command returned a zero-response report after restarting the backend. Real voice-turn accounting is pending; historical turns before activation are not backfilled. Firmware was unchanged for this feature.

## Completion display recovery

During the first usage-reporting check, the user saw `Time up`. A device-confirmed status query showed the earlier 180-second timer had completed; the backend was connected and no new API response had been requested. The completed-timer notice previously overrode Ready/Offline indefinitely. It now expires after eight seconds, restoring the current voice/connection status while retaining `FOCUS COMPLETE` in the header and the completed timer state. Both firmware targets built, the voice firmware uploaded with flash hash verification, and a device-accepted two-second timer completed. Serial rendering diagnostics showed `Time up`, local completion tone, and return to Ready after 8.0 seconds. The completed timer remains available through status. The first bench attempt was not dispatched because serial attachment had reset the board and reconnection was still pending; the successful attempt waited for confirmed Realtime Ready.
