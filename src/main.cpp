#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <driver/i2s.h>
#include <esp_err.h>
#include <cmath>
#include "voice_transport.h"
#include "focus_timer.h"

namespace {
constexpr i2s_port_t kPort = I2S_NUM_0;
constexpr int kBclk = 26;
constexpr int kLrclk = 25;
constexpr int kDataOut = 27;
constexpr int kDataIn = 34;
constexpr int kTouchPin = 33;
constexpr uint32_t kTouchDebounceMs = 25;
#ifdef DESK_BUDDY_VOICE
constexpr uint32_t kSampleRate = 24000;
constexpr bool kVoiceBuild = true;
#else
constexpr uint32_t kSampleRate = 16000;
constexpr bool kVoiceBuild = false;
#endif
constexpr uint32_t kToneHz = 440;
constexpr uint32_t kToneMs = 600;
constexpr uint32_t kFadeSamples = kSampleRate * 20 / 1000;
constexpr size_t kBlockFrames = 128;
// 1% of signed 32-bit full scale (~-40 dBFS peak). Start quiet.
constexpr double kPeak = 2147483647.0 * 0.01;
constexpr double kTwoPi = 6.283185307179586;
bool ready = false;
bool audioFault = false;
bool i2cReady = false;
bool meterEnabled = false;
uint32_t discardUntil = 0;
bool touchCandidate = false;
bool touchPressed = false;
bool touchReady = false;
uint32_t touchChangedAt = 0;
uint32_t touchPressedAt = 0;
Adafruit_SSD1306 oled(128, 64, &Wire, -1, kVoiceBuild ? 400000 : 100000, 100000);
bool oledReady = false;
bool oledDirty = true;
bool toneActive = false;
bool thinkingDemo = false;
uint32_t demoStartedAt = 0;
bool completionNotice = false;
uint32_t completionNoticeStartedAt = 0;
constexpr uint32_t kCompletionNoticeMs = 8000;

void renderStatus() {
  if (!oledDirty) return;
  oledDirty = false;
  const char *status = !ready && !toneActive ? "Starting" : "Ready";
  if (touchPressed) status = "Listening";
  if (thinkingDemo) status = "Thinking";
  if (toneActive) status = "Speaking";
  if (kVoiceBuild && ready && !toneActive) status = voiceStatus();
  if (audioFault) status = "Audio err";
  const FocusSnapshot timer = focusTimerSnapshot();
  char countdown[16];
  snprintf(countdown, sizeof(countdown), "%02lu:%02lu", static_cast<unsigned long>(timer.remaining / 60), static_cast<unsigned long>(timer.remaining % 60));
  if (!audioFault && !toneActive && voiceIdle()) {
    if (timer.running) status = countdown;
    else if (timer.completed && completionNotice) status = "Time up";
  }
  Serial.printf("OLED status: %s (%s)\n", status, kVoiceBuild ? "voice build" : "local bench test");
  if (!oledReady) return;
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  if (timer.running) { oled.print("FOCUS "); oled.print(countdown); }
  else if (timer.completed) oled.print("FOCUS COMPLETE");
  else if (timer.cancelled) oled.print("FOCUS CANCELLED");
  else oled.print(kVoiceBuild ? "DESK BUDDY / VOICE" : "DESK BUDDY / LOCAL");
  oled.drawFastHLine(0, 15, 128, SSD1306_WHITE);
  oled.setTextSize(2);
  oled.setCursor(0, 26);
  oled.print(status);
  oled.setTextSize(1);
  oled.setCursor(0, 54);
  oled.print(timer.running && voiceIdle() ? "Runs locally on ESP32" : kVoiceBuild && !toneActive ? voiceHint() : thinkingDemo || toneActive ? "Local test / no AI" :
             touchPressed ? "Mic live / local only" : "Hold touch pad");
  oled.display();
}

void initOled() {
  if (!i2cReady) return;
  Wire.beginTransmission(0x3C);
  if (Wire.endTransmission() != 0) {
    Serial.println("OLED absent at 0x3C; continuing audio/touch diagnostics.");
    return;
  }
  oledReady = oled.begin(SSD1306_SWITCHCAPVCC, 0x3C, false, false);
  if (!oledReady) {
    Serial.println("OLED framebuffer allocation failed; continuing diagnostics.");
    return;
  }
  Serial.println("OLED initialized: SSD1306 128x64 at 0x3C. Confirm visible text.");
  renderStatus();
}

void scanI2c() {
  if (!i2cReady) {
    i2cReady = Wire.begin(21, 22, 100000);
    Wire.setTimeOut(20);
  }
  if (!i2cReady) {
    Serial.println("I2C initialization failed (SDA=21 SCL=22)");
    return;
  }
  Serial.println("I2C scan: SDA=21 SCL=22, 100 kHz, 7-bit addresses");
  unsigned found = 0;
  unsigned errors = 0;
  for (uint8_t address = 0x08; address <= 0x77; ++address) {
    Wire.beginTransmission(address);
    const uint8_t result = Wire.endTransmission();
    if (result == 0) {
      Serial.printf("I2C device at 0x%02X%s\n", address,
                    address == 0x3C || address == 0x3D ? " (OLED candidate; controller not identified)" : "");
      ++found;
    } else if (result != 2) {
      // Address NACK is normal for unused addresses; other results indicate a bus issue.
      ++errors;
    }
  }
  Serial.printf("I2C scan complete: %u device(s), %u bus error(s)\n", found, errors);
  if (found == 0) Serial.println("No devices found. Power off before checking OLED power and SDA/SCL wiring.");
}

void printTouchStatus() {
  Serial.printf("TOUCH GPIO33 raw=%s stable=%s\n",
                digitalRead(kTouchPin) == HIGH ? "HIGH" : "LOW",
                touchPressed ? "PRESSED" : "RELEASED");
}

void initTouch() {
  touchCandidate = touchPressed = digitalRead(kTouchPin) == HIGH;
  touchChangedAt = touchPressedAt = millis();
  touchReady = true;
  printTouchStatus();
  if (touchPressed) {
    Serial.println("TOUCH starts HIGH: release pad; idle should become LOW. Check A/B if it stays latched.");
  }
  Serial.println("Touch test: hold pad, then release. Expect one PRESSED and one RELEASED per hold.");
}

void pollTouch() {
  if (!touchReady) return;
  const uint32_t now = millis();
  const bool raw = digitalRead(kTouchPin) == HIGH;
  if (raw != touchCandidate) {
    touchCandidate = raw;
    touchChangedAt = now;
  }
  if (touchCandidate != touchPressed && now - touchChangedAt >= kTouchDebounceMs) {
    touchPressed = touchCandidate;
    // A fresh press cancels the explicit local Thinking demo.
    if (touchPressed) thinkingDemo = false;
    voiceTouch(touchPressed);
    oledDirty = true;
    if (touchPressed) {
      touchPressedAt = now;
      Serial.println("TOUCH PRESSED (GPIO33 HIGH)");
    } else {
      Serial.printf("TOUCH RELEASED (GPIO33 LOW), held %lu ms\n",
                    static_cast<unsigned long>(now - touchPressedAt));
    }
  }
}

struct Levels {
  double sum = 0;
  double squares = 0;
  double peak = 0;
  uint32_t nonzero = 0;
};
Levels levels[2];
uint32_t meterFrames = 0;

void resetMeter() {
  levels[0] = {};
  levels[1] = {};
  meterFrames = 0;
}

double dbfs(double value) {
  return value > 0.000001 ? 20.0 * std::log10(value) : -120.0;
}

bool check(esp_err_t result, const char *operation) {
  if (result == ESP_OK) return true;
  audioFault = true;
  oledDirty = true;
  Serial.printf("ERROR: %s: %s\n", operation, esp_err_to_name(result));
  return false;
}

bool writeFrames(const int32_t *samples, size_t frames, bool handleTouch = true) {
  const auto *bytes = reinterpret_cast<const uint8_t *>(samples);
  size_t remaining = frames * 2 * sizeof(int32_t);
  while (remaining > 0) {
    if (handleTouch) pollTouch();
    size_t written = 0;
    if (!check(i2s_write(kPort, bytes, remaining, &written,
                         pdMS_TO_TICKS(1000)), "i2s_write")) return false;
    if (written == 0 || written > remaining) {
      audioFault = true;
      oledDirty = true;
      Serial.println("ERROR: I2S write made no valid progress");
      return false;
    }
    bytes += written;
    remaining -= written;
  }
  return true;
}

bool silence(uint32_t milliseconds, bool handleTouch = true) {
  const int32_t zeros[kBlockFrames * 2] = {};
  uint32_t remaining = kSampleRate * milliseconds / 1000;
  while (remaining > 0) {
    const size_t frames = remaining < kBlockFrames ? remaining : kBlockFrames;
    if (!writeFrames(zeros, frames, handleTouch)) return false;
    remaining -= frames;
  }
  return true;
}

bool playTone() {
  toneActive = true;
  oledDirty = true;
  renderStatus();
  Serial.println("Tone: 440 Hz, 600 ms, 1% peak (-40 dBFS)");
  // Clear startup data and allow the amplifier to wake before the tone.
  if (!silence(100)) return false;
  int32_t samples[kBlockFrames * 2];
  constexpr uint32_t total = kSampleRate * kToneMs / 1000;
  for (uint32_t first = 0; first < total; first += kBlockFrames) {
    const size_t frames = total - first < kBlockFrames ? total - first : kBlockFrames;
    for (size_t frame = 0; frame < frames; ++frame) {
      const uint32_t index = first + frame;
      double envelope = 1.0;
      if (index < kFadeSamples) envelope = double(index) / kFadeSamples;
      if (total - 1 - index < kFadeSamples)
        envelope = double(total - 1 - index) / kFadeSamples;
      const int32_t value = static_cast<int32_t>(
          kPeak * envelope * std::sin(kTwoPi * kToneHz * index / kSampleRate));
      // Identical left/right samples support either mono channel selection.
      samples[frame * 2] = value;
      samples[frame * 2 + 1] = value;
    }
    if (!writeFrames(samples, frames)) return false;
  }
  // Queue more silence than the whole DMA ring so the tone actually drains.
  if (!silence(100)) return false;
  delay(100);
  if (!check(i2s_zero_dma_buffer(kPort), "i2s_zero_dma_buffer")) return false;
  Serial.println("Tone data sent. Confirm sound at the speaker.");
  return true;
}

bool playReply(const uint8_t *pcm, size_t bytes, bool finish) {
  if (finish) {
    // The final packet acknowledgement means queued, not yet audible. Drain before Ready.
    if (!silence(60, false)) return false;
    delay(50);
    return check(i2s_zero_dma_buffer(kPort), "reply drain");
  }
  if (!ready || bytes % 2) return false;
  int32_t stereo[kBlockFrames * 2];
  for (size_t offset = 0; offset < bytes;) {
    const size_t frames = (bytes - offset) / 2 < kBlockFrames ? (bytes - offset) / 2 : kBlockFrames;
    for (size_t i = 0; i < frames; ++i) {
      const uint16_t raw = uint16_t(pcm[offset + i * 2]) | (uint16_t(pcm[offset + i * 2 + 1]) << 8);
      // Multiplication avoids undefined signed-left-shift behavior for negative PCM.
      const int32_t sample = int32_t(static_cast<int16_t>(raw)) * 65536;
      stereo[i * 2] = stereo[i * 2 + 1] = sample;
    }
    if (!writeFrames(stereo, frames, false)) return false;
    offset += frames * 2;
  }
  return true;
}

bool initAudio() {
  i2s_config_t config = {};
  // One peripheral owns shared BCLK/WS for both directions, with separate data pins.
  config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX);
  config.sample_rate = kSampleRate;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  // Voice display refresh is ~26 ms at 400 kHz; eight blocks cover ~43 ms at 24 kHz.
  config.dma_buf_count = kVoiceBuild ? 8 : 4;
  config.dma_buf_len = kBlockFrames;
  config.use_apll = false;
  config.tx_desc_auto_clear = true;
  if (!check(i2s_driver_install(kPort, &config, 0, nullptr),
             "i2s_driver_install")) return false;

  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = kBclk;
  pins.ws_io_num = kLrclk;
  pins.data_out_num = kDataOut;
  pins.data_in_num = kDataIn;
  if (!check(i2s_set_pin(kPort, &pins), "i2s_set_pin") ||
      !check(i2s_zero_dma_buffer(kPort), "i2s_zero_dma_buffer")) {
    i2s_driver_uninstall(kPort);
    return false;
  }
  Serial.printf("I2S ready: BCLK=26 LRCLK=25 DOUT=27, %lu Hz, stereo 32-bit Philips I2S\n",
                static_cast<unsigned long>(kSampleRate));
  Serial.println("Mic input: DIN=34, INMP441 L/R strapped to GND (left)");
  return true;
}

