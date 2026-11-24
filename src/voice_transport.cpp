#include "voice_transport.h"

#ifdef DESK_BUDDY_VOICE
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include "playback_stream.h"
#include "focus_timer.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

namespace {
enum class Phase { Setup, Offline, Connecting, Ready, Listening, Thinking, Speaking, Error };
Phase phase = Phase::Setup;
WebSocketsClient socket;
BuddyChanged changed;
bool replyStarted = false;
bool configured = false;
bool recording = false;
bool connected = false;
bool waitingForRelease = true;
uint32_t turn = 0;
uint32_t recordingFrames = 0;
uint8_t pendingPcm[2048]; // ~43 ms packets reduce per-message Wi-Fi overhead.
size_t pendingBytes = 0;
uint32_t lastWifiAttempt = 0;
uint32_t maxSendMs = 0;
bool echoMode = false;
bool wifiEnabled = true;

void timerFields(JsonDocument &message, const FocusSnapshot &timer) {
  // Own the string: a const char array can otherwise be borrowed by ArduinoJson.
  message["timer_id"] = String(timer.requestId);
  message["seconds"] = timer.seconds;
  message["remaining_seconds"] = timer.remaining;
  message["running"] = timer.running;
  message["completed"] = timer.completed;
  message["cancelled"] = timer.cancelled;
}

void timerControl(JsonDocument &request) {
  const char *id = request["request_id"] | "";
  JsonDocument reply;
  reply["request_id"] = String(id);
  const char *type = request["type"] | "";
  if (!strcmp(type, "start_timer")) {
    const auto result = request["seconds"].is<uint32_t>() ?
        focusTimerStart(id, request["seconds"].as<uint32_t>()) : FocusClock::Result::Invalid;
    reply["type"] = "timer_result";
    reply["accepted"] = result == FocusClock::Result::Accepted || result == FocusClock::Result::Duplicate;
    reply["duplicate"] = result == FocusClock::Result::Duplicate;
    reply["code"] = result == FocusClock::Result::Busy ? "timer_already_running" :
                     result == FocusClock::Result::Invalid ? "invalid_timer_command" : "accepted";
    if (changed) changed();
  } else if (!strcmp(type, "cancel_timer")) {
    const char *timerId = request["timer_id"] | "";
    const auto result = focusTimerCancel(timerId);
    using C = FocusClock::CancelResult;
    reply["type"] = "timer_cancel_result";
    reply["accepted"] = result == C::Cancelled || result == C::Duplicate;
    reply["duplicate"] = result == C::Duplicate;
    reply["code"] = result == C::NotFound ? "timer_changed" :
                     result == C::NotRunning ? "timer_not_running" :
                     result == C::Elapsed ? "timer_elapsed" : "cancelled";
    if (changed) changed();
  } else reply["type"] = "timer_status";
  const FocusSnapshot timer = focusTimerSnapshot();
  timerFields(reply, timer);
  String text;
  serializeJson(reply, text);
  socket.sendTXT(text);
}

void setPhase(Phase next) {
  if (phase == next) return;
  phase = next;
  if (changed) changed();
}

bool sendControl(const char *type, unsigned count = 0, unsigned gaps = 0, uint32_t maxGapMs = 0) {
  JsonDocument message;
  message["type"] = type;
  message["turn"] = turn;
  if (!strcmp(type, "ack")) message["count"] = count;
  if (!strcmp(type, "playback_done")) {
    message["buffer_gaps"] = gaps;
    message["max_gap_ms"] = maxGapMs;
  }
  String text;
  serializeJson(message, text);
  return connected && socket.sendTXT(text);
}

void abortConnection(const char *reason) {
  Serial.printf("Voice: %s; release pad and wait for Ready before retrying.\n", reason);
  recording = false;
  captureEnd();
  playbackCancel();
  replyStarted = false;
  pendingBytes = 0;
  waitingForRelease = true;
  connected = false;
  setPhase(Phase::Error);
  socket.disconnect();
}

bool flushPcm() {
  if (!pendingBytes) return true;
  const uint32_t before = millis();
  const bool sent = connected && socket.sendBIN(pendingPcm, pendingBytes);
  const uint32_t duration = millis() - before;
  if (duration > maxSendMs) maxSendMs = duration;
  if (!sent) {
    abortConnection("audio send failed");
    return false;
  }
  pendingBytes = 0;
  if (const char *error = captureFault()) {
    abortConnection(error);
    return false;
  }
  return true;
}

void endTurn() {
  captureEnd();
  if (const char *error = captureFault()) { abortConnection(error); return; }
  if (!flushPcm()) return;
  Serial.printf("Capture turn %lu: queue high-water=%u/64 blocks, max audio send=%lu ms, RSSI=%d dBm, heap=%u\n",
                static_cast<unsigned long>(turn), captureHighWater(), static_cast<unsigned long>(maxSendMs), WiFi.RSSI(), ESP.getFreeHeap());
  recording = false;
  waitingForRelease = true;
  setPhase(Phase::Thinking);
  if (!sendControl("end")) abortConnection("could not end turn");
}

void onSocket(WStype_t type, uint8_t *payload, size_t length) {
  if (type == WStype_CONNECTED) {
    connected = true;
    playbackCancel();
    replyStarted = false;
    recording = false;
    waitingForRelease = true;
    setPhase(Phase::Connecting);
    JsonDocument hello;
    hello["type"] = "hello";
    hello["token"] = BUDDY_DEVICE_TOKEN;
    hello["sample_rate"] = 24000;
    hello["playback_window"] = kPlaybackWindow;
    hello["timer_support"] = 1;
    hello["timer_cancel_support"] = 1;
    String text;
    serializeJson(hello, text);
    socket.sendTXT(text);
  } else if (type == WStype_DISCONNECTED) {
    captureEnd();
    playbackCancel();
    replyStarted = false;
    pendingBytes = 0;
    connected = recording = false;
    waitingForRelease = true;
    setPhase(Phase::Offline);
  } else if (type == WStype_BIN) {
    if (phase != Phase::Speaking || length == 0 || length > 1024 || length % 2) {
      abortConnection("unexpected reply audio");
      return;
    }
    if (!replyStarted || !playbackEnqueue(payload, length)) abortConnection("playback buffer rejected audio");
  } else if (type == WStype_TEXT) {
    if (length > 1024) { abortConnection("oversized control"); return; }
    JsonDocument message;
    if (deserializeJson(message, payload, length)) { abortConnection("invalid backend JSON"); return; }
    const char *kind = message["type"] | "";
    if (!strcmp(kind, "start_timer") || !strcmp(kind, "timer_status") || !strcmp(kind, "cancel_timer")) {
      timerControl(message);
      return;
    }
    if (!strcmp(kind, "ready")) {
      if ((message["sample_rate"] | 0) != 24000) { abortConnection("audio rate mismatch"); return; }
      echoMode = !strcmp(message["mode"] | "", "echo");
      setPhase(Phase::Ready);
      Serial.printf("Voice Ready (%s). Hold to speak, release for reply.\n", echoMode ? "local echo" : "Realtime");
    } else if (!strcmp(kind, "status")) {
      if ((message["turn"] | uint32_t(0)) != turn) { abortConnection("turn mismatch"); return; }
      const char *state = message["state"] | "";
      // A fast release can arrive before the server's Listening acknowledgement.
      if (!strcmp(state, "Listening")) {
        if (recording) setPhase(Phase::Listening);
      } else if (!strcmp(state, "Thinking")) setPhase(Phase::Thinking);
      else if (!strcmp(state, "Speaking")) {
        if (!replyStarted) {
          if (!playbackBegin()) { abortConnection("playback worker unavailable"); return; }
          replyStarted = true;
        }
        setPhase(Phase::Speaking);
      }
      else abortConnection("unknown status");
    } else if (!strcmp(kind, "finish")) {
      if ((message["turn"] | uint32_t(0)) != turn ||
          (phase != Phase::Speaking && phase != Phase::Thinking)) { abortConnection("unexpected finish"); return; }
      if (!replyStarted) {
        if (!playbackBegin()) { abortConnection("playback worker unavailable"); return; }
        replyStarted = true;
      }
      playbackEnd();
    } else if (!strcmp(kind, "notice")) {
      Serial.printf("Voice notice: %s\n", message["code"] | "unknown");
    } else if (!strcmp(kind, "error")) {
      Serial.printf("Backend error: %s\n", message["code"] | "unknown");
      abortConnection("backend rejected turn");
    } else abortConnection("unknown backend message");
  } else if (type == WStype_FRAGMENT_TEXT_START || type == WStype_FRAGMENT_BIN_START ||
             type == WStype_FRAGMENT || type == WStype_FRAGMENT_FIN) {
    abortConnection("fragmented backend frame");
  }
}
}

