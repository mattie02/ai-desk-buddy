#pragma once
#include <Arduino.h>

constexpr size_t kCaptureBlockFrames = 128;
struct CaptureBlock {
  uint32_t turn;
  size_t frames;
  int16_t samples[kCaptureBlockFrames];
};
bool captureStreamInit();
void captureBegin(uint32_t turn);
void captureEnd();
bool captureRead(CaptureBlock &block, uint32_t waitMs = 0);
unsigned capturePending();
unsigned captureHighWater();
const char *captureFault();