bool captureMeter(uint32_t waitMs = 10) {
  int32_t samples[kBlockFrames * 2];
  size_t bytesRead = 0;
  if (kVoiceBuild) {
    CaptureBlock block;
    if (!captureRead(block, waitMs)) return true;
    voiceCapture(block);
    if (!meterEnabled || !voiceIdle()) return true;
    for (size_t i = 0; i < block.frames; ++i) {
      samples[i * 2] = int32_t(block.samples[i]) * 65536;
      samples[i * 2 + 1] = 0;
    }
    bytesRead = block.frames * 2 * sizeof(int32_t);
  } else if (!check(i2s_read(kPort, samples, sizeof(samples), &bytesRead,
                      pdMS_TO_TICKS(1000)), "i2s_read")) return false;
  if (bytesRead == 0 || bytesRead % (2 * sizeof(int32_t)) != 0) {
    audioFault = true;
    oledDirty = true;
    Serial.println("ERROR: mic read timed out or returned an incomplete stereo frame");
    return false;
  }
  // Drain RX continuously, even while logging is disabled. Discard queued boot/tone audio.
  if (kVoiceBuild && !voiceIdle()) return true;
  if (!meterEnabled || static_cast<int32_t>(millis() - discardUntil) < 0) return true;
  const size_t frames = bytesRead / (2 * sizeof(int32_t));
  for (size_t frame = 0; frame < frames; ++frame) {
    for (size_t slot = 0; slot < 2; ++slot) {
      // INMP441 supplies 24 signed bits in the high bits of each 32-bit slot.
      const int32_t pcm24 = samples[frame * 2 + slot] >> 8;
      const double normalized = double(pcm24) / 8388608.0;
      auto &level = levels[slot];
      level.sum += normalized;
      level.squares += normalized * normalized;
      if (std::fabs(normalized) > level.peak) level.peak = std::fabs(normalized);
      if (pcm24 != 0) ++level.nonzero;
    }
  }
  meterFrames += frames;
  if (meterFrames >= kSampleRate / 2) {
    Serial.print("MIC");
    for (size_t slot = 0; slot < 2; ++slot) {
      const auto &level = levels[slot];
      const double mean = level.sum / meterFrames;
      const double variance = level.squares / meterFrames - mean * mean;
      const double acRms = std::sqrt(variance > 0 ? variance : 0);
      Serial.printf(" slot%u RMS=%.1f peak=%.1f dBFS nonzero=%u/%u",
                    unsigned(slot), dbfs(acRms), dbfs(level.peak),
                    unsigned(level.nonzero), unsigned(meterFrames));
    }
    Serial.println();
    resetMeter();
  }
  return true;
}
}  // namespace

