#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

struct WifiProfile {
  char ssid[33] = {};
  char password[65] = {};
  bool open = false;
};

inline bool validWifiProfile(const WifiProfile &profile) {
  if (!std::memchr(profile.ssid, 0, sizeof(profile.ssid)) ||
      !std::memchr(profile.password, 0, sizeof(profile.password))) return false;
  const size_t ssidLength = std::strlen(profile.ssid);
  const size_t passwordLength = std::strlen(profile.password);
  if (!ssidLength || ssidLength > 32) return false;
  for (size_t i = 0; i < ssidLength; ++i) {
    const unsigned char c = profile.ssid[i];
    if (c < 32 || c == 127) return false;
  }
  if (profile.open) return passwordLength == 0;
  if (passwordLength < 8 || passwordLength > 64) return false;
  for (size_t i = 0; i < passwordLength; ++i) {
    const unsigned char c = profile.password[i];
    if (passwordLength == 64) {
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    } else if (c < 32 || c > 126) return false;
  }
  return true;
}

// One NVS blob: committing the new profile never requires two separate key writes.
struct WifiRecord {
  uint8_t version = 1;
  uint8_t open = 0;
  char ssid[33] = {};
  char password[65] = {};
  uint32_t checksum = 0;
};
static_assert(offsetof(WifiRecord, checksum) == 100, "Unexpected Wi-Fi record layout");
inline uint32_t wifiRecordChecksum(const WifiRecord &record) {
  const auto *bytes = reinterpret_cast<const uint8_t *>(&record);
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < offsetof(WifiRecord, checksum); ++i) hash = (hash ^ bytes[i]) * 16777619u;
  return hash;
}
inline WifiRecord encodeWifiProfile(const WifiProfile &profile) {
  WifiRecord record;
  if (!validWifiProfile(profile)) { record.version = 0; return record; }
  record.open = profile.open;
  std::strcpy(record.ssid, profile.ssid);
  std::strcpy(record.password, profile.password);
  record.checksum = wifiRecordChecksum(record);
  return record;
}
inline bool decodeWifiProfile(const WifiRecord &record, WifiProfile &profile) {
  if (record.version != 1 || record.open > 1 || record.checksum != wifiRecordChecksum(record)) return false;
  WifiProfile candidate;
  std::memcpy(candidate.ssid, record.ssid, sizeof(candidate.ssid));
  std::memcpy(candidate.password, record.password, sizeof(candidate.password));
  candidate.open = record.open;
  if (!validWifiProfile(candidate)) return false;
  profile = candidate;
  return true;
}

class BootSetupGesture {
 public:
  enum class Result { Normal, Holding, OpenSetup };
  void begin(bool held, uint32_t now) { holding_ = held; started_ = now; }
  Result poll(bool held, uint32_t now) {
    if (!holding_) return Result::Normal;
    if (!held) { holding_ = false; return Result::Normal; }
    if (uint32_t(now - started_) < 3000) return Result::Holding;
    holding_ = false;
    return Result::OpenSetup;
  }
 private:
  bool holding_ = false;
  uint32_t started_ = 0;
};
