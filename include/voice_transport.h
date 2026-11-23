#pragma once
#include <Arduino.h>
#include "capture_stream.h"

using BuddyPlayback = bool (*)(const uint8_t *, size_t, bool);
using BuddyChanged = void (*)();
void voiceInit(BuddyPlayback playback, BuddyChanged changed);
void voiceLoop();
void voiceTouch(bool pressed);
void voiceCapture(const CaptureBlock &block);
const char *voiceStatus();
const char *voiceHint();
bool voiceIdle();
void voiceWifiEnabled(bool enabled);
