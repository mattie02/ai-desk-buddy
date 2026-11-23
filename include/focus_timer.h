#pragma once
#include <Arduino.h>
#include "focus_clock.h"
struct FocusSnapshot {
  char requestId[65] = {};
  uint32_t seconds = 0;
  uint32_t remaining = 0;
  bool running = false;
  bool completed = false;
};
bool focusTimerInit();
FocusClock::Result focusTimerStart(const char *id, uint32_t seconds);
FocusSnapshot focusTimerSnapshot();
bool focusTimerCompletion(FocusSnapshot &result);
