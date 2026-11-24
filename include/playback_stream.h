#pragma once
#include <Arduino.h>

using PlaybackSink = bool (*)(const uint8_t *, size_t, bool);
constexpr unsigned kPlaybackWindow = 32;
struct PlaybackProgress {
  unsigned acknowledgements = 0;
  bool complete = false;
  bool failed = false;
  bool interrupted = false;
  unsigned bufferGaps = 0;
  uint32_t maxGapMs = 0;
};
bool playbackInit(PlaybackSink sink);
bool playbackBegin();
bool playbackEnqueue(const uint8_t *pcm, size_t bytes);
void playbackEnd();
void playbackCancel();
PlaybackProgress playbackProgress();
void playbackAlarm();
bool playbackAlarmBusy();
