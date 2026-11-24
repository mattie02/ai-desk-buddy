#pragma once
#include <Arduino.h>
using WifiSetupChanged = void (*)();
void wifiSetupInit(const char *legacySsid, const char *legacyPassword, WifiSetupChanged changed);
void wifiSetupLoop();
bool wifiSetupActive();
bool wifiSetupHolding();
bool wifiSetupHasProfile();
const char *wifiSetupApName();
const char *wifiSetupApPassword();
const char *wifiSetupMessage();
