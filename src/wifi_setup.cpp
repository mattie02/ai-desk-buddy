#include "wifi_setup.h"
#ifdef DESK_BUDDY_VOICE
#include "wifi_profile.h"
#include "setup_http.h"
#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <esp_wifi.h>
#include <esp_system.h>

namespace {
enum class Mode { Normal, Holding, Portal };
enum class Trial { None, Disconnecting, Testing, Saved, Exit };
Mode mode = Mode::Normal;
Trial trial = Trial::None;
WifiProfile current, candidate;
bool haveProfile = false;
BootSetupGesture gesture;
WifiSetupChanged changed = nullptr;
WiFiServer http(IPAddress(192, 168, 4, 1), 80, 1);
WiFiClient client;
SetupHttpRequest request;
uint32_t openedAt = 0, clientAt = 0, trialAt = 0, closeAt = 0;
String apName, apPassword, csrf;
const char *message = "Ready to configure";
constexpr uint32_t kPortalMs = 5 * 60 * 1000;
void notify(const char *text) { message = text; if (changed) changed(); }

const char kPage[] PROGMEM = R"HTML(<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Desk Buddy Wi-Fi</title><style>
body{font:17px system-ui,sans-serif;max-width:30rem;margin:2rem auto;padding:0 1rem;color:#162535;background:#f1f5f8}h1{font-size:1.7rem}label{display:block;margin:1rem 0 .4rem}input:not([type=checkbox]),button{box-sizing:border-box;width:100%;padding:.8rem;font:inherit;border:1px solid #9aa9b6;border-radius:.5rem}button{margin-top:1rem;background:#173d60;color:white;cursor:pointer}button.secondary{background:white;color:#173d60}#status{padding:1rem;background:white;border-radius:.5rem;min-height:2rem}small{display:block;margin-top:1rem}input[type=checkbox]{width:1.2rem;height:1.2rem}</style>
<h1>Desk Buddy Wi-Fi</h1><p>Choose a 2.4 GHz network. The new connection is tested before its settings are saved.</p>
<form id="wifi" method="post" action="/save"><input type="hidden" name="csrf" value="__CSRF__"><label for="ssid">Network name</label><input id="ssid" name="ssid" list="networks" maxlength="32" required autocomplete="off"><datalist id="networks"></datalist>
<label for="password">Wi-Fi password</label><input id="password" name="password" type="password" maxlength="64" autocomplete="off">
<label><input id="open" name="open" type="checkbox" value="1"> This is an open network (no password)</label><button id="save">Test and save</button></form>
<p id="status" role="status" aria-live="polite">Loading setup status…</p><button class="secondary" id="exit">Exit setup</button>
<small>Setup closes after five minutes. Wrong credentials leave the previous settings unchanged. To try again later, hold the touch pad while pressing EN/RESET and keep holding until setup details appear. Resetting stops any running focus timer.</small>
<script>
const csrf='__CSRF__'; let stopped=false;
const status=document.getElementById('status'),save=document.getElementById('save'),password=document.getElementById('password'),open=document.getElementById('open');
open.onchange=()=>{password.disabled=open.checked;if(open.checked)password.value='';};
async function post(path,body){let r=await fetch(path,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});return r.json();}
document.getElementById('wifi').onsubmit=async e=>{e.preventDefault();save.disabled=true;status.textContent='Starting connection test…';try{let data=new URLSearchParams(new FormData(e.target));if(open.checked)data.set('password','');const r=await post('/save',data);status.textContent=r.message;if(!r.ok)save.disabled=false;password.value='';}catch{status.textContent='Connection lost. Rejoin the setup Wi-Fi and reopen this page.';save.disabled=false;}};
document.getElementById('exit').onclick=async()=>{try{const r=await post('/exit',new URLSearchParams({csrf}));status.textContent=r.message;stopped=true;}catch{status.textContent='Setup connection closed. Check the OLED.';}};
async function poll(){if(stopped)return;try{const r=await(await fetch('/status',{cache:'no-store'})).json();status.textContent=r.message;save.disabled=r.busy||r.saved;if(r.saved){status.textContent+=' Rejoin your normal Wi-Fi. The backend must be reachable on that network.';stopped=true;}}catch{status.textContent='Setup connection unavailable. Check the OLED or rejoin its Wi-Fi.';}if(!stopped)setTimeout(poll,1500);}
async function networks(){try{const r=await(await fetch('/networks',{cache:'no-store'})).json();const list=document.getElementById('networks');list.replaceChildren();for(const ssid of r.networks){const option=document.createElement('option');option.value=ssid;list.append(option);}if(r.scanning)setTimeout(networks,2000);}catch{}}
poll();networks();</script></html>)HTML";

