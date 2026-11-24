#include "wifi_profile.h"
#include "setup_http.h"
#include <cassert>
#include <string>
#include <limits>

SetupHttpRequest::State parse(SetupHttpRequest &request, const std::string &text) {
  auto state = SetupHttpRequest::State::Reading;
  for (char c : text) state = request.feed(c);
  return state;
}
std::string post(const std::string &body, const std::string &extra = "") {
  return "POST /save HTTP/1.1\r\nHost: 192.168.4.1\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: " +
    std::to_string(body.size()) + "\r\n" + extra + "\r\n" + body;
}
int main() {
  WifiProfile old;
  std::strcpy(old.ssid, "Example network"); std::strcpy(old.password, "test-only-password");
  assert(validWifiProfile(old));
  const auto record = encodeWifiProfile(old);
  WifiProfile restored;
  assert(decodeWifiProfile(record, restored));
  assert(!std::strcmp(restored.ssid, old.ssid) && !std::strcmp(restored.password, old.password));
  auto damaged = record; damaged.password[0] ^= 1;
  assert(!decodeWifiProfile(damaged, restored));
  assert(!std::strcmp(restored.password, old.password)); // Failed load cannot replace a working profile.
  damaged = record; damaged.version = 2; damaged.checksum = wifiRecordChecksum(damaged);
  assert(!decodeWifiProfile(damaged, restored));
  WifiProfile open = old; open.open = true;
  assert(!validWifiProfile(open));
  open.password[0] = 0; assert(validWifiProfile(open));
  assert(decodeWifiProfile(encodeWifiProfile(open), restored) && restored.open);
  auto invalid = old; std::strcpy(invalid.password, "short"); assert(!validWifiProfile(invalid));
  invalid = old; std::memset(invalid.ssid, 'a', sizeof(invalid.ssid)); assert(!validWifiProfile(invalid));
  invalid = old; invalid.ssid[0] = '\n'; assert(!validWifiProfile(invalid));
  invalid = old; std::memset(invalid.password, 'a', 64); invalid.password[64] = 0;
  assert(validWifiProfile(invalid)); invalid.password[63] = 'z'; assert(!validWifiProfile(invalid));

  BootSetupGesture gesture;
  using G = BootSetupGesture::Result;
  gesture.begin(false, 0); assert(gesture.poll(true, 10000) == G::Normal); // Runtime holds do not open setup.
  gesture.begin(true, 1000); assert(gesture.poll(true, 3999) == G::Holding);
  assert(gesture.poll(true, 4000) == G::OpenSetup);
  assert(gesture.poll(true, 9000) == G::Normal);
  gesture.begin(true, 1000); assert(gesture.poll(false, 1100) == G::Normal);
  assert(gesture.poll(true, 5000) == G::Normal);
  const uint32_t nearWrap = std::numeric_limits<uint32_t>::max() - 1000;
  gesture.begin(true, nearWrap); assert(gesture.poll(true, uint32_t(nearWrap + 3000)) == G::OpenSetup);

  using S = SetupHttpRequest::State;
  SetupHttpRequest get;
  assert(parse(get, "GET / HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n") == S::Complete);
  assert(!std::strcmp(get.method(), "GET") && !std::strcmp(get.path(), "/"));
  const std::string body = "ssid=My+%26+WiFi&password=test%2Bpassword&csrf=nonce";
  SetupHttpRequest save;
  assert(parse(save, post(body)) == S::Complete && body == save.body());
  char value[33];
  assert(setupFormValue(save.body(), "ssid", value, sizeof(value)) && !std::strcmp(value, "My & WiFi"));
  assert(setupFormValue(save.body(), "password", value, sizeof(value)) && !std::strcmp(value, "test+password"));
  assert(!setupFormValue("ssid=first&ssid=second", "ssid", value, sizeof(value)));
  assert(!setupFormValue("ssid=%00", "ssid", value, sizeof(value)));
  assert(!setupFormValue("ssid=%0A", "ssid", value, sizeof(value)));
  assert(!setupFormValue("ssid=%XX", "ssid", value, sizeof(value)));
  assert(!setupFormValue("ssid=%A", "ssid", value, sizeof(value)));
  assert(!setupFormValue("ssid=12345", "ssid", value, 5));
  assert(!setupFormValue("password=secret", "ssid", value, sizeof(value)));
  for (const auto &text : {
      "GET / HTTP/1.1\r\nHost: attacker.example\r\n\r\n",
      "GET / HTTP/1.1\r\n\r\n",
      "POST /save HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n" }) {
    SetupHttpRequest bad; assert(parse(bad, text) == S::Invalid);
  }
  SetupHttpRequest duplicate; assert(parse(duplicate, post(body, "Content-Length: 1\r\n")) == S::Invalid);
  SetupHttpRequest chunked; assert(parse(chunked, post(body, "Transfer-Encoding: chunked\r\n")) == S::Invalid);
  SetupHttpRequest large; assert(parse(large, post(std::string(513, 'a'))) == S::TooLarge);
  SetupHttpRequest headers; assert(parse(headers, "GET / HTTP/1.1\r\nX: " + std::string(2200, 'a')) == S::TooLarge);
  SetupHttpRequest partial;
  const auto text = post(body);
  assert(parse(partial, text.substr(0, text.size() - 1)) == S::Reading);
  assert(partial.feed(text.back()) == S::Complete);
  // Exercise arbitrary malformed traffic under address/undefined-behavior sanitizers.
  uint32_t random = 17;
  for (unsigned i = 0; i < 1000; ++i) {
    SetupHttpRequest fuzz;
    for (unsigned j = 0; j < 3000; ++j) {
      random = random * 1664525u + 1013904223u;
      if (fuzz.feed(char(random >> 24)) != S::Reading) break;
    }
  }
}