void voiceInit(BuddyPlayback playback, BuddyChanged onChanged) {
  changed = onChanged;
  configured = strlen(BUDDY_WIFI_SSID) && strlen(BUDDY_SERVER_HOST) && strlen(BUDDY_DEVICE_TOKEN) >= 24;
  if (!configured) {
    Serial.println("Voice setup required: edit include/secrets.h with Wi-Fi, Mac LAN IP, and device token.");
    return;
  }
  if (!playbackInit(playback)) {
    configured = false;
    setPhase(Phase::Error);
    Serial.println("Voice: could not allocate playback worker");
    return;
  }
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(BUDDY_WIFI_SSID, BUDDY_WIFI_PASSWORD);
  lastWifiAttempt = millis();
  socket.onEvent(onSocket);
  socket.begin(BUDDY_SERVER_HOST, BUDDY_SERVER_PORT, "/buddy");
  socket.setReconnectInterval(3000);
  socket.enableHeartbeat(10000, 3000, 2);
  setPhase(Phase::Offline);
}

void voiceLoop() {
  if (!configured) return;
  FocusSnapshot completed;
  if (focusTimerCompletion(completed)) {
    if (changed) changed();
    if (connected) {
      JsonDocument event;
      event["type"] = "timer_finished";
      timerFields(event, completed);
      String text;
      serializeJson(event, text);
      socket.sendTXT(text);
    }
  }
  if (!wifiEnabled) { setPhase(Phase::Offline); return; }
  if (recording && captureFault()) {
    abortConnection(captureFault());
    return;
  }
  if (!recording && digitalRead(33) == LOW) waitingForRelease = false;
  if (WiFi.status() != WL_CONNECTED) {
    if (connected) {
      socket.disconnect();
      connected = recording = false;
      waitingForRelease = true;
    }
    setPhase(Phase::Offline);
    if (millis() - lastWifiAttempt >= 10000) {
      lastWifiAttempt = millis();
      WiFi.reconnect();
    }
    return;
  }
  socket.loop();
  const PlaybackProgress progress = playbackProgress();
  if (progress.interrupted && (replyStarted || recording || phase == Phase::Thinking)) {
    abortConnection("local timer alarm interrupted voice turn"); return;
  }
  if (progress.failed) { abortConnection("playback worker failed"); return; }
  if (progress.acknowledgements && !sendControl("ack", progress.acknowledgements)) {
    abortConnection("could not acknowledge playback"); return;
  }
  if (progress.complete) {
    Serial.printf("Playback complete: buffer gaps=%u, max queue wait=%lu ms\n",
                  progress.bufferGaps, static_cast<unsigned long>(progress.maxGapMs));
    replyStarted = false;
    if (!sendControl("playback_done", 0, progress.bufferGaps, progress.maxGapMs)) {
      abortConnection("could not finish playback"); return;
    }
  }
  if (recording && captureFault()) abortConnection(captureFault());
}

