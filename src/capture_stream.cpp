#include "capture_stream.h"
#ifdef DESK_BUDDY_VOICE
#include <driver/i2s.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

namespace {
QueueHandle_t blocks = nullptr;
SemaphoreHandle_t gate = nullptr;
uint32_t activeTurn = 0;
enum class Fault { None, Read, Overflow, DmaStall };
Fault fault = Fault::None;

void captureTask(void *) {
  int32_t stereo[kCaptureBlockFrames * 2];
  uint32_t lastRead = millis();
  for (;;) {
    size_t bytes = 0;
    const esp_err_t result = i2s_read(I2S_NUM_0, stereo, sizeof(stereo), &bytes, pdMS_TO_TICKS(1000));
    const uint32_t now = millis();
    CaptureBlock block = {};
    const bool valid = result == ESP_OK && bytes && bytes % (2 * sizeof(int32_t)) == 0;
    if (valid) {
      block.frames = bytes / (2 * sizeof(int32_t));
      for (size_t i = 0; i < block.frames; ++i) block.samples[i] = static_cast<int16_t>(stereo[i * 2] >> 16);
    }
    xSemaphoreTake(gate, portMAX_DELAY);
    block.turn = activeTurn;
    if (!valid) fault = Fault::Read;
    else if (activeTurn && now - lastRead > 35) fault = Fault::DmaStall;
    // Never wait for the network: 64 PCM blocks provide ~341 ms of bounded headroom.
    if (valid && xQueueSend(blocks, &block, 0) != pdTRUE && activeTurn) fault = Fault::Overflow;
    xSemaphoreGive(gate);
    lastRead = now;
    if (!valid) vTaskDelay(pdMS_TO_TICKS(10));
  }
}
}

bool captureStreamInit() {
  blocks = xQueueCreate(64, sizeof(CaptureBlock));
  gate = xSemaphoreCreateMutex();
  if (blocks && gate && xTaskCreatePinnedToCore(captureTask, "mic-capture", 4096, nullptr, 2, nullptr, 1) == pdPASS) {
    Serial.println("Mic capture worker ready: 64 blocks, ~341 ms network buffer");
    return true;
  }
  if (blocks) vQueueDelete(blocks);
  if (gate) vSemaphoreDelete(gate);
  blocks = nullptr;
  gate = nullptr;
  return false;
}

void captureBegin(uint32_t turn) {
  if (!gate) return;
  xSemaphoreTake(gate, portMAX_DELAY);
  xQueueReset(blocks); // Discard idle samples; each turn starts with a fresh queue.
  if (fault != Fault::Read) fault = Fault::None;
  activeTurn = turn;
  xSemaphoreGive(gate);
}

void captureEnd() {
  if (!gate) return;
  xSemaphoreTake(gate, portMAX_DELAY);
  activeTurn = 0; // Serializes with producer enqueue so the final turn can be drained safely.
  xSemaphoreGive(gate);
}

bool captureRead(CaptureBlock &block, uint32_t waitMs) {
  return blocks && xQueueReceive(blocks, &block, pdMS_TO_TICKS(waitMs)) == pdTRUE;
}

const char *captureFault() {
  if (!gate) return "capture worker unavailable";
  xSemaphoreTake(gate, portMAX_DELAY);
  const Fault current = fault;
  xSemaphoreGive(gate);
  switch (current) {
    case Fault::Read: return "I2S capture read failed";
    case Fault::Overflow: return "network capture buffer overflow";
    case Fault::DmaStall: return "I2S capture worker stalled";
    default: return nullptr;
  }
}
#else
bool captureStreamInit() { return true; }
void captureBegin(uint32_t) {}
void captureEnd() {}
bool captureRead(CaptureBlock &, uint32_t) { return false; }
const char *captureFault() { return nullptr; }
#endif
