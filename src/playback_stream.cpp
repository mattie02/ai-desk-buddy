#include "playback_stream.h"
#ifdef DESK_BUDDY_VOICE
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <cmath>

namespace {
constexpr size_t kPacketBytes = 1024;
constexpr size_t kPrebufferBytes = 24 * kPacketBytes; // ~512 ms at 24 kHz PCM16.
struct Packet { size_t bytes; uint8_t pcm[kPacketBytes]; };
QueueHandle_t packets = nullptr;
SemaphoreHandle_t gate = nullptr;
PlaybackSink sink = nullptr;
uint32_t generation = 0;
bool active = false;
bool ended = false;
bool started = false;
size_t queuedBytes = 0;
uint32_t firstPacketAt = 0;
PlaybackProgress progress;
bool alarmPending = false;
bool alarmPlaying = false;

void localAlarm() {
  uint8_t pcm[1024];
  bool ok = sink(nullptr, 0, true);
  // Two local chimes; no network, API, or main-loop polling is involved.
  for (uint32_t first = 0; ok && first < 14400; first += 512) {
    const uint32_t frames = 14400 - first < 512 ? 14400 - first : 512;
    for (uint32_t i = 0; i < frames; ++i) {
      const uint32_t n = first + i;
      const bool sounding = n < 4800 || (n >= 7200 && n < 12000);
      const uint32_t position = n < 7200 ? n : n - 7200;
      double envelope = 1;
      if (position < 240) envelope = double(position) / 240;
      if (position < 4800 && position >= 4560) envelope = double(4799 - position) / 240;
      const int16_t value = sounding ? int16_t(2621 * envelope * std::sin(6.283185307179586 * (n < 7200 ? 880 : 660) * n / 24000)) : 0;
      pcm[i * 2] = uint16_t(value) & 255;
      pcm[i * 2 + 1] = uint16_t(value) >> 8;
    }
    ok = sink(pcm, frames * 2, false);
  }
  sink(nullptr, 0, true);
  Serial.println(ok ? "TIMER completion tone played locally" : "ERROR: local timer tone failed");
}

void playbackTask(void *) {
  Packet packet;
  uint32_t gapAt = 0;
  bool gapCounted = false;
  uint32_t seenGeneration = 0;
  for (;;) {
    xSemaphoreTake(gate, portMAX_DELAY);
    const bool playAlarm = alarmPending;
    if (playAlarm) {
      alarmPending = false;
      alarmPlaying = true;
      ++generation;
      active = false;
      queuedBytes = 0;
      xQueueReset(packets);
      progress = {};
      progress.interrupted = true;
    }
    xSemaphoreGive(gate);
    if (playAlarm) {
      localAlarm();
      xSemaphoreTake(gate, portMAX_DELAY);
      alarmPlaying = false;
      xSemaphoreGive(gate);
      continue;
    }
    bool havePacket = false;
    bool finish = false;
    xSemaphoreTake(gate, portMAX_DELAY);
    const uint32_t current = generation;
    if (seenGeneration != current) {
      gapAt = 0;
      gapCounted = false;
      seenGeneration = current;
    }
    if (active) {
      if (!started && (queuedBytes >= kPrebufferBytes || ended ||
                       (queuedBytes && millis() - firstPacketAt >= 1000))) started = true;
      if (started) {
        havePacket = xQueueReceive(packets, &packet, 0) == pdTRUE;
        if (havePacket) {
          queuedBytes -= packet.bytes;
          if (gapAt) {
            const uint32_t duration = millis() - gapAt;
            if (duration > progress.maxGapMs) progress.maxGapMs = duration;
          }
          gapAt = 0;
          gapCounted = false;
        } else if (ended) {
          active = false;
          finish = true;
        } else {
          if (!gapAt) gapAt = millis();
          // DMA has about 43 ms of headroom; shorter queue waits may be inaudible.
          if (!gapCounted && millis() - gapAt > 43) {
            ++progress.bufferGaps;
            gapCounted = true;
            // After a real underrun, refill before resuming instead of playing
            // each late packet as it arrives and producing repeated small gaps.
            started = false;
            firstPacketAt = millis();
          }
        }
      }
    }
    xSemaphoreGive(gate);
    if (!havePacket && !finish) { vTaskDelay(pdMS_TO_TICKS(2)); continue; }

    const bool ok = sink(havePacket ? packet.pcm : nullptr, havePacket ? packet.bytes : 0, finish);
    xSemaphoreTake(gate, portMAX_DELAY);
    const bool cancelled = generation != current;
    if (!cancelled) {
      if (!ok) { progress.failed = true; active = false; }
      else if (havePacket) ++progress.acknowledgements;
      else progress.complete = true;
    }
    xSemaphoreGive(gate);
    // A disconnect cannot leave old DMA samples audible in a later turn.
    if (cancelled || !ok) sink(nullptr, 0, true);
  }
}
}