bool saveProfile(const WifiProfile &profile) {
  Preferences prefs;
  if (!prefs.begin("buddy-net", false)) return false;
  const WifiRecord record = encodeWifiProfile(profile);
  const bool ok = prefs.putBytes("profile", &record, sizeof(record)) == sizeof(record);
  prefs.end();
  return ok;
}
void connectCurrent() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  if (haveProfile) WiFi.begin(current.ssid, current.open ? nullptr : current.password);
}
void finishPortal() {
  client.stop(); http.end(); esp_wifi_scan_stop(); WiFi.scanDelete(); request = SetupHttpRequest();
  WiFi.disconnect(false, false);
  WiFi.softAPdisconnect(false);
  mode = Mode::Normal; trial = Trial::None;
  apPassword = ""; csrf = "";
  connectCurrent();
  notify(haveProfile ? "Setup closed" : "Restart for setup");
  Serial.println("Wi-Fi setup closed; saved configuration retained");
}
void openPortal() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(false);
  char name[24];
  snprintf(name, sizeof(name), "Buddy-%06lX", static_cast<unsigned long>(ESP.getEfuseMac() & 0xffffff));
  apName = name;
  constexpr char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  apPassword = ""; csrf = "";
  for (unsigned i = 0; i < 12; ++i) apPassword += alphabet[esp_random() % (sizeof(alphabet) - 1)];
  for (unsigned i = 0; i < 4; ++i) { char part[9]; snprintf(part, sizeof(part), "%08lx", static_cast<unsigned long>(esp_random())); csrf += part; }
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  if (!WiFi.softAP(apName.c_str(), apPassword.c_str(), 1, false, 2)) {
    mode = Mode::Normal; connectCurrent(); notify("Setup AP failed"); return;
  }
  mode = Mode::Portal; trial = Trial::None; openedAt = millis();
  http.begin();
  if (!http) { finishPortal(); notify("Setup server failed"); return; }
  WiFi.scanNetworks(true);
  notify("Open setup page");
  Serial.println("Wi-Fi setup opened; connection details are on the OLED only");
}
void reply(int code, const char *type, const String &body) {
  String header = "HTTP/1.1 " + String(code) + (code == 200 ? " OK" : " Error");
  header += "\r\nContent-Type: "; header += type;
  header += "\r\nCache-Control: no-store\r\nConnection: close\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nContent-Security-Policy: default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; frame-ancestors 'none'; form-action 'self'\r\nContent-Length: ";
  header += body.length(); header += "\r\n\r\n";
  client.print(header); client.print(body); client.stop();
}
void result(int code, bool ok, const char *text) {
  JsonDocument doc; doc["ok"] = ok; doc["message"] = text;
  String body; serializeJson(doc, body); reply(code, "application/json", body);
}
void handleRequest() {
  if (!strcmp(request.method(), "GET")) {
    if (!strcmp(request.path(), "/")) {
      String page = FPSTR(kPage); page.replace("__CSRF__", csrf);
      reply(200, "text/html; charset=utf-8", page);
    } else if (!strcmp(request.path(), "/status")) {
      JsonDocument doc;
      doc["message"] = message;
      doc["busy"] = trial == Trial::Testing || trial == Trial::Disconnecting || trial == Trial::Exit;
      doc["saved"] = trial == Trial::Saved;
      String body; serializeJson(doc, body); reply(200, "application/json", body);
    } else if (!strcmp(request.path(), "/networks")) {
      JsonDocument doc; JsonArray networks = doc["networks"].to<JsonArray>();
      const int found = WiFi.scanComplete();
      doc["scanning"] = found == WIFI_SCAN_RUNNING;
      for (int i = 0; i < found && i < 20; ++i) if (WiFi.SSID(i).length()) networks.add(WiFi.SSID(i));
      String body; serializeJson(doc, body); reply(200, "application/json", body);
    } else result(404, false, "Page not found");
    return;
  }
  char supplied[33];
  if (!setupFormValue(request.body(), "csrf", supplied, sizeof(supplied)) || csrf != supplied) {
    result(403, false, "Setup session expired. Reload this page."); return;
  }
  if (!strcmp(request.path(), "/exit")) {
    result(200, true, "Setup is closing. Rejoin your normal Wi-Fi.");
    trial = Trial::Exit; closeAt = millis() + 500; return;
  }
  if (strcmp(request.path(), "/save")) { result(404, false, "Page not found"); return; }
  if (trial != Trial::None) { result(409, false, "Wait for the current connection test."); return; }
  WifiProfile next;
  char open[2];
  const bool hasOpen = setupFormValue(request.body(), "open", open, sizeof(open));
  next.open = hasOpen && !strcmp(open, "1");
  if (!setupFormValue(request.body(), "ssid", next.ssid, sizeof(next.ssid)) ||
      !setupFormValue(request.body(), "password", next.password, sizeof(next.password)) || !validWifiProfile(next)) {
    result(400, false, "Use a network name up to 32 bytes and an 8–63 ASCII character password (or 64 hex digits). Select open network only when it has no password."); return;
  }
  candidate = next;
  result(200, true, "Testing Wi-Fi. This may take up to 25 seconds; stay on the setup network.");
  esp_wifi_scan_stop(); WiFi.scanDelete();
  WiFi.setAutoReconnect(false); WiFi.disconnect(false, false);
  trial = Trial::Disconnecting; trialAt = millis(); notify("Testing Wi-Fi...");
}
void serve() {
  if (!client) {
    client = http.available();
    if (!client) return;
    if (client.localIP() != IPAddress(192,168,4,1)) { client.stop(); return; }
    client.setTimeout(100);
    request = SetupHttpRequest(); clientAt = millis();
  }
  if (millis() - clientAt > 3000) { client.stop(); return; }
  // Limit parsing work per loop; capture is idle and the timer task remains independent.
  for (unsigned i = 0; i < 512 && client.available(); ++i) {
    const auto state = request.feed(static_cast<char>(client.read()));
    if (state == SetupHttpRequest::State::Complete) { handleRequest(); return; }
    if (state != SetupHttpRequest::State::Reading) { result(400, false, "Invalid or oversized request"); return; }
  }
  if (!client.connected()) client.stop();
}
}