void setup() {
  // The module drives this digital input; this is not the ESP32 touchRead API.
  pinMode(kTouchPin, INPUT_PULLDOWN);
  Serial.begin(115200);
  delay(1500);
  Serial.println("\nAI Desk Buddy - OLED bring-up");
  initTouch();
  Serial.println(kVoiceBuild ? "Commands: s = touch status, m = mic meter (idle only), i = I2C scan (idle only)." :
                 "Commands: s = touch status, m = mic meter (starts OFF), t = test tone.");
  if (!kVoiceBuild) Serial.println("Commands: i = repeat I2C scan, d = local Thinking/Speaking demo.");
  scanI2c();
  initOled();
  if (!initAudio()) {
    Serial.println("Audio initialization failed. Check errors above; press EN/RESET to retry.");
    return;
  }
  if (kVoiceBuild && !captureStreamInit()) {
    audioFault = true;
    oledDirty = true;
    i2s_stop(kPort);
    Serial.println("ERROR: could not allocate microphone capture worker");
    return;
  }
  ready = playTone();
  toneActive = false;
  oledDirty = true;
  if (ready) {
    voiceInit(playReply, [] { oledDirty = true; });
    if (kVoiceBuild && !focusTimerInit()) Serial.println("ERROR: could not start local focus timer task");
    if (kVoiceBuild) Serial.println("Timer bench commands: w = hold Wi-Fi OFF, W = reconnect, u = timer status");
    if (kVoiceBuild) Serial.printf("Voice memory: %u bytes free heap\n", ESP.getFreeHeap());
    discardUntil = millis() + 200;
    Serial.println(kVoiceBuild ? "Voice build: waiting for Wi-Fi/backend Ready before accepting a hold." :
                   "Ready. Touch logging ON; send m for microphone levels. No audio is stored or sent.");
  } else {
    i2s_stop(kPort);
    Serial.println("Audio failed. Check errors above; press EN/RESET to retry.");
  }
}