bool playbackInit(PlaybackSink output) {
  sink = output;
  packets = xQueueCreate(48, sizeof(Packet));
  gate = xSemaphoreCreateMutex();
  if (packets && gate && xTaskCreatePinnedToCore(playbackTask, "speaker-play", 4096, nullptr, 3, nullptr, 1) == pdPASS) {
    Serial.println("Playback worker ready: ~512 ms prebuffer, 32-packet credit window, underrun refill");
    return true;
  }
  if (packets) vQueueDelete(packets);
  if (gate) vSemaphoreDelete(gate);
  packets = nullptr;
  gate = nullptr;
  return false;
}

bool playbackBegin() {
  if (!gate) return false;
  xSemaphoreTake(gate, portMAX_DELAY);
  ++generation;
  xQueueReset(packets);
  active = true;
  ended = started = false;
  queuedBytes = 0;
  firstPacketAt = 0;
  progress = {};
  xSemaphoreGive(gate);
  return true;
}

bool playbackEnqueue(const uint8_t *pcm, size_t bytes) {
  if (!gate || !bytes || bytes > kPacketBytes || bytes % 2) return false;
  Packet packet;
  packet.bytes = bytes;
  memcpy(packet.pcm, pcm, bytes);
  xSemaphoreTake(gate, portMAX_DELAY);
  const bool ok = active && !ended && xQueueSend(packets, &packet, 0) == pdTRUE;
  if (ok) {
    if (!firstPacketAt) firstPacketAt = millis();
    queuedBytes += bytes;
  }
  xSemaphoreGive(gate);
  return ok;
}

void playbackEnd() {
  if (!gate) return;
  xSemaphoreTake(gate, portMAX_DELAY);
  ended = true; // Also starts short replies that never fill the prebuffer.
  xSemaphoreGive(gate);
}

void playbackCancel() {
  if (!gate) return;
  xSemaphoreTake(gate, portMAX_DELAY);
  ++generation;
  active = false;
  ended = false;
  queuedBytes = 0;
  xQueueReset(packets);
  progress = {};
  xSemaphoreGive(gate);
}

PlaybackProgress playbackProgress() {
  if (!gate) return {};
  xSemaphoreTake(gate, portMAX_DELAY);
  const PlaybackProgress result = progress;
  progress.acknowledgements = 0;
  progress.complete = false;
  progress.interrupted = false;
  xSemaphoreGive(gate);
  return result;
}
void playbackAlarm() {
  if (!gate) { Serial.println("ERROR: local alarm worker unavailable"); return; }
  xSemaphoreTake(gate, portMAX_DELAY);
  alarmPending = true;
  xSemaphoreGive(gate);
}
bool playbackAlarmBusy() {
  if (!gate) return false;
  xSemaphoreTake(gate, portMAX_DELAY);
  const bool result = alarmPending || alarmPlaying;
  xSemaphoreGive(gate);
  return result;
}
#else
bool playbackInit(PlaybackSink) { return true; }
bool playbackBegin() { return true; }
bool playbackEnqueue(const uint8_t *, size_t) { return false; }
void playbackEnd() {}
void playbackCancel() {}
PlaybackProgress playbackProgress() { return {}; }
void playbackAlarm() {}
bool playbackAlarmBusy() { return false; }
#endif