void wifiSetupInit(const char *legacySsid, const char *legacyPassword, WifiSetupChanged callback) {
  changed = callback;
  Preferences prefs;
  WifiRecord record;
  if (prefs.begin("buddy-net", true)) {
    haveProfile = prefs.getBytesLength("profile") == sizeof(record) &&
      prefs.getBytes("profile", &record, sizeof(record)) == sizeof(record) && decodeWifiProfile(record, current);
    prefs.end();
  }
  if (haveProfile) Serial.println("Wi-Fi configuration source: saved device profile");
  else if (strlen(legacySsid) <= 32 && strlen(legacyPassword) <= 64) {
    strcpy(current.ssid, legacySsid); strcpy(current.password, legacyPassword);
    current.open = !strlen(legacyPassword);
    haveProfile = validWifiProfile(current);
    if (haveProfile) Serial.println("Wi-Fi configuration source: legacy firmware fallback");
  }
  WiFi.persistent(false); WiFi.mode(WIFI_STA); WiFi.setSleep(false);
  const bool held = digitalRead(33) == HIGH;
  gesture.begin(held, millis());
  if (held) { mode = Mode::Holding; notify("Keep holding pad..."); }
  else if (!haveProfile) openPortal();
  else connectCurrent();
}
void wifiSetupLoop() {
  if (mode == Mode::Holding) {
    const auto result = gesture.poll(digitalRead(33) == HIGH, millis());
    if (result == BootSetupGesture::Result::OpenSetup) openPortal();
    else if (result == BootSetupGesture::Result::Normal) {
      mode = Mode::Normal;
      if (!haveProfile) openPortal(); else { connectCurrent(); notify("Normal startup"); }
    }
    return;
  }
  if (mode != Mode::Portal) return;
  const uint32_t now = millis();
  if (now - openedAt >= kPortalMs || ((trial == Trial::Saved || trial == Trial::Exit) && int32_t(now - closeAt) >= 0)) {
    finishPortal(); return;
  }
  if (trial == Trial::Disconnecting) {
    wifi_ap_record_t info;
    if (WiFi.status() != WL_CONNECTED && esp_wifi_sta_get_ap_info(&info) != ESP_OK) {
      WiFi.begin(candidate.ssid, candidate.open ? nullptr : candidate.password);
      trial = Trial::Testing;
    }
  }
  if (trial == Trial::Testing && WiFi.status() == WL_CONNECTED &&
      WiFi.SSID() == candidate.ssid && uint32_t(WiFi.localIP()) != 0) {
    if (saveProfile(candidate)) {
      current = candidate; haveProfile = true;
      trial = Trial::Saved; closeAt = now + 15000;
      notify("Wi-Fi saved"); Serial.println("Wi-Fi candidate connected and saved; setup closes shortly");
    } else {
      trial = Trial::None; WiFi.disconnect(false, false);
      notify("Storage failed. Retry or exit; previous settings were retained.");
    }
    candidate = {};
  } else if ((trial == Trial::Testing || trial == Trial::Disconnecting) && now - trialAt >= 25000) {
    WiFi.disconnect(false, false); candidate = {}; trial = Trial::None;
    notify("Connection failed. Check the password and 2.4 GHz signal, then retry. Previous settings were retained.");
    Serial.println("Wi-Fi candidate failed; previous settings retained");
  }
  serve();
}
bool wifiSetupActive() { return mode != Mode::Normal; }
bool wifiSetupHolding() { return mode == Mode::Holding; }
bool wifiSetupHasProfile() { return haveProfile; }
const char *wifiSetupApName() { return apName.c_str(); }
const char *wifiSetupApPassword() { return apPassword.c_str(); }
const char *wifiSetupMessage() { return message; }
#else
void wifiSetupInit(const char *, const char *, WifiSetupChanged) {}
void wifiSetupLoop() {}
bool wifiSetupActive() { return false; }
bool wifiSetupHolding() { return false; }
bool wifiSetupHasProfile() { return false; }
const char *wifiSetupApName() { return ""; }
const char *wifiSetupApPassword() { return ""; }
const char *wifiSetupMessage() { return ""; }
#endif