void loop() {
  const FocusSnapshot timer = focusTimerSnapshot();
  static uint32_t lastRemaining = UINT32_MAX;
  static bool lastCompleted = false;
  static bool lastCancelled = false;
  if (timer.completed && !lastCompleted) {
    completionNotice = true;
    completionNoticeStartedAt = millis();
  }
  if (completionNotice && (!timer.completed || millis() - completionNoticeStartedAt >= kCompletionNoticeMs)) {
    completionNotice = false;
    oledDirty = true;
  }
  if (timer.remaining != lastRemaining || timer.completed != lastCompleted || timer.cancelled != lastCancelled) {
    lastRemaining = timer.remaining;
    lastCompleted = timer.completed;
    lastCancelled = timer.cancelled;
    oledDirty = true;
  }
  voiceLoop();
  pollTouch();
  if (Serial.available() > 0) {
    const char command = static_cast<char>(Serial.read());
    if (kVoiceBuild && command == 'w') voiceWifiEnabled(false);
    else if (kVoiceBuild && command == 'W') voiceWifiEnabled(true);
    else if (command == 'u') {
      const auto current = focusTimerSnapshot();
      Serial.printf("TIMER status: running=%u completed=%u remaining=%lu seconds\n", current.running, current.completed, static_cast<unsigned long>(current.remaining));
    } else if (!kVoiceBuild && ready && (command == 't' || command == 'T')) {
      thinkingDemo = false;
      if (!playTone()) {
        ready = false;
        i2s_stop(kPort);
        Serial.println("Audio failed. Press EN/RESET to retry.");
      }
      toneActive = false;
      oledDirty = true;
      resetMeter();
      discardUntil = millis() + 200;
    } else if (command == 'm' || command == 'M') {
      meterEnabled = !meterEnabled;
      resetMeter();
      Serial.printf("Mic meter %s\n", meterEnabled ? "ON" : "OFF");
    } else if (command == 's' || command == 'S') {
      printTouchStatus();
    } else if (voiceIdle() && (command == 'i' || command == 'I')) {
      scanI2c();
    } else if (!kVoiceBuild && ready && (command == 'd' || command == 'D')) {
      thinkingDemo = true;
      demoStartedAt = millis();
      oledDirty = true;
      Serial.println("Local status demo: Thinking for 1 second, then Speaking test tone. No backend call.");
    }
  }
  if (ready && thinkingDemo && millis() - demoStartedAt >= 1000) {
    thinkingDemo = false;
    if (!playTone()) {
      ready = false;
      i2s_stop(kPort);
      Serial.println("Audio failed. Press EN/RESET to retry.");
    }
    toneActive = false;
    oledDirty = true;
    resetMeter();
    discardUntil = millis() + 200;
  }
  bool captureOk = true;
  if (ready) {
    captureOk = captureMeter(kVoiceBuild ? 2 : 10);
    // One block per loop cannot catch up after an OLED/network delay. Drain a
    // bounded batch, then return to touch/network processing rather than starve it.
    for (unsigned i = 0; kVoiceBuild && captureOk && i < 16 && capturePending(); ++i) {
      captureOk = captureMeter(0);
    }
  }
  if (ready && !captureOk) {
    ready = false;
    oledDirty = true;
    i2s_stop(kPort);
    Serial.println("Capture failed. Press EN/RESET to retry.");
  }
  renderStatus();
  if (!ready) delay(10);
}
