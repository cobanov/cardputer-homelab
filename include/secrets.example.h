#pragma once
// Copy to secrets.h and fill in (git-ignored)
struct WifiCred { const char* ssid; const char* pass; };
static const WifiCred WIFI_LIST[] = {
  {"ssid", "password"},
};
