#pragma once
// Copy to secrets.h and fill in (git-ignored)
struct WifiCred { const char* ssid; const char* pass; };
static const WifiCred WIFI_LIST[] = {
  {"ssid", "password"},
};

// Cloudflare Access service token for the panel hostname
#define CF_ACCESS_CLIENT_ID "xxxx.access"
#define CF_ACCESS_CLIENT_SECRET "xxxx"
