#pragma once
// Copy to secrets.h (gitignored), or run `npm run setup` in backend.
// Original ESP32 needs a 2.4 GHz Wi-Fi network. Do not put an OpenAI API key here.
// Wi-Fi fields are optional legacy fallbacks. Saved phone setup settings take priority.
// With neither saved settings nor a fallback, the OLED opens Wi-Fi setup automatically.
#define BUDDY_WIFI_SSID ""
#define BUDDY_WIFI_PASSWORD ""
#define BUDDY_SERVER_HOST ""  // Mac LAN IPv4 address, not localhost
#define BUDDY_SERVER_PORT 8765
#define BUDDY_DEVICE_TOKEN "" // Must match backend DEVICE_TOKEN
