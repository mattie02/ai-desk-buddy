#include "focus_timer.h"
#include "playback_stream.h"
#include <esp_timer.h>
#include <freertos/semphr.h>
namespace {
FocusClock clockState;
SemaphoreHandle_t gate = nullptr;
FocusSnapshot completion;
bool completionPending = false;
uint64_t nowMs() { return uint64_t(esp_timer_get_time()) / 1000; }
FocusSnapshot snapshot() {
  FocusSnapshot result;
  strcpy(result.requestId, clockState.id());
  result.seconds = clockState.seconds();
  result.remaining = clockState.remaining(nowMs());
  result.running = clockState.running();
  result.completed = clockState.completed();
  return result;
}
void timerTask(void *) {
  for (;;) {
    xSemaphoreTake(gate, portMAX_DELAY);
    const bool finished = clockState.poll(nowMs());
    if (finished) {
      completion = snapshot();
      completionPending = true;
    }
    xSemaphoreGive(gate);
    if (finished) {
      Serial.printf("TIMER finished locally: %s\n", completion.requestId);
      playbackAlarm();
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
}
bool focusTimerInit() {
  gate = xSemaphoreCreateMutex();
  if (gate && xTaskCreatePinnedToCore(timerTask, "focus-clock", 3072, nullptr, 2, nullptr, 1) == pdPASS) return true;
  if (gate) vSemaphoreDelete(gate);
  gate = nullptr;
  return false;
}
FocusClock::Result focusTimerStart(const char *id, uint32_t seconds) {
  if (!gate) return FocusClock::Result::Invalid;
  xSemaphoreTake(gate, portMAX_DELAY);
  const auto result = clockState.start(id, seconds, nowMs());
  xSemaphoreGive(gate);
  if (result == FocusClock::Result::Accepted) Serial.printf("TIMER accepted locally: %s, %lu seconds\n", id, static_cast<unsigned long>(seconds));
  return result;
}
FocusSnapshot focusTimerSnapshot() {
  if (!gate) return {};
  xSemaphoreTake(gate, portMAX_DELAY);
  const auto result = snapshot();
  xSemaphoreGive(gate);
  return result;
}
bool focusTimerCompletion(FocusSnapshot &result) {
  if (!gate) return false;
  xSemaphoreTake(gate, portMAX_DELAY);
  const bool available = completionPending;
  if (available) { result = completion; completionPending = false; }
  xSemaphoreGive(gate);
  return available;
}