void voiceTouch(bool pressed) {
  if (!pressed) {
    waitingForRelease = false;
    if (recording) {
      captureEnd();
      // The capture worker has stopped tagging this turn. Flush its tail before commit.
      CaptureBlock block;
      while (recording && captureRead(block)) voiceCapture(block);
      if (recording) endTurn();
    }
    return;
  }
  if (phase != Phase::Ready || waitingForRelease || playbackAlarmBusy()) {
    Serial.println("Voice busy/offline: release pad and wait for Ready.");
    return;
  }
  ++turn;
  recordingFrames = 0;
  pendingBytes = 0;
  captureBegin(turn);
  maxSendMs = 0;
  recording = true;
  setPhase(Phase::Listening);
  if (!sendControl("begin")) abortConnection("could not begin turn");
}

void voiceCapture(const CaptureBlock &block) {
  if (!recording || block.turn != turn) return;
  if (const char *error = captureFault()) { abortConnection(error); return; }
  size_t frames = block.frames;
  if (frames > kCaptureBlockFrames) { abortConnection("capture block too large"); return; }
  const uint32_t remaining = 24000 * 15 - recordingFrames;
  if (frames > remaining) frames = remaining;
  for (size_t i = 0; i < frames; ++i) {
    const uint16_t sample = static_cast<uint16_t>(block.samples[i]);
    pendingPcm[pendingBytes++] = sample & 0xff;
    pendingPcm[pendingBytes++] = sample >> 8;
    if (pendingBytes == sizeof(pendingPcm) && !flushPcm()) return;
  }
  recordingFrames += frames;
  if (recordingFrames >= 24000 * 15) {
    Serial.println("Voice: 15 second hold limit reached; ending turn.");
    endTurn();
  }
}

const char *voiceStatus() {
  switch (phase) {
    case Phase::Setup: return "Setup";
    case Phase::Offline: return "Offline";
    case Phase::Connecting: return "Connecting";
    case Phase::Ready: return "Ready";
    case Phase::Listening: return "Listening";
    case Phase::Thinking: return "Thinking";
    case Phase::Speaking: return "Speaking";
    case Phase::Error: return "Net error";
  }
  return "Offline";
}
const char *voiceHint() {
  if (phase == Phase::Setup) return "Configure Wi-Fi";
  if (phase == Phase::Offline) return "Check Wi-Fi / Mac";
  if (phase == Phase::Connecting) return "Backend connecting";
  if (phase == Phase::Error) return "Release and retry";
  return echoMode ? "Local echo test" : "Hold pad / max 15s";
}
bool voiceIdle() { return phase == Phase::Ready || phase == Phase::Setup || phase == Phase::Offline; }
void voiceWifiEnabled(bool enabled) {
  wifiEnabled = enabled;
  if (!enabled) {
    socket.disconnect();
    connected = recording = false;
    captureEnd();
    playbackCancel();
    waitingForRelease = true;
    WiFi.disconnect(false, false);
    setPhase(Phase::Offline);
    Serial.println("WIFI held OFF for offline timer test; timer and local alarm continue");
  } else {
    WiFi.reconnect();
    lastWifiAttempt = millis();
    Serial.println("WIFI reconnect enabled");
  }
}

#else
void voiceInit(BuddyPlayback, BuddyChanged) {}
void voiceLoop() {}
void voiceTouch(bool) {}
void voiceCapture(const CaptureBlock &) {}
const char *voiceStatus() { return "Ready"; }
const char *voiceHint() { return "Local bench test"; }
bool voiceIdle() { return true; }
void voiceWifiEnabled(bool) {}
#endif
