// Cardputer homelab panel: shows CPU/RAM/disk/GPU for every machine, read from the fleet-panel
// service (server/fleet-panel.py) on the Mac mini. Wi-Fi list lives in include/secrets.h.
// The service is LAN-only, so the panel only works on the home network.
#include <M5Cardputer.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <algorithm>
#include <vector>
#include "secrets.h"

#define N(a) (int)(sizeof(a) / sizeof(a[0]))

// ---------- data source ----------

#define PANEL_URL "http://192.168.8.110:8791/panel"  // Mac mini, launchd dev.cobanov.fleet-panel
const uint32_t REFRESH_MS = 15000;

struct Host {
  String name, diskPath, gpuName, activity;
  bool up = false;
  float cpu = NAN, mem = NAN, memUsed = NAN, memTotal = NAN, disk = NAN, temp = NAN, load = NAN, gpu = NAN, vram = NAN;
  int cores = 0, contRun = -1, contTotal = -1;
};

// ---------- look ----------

struct Tz { const char* label; const char* posix; };
static const Tz TZS[] = {
    {"IST", "<+03>-3"}, {"LON", "GMT0BST,M3.5.0/1,M10.5.0"}, {"NYC", "EST5EDT,M3.2.0,M11.1.0"},
    {"SF", "PST8PDT,M3.2.0,M11.1.0"}, {"UTC", "UTC0"},
};

// RGB888, cobanov.dev palette
constexpr uint32_t BG = 0x0B0B12, BAR = 0x16162A, SEL = 0x24244A, TRACK = 0x26263A, TEXT = 0xF2F2F7,
                   DIM = 0x9A9AB0, MUTED = 0x5C5C75, ACCENT = 0x6E81E4, C_OK = 0x4CD07D, C_WARN = 0xF5C04A,
                   C_ERR = 0xF2555A;

// 240x135: status bar 0-13, title 17, content 28-120, footer 122-134. Font 6x8.
const int W = 240, CONTENT_Y = 28, ROW_H = 13, ROWS = 7, LINE_H = 10, FOOT_Y = 122;
const int COL_X[] = {84, 136, 188};  // CPU, RAM, DISK

// ---------- state ----------

enum Page { HOSTS, DETAIL, HELP, QUIT };
Page page = HOSTS, helpReturn = HOSTS;
std::vector<Host> hosts;
int sel = 0, scroll = 0;
bool fetchOk = false;
int dataAge = 0;
uint32_t lastFetch = 0, lastOk = 0, lastScan = 0, lastRender = 0, toastUntil = 0;
String toastMsg;
int tzIdx = 0;
bool ntpStarted = false;
Preferences prefs;
M5Canvas C(&M5Cardputer.Display);  // draw here first, then push, so the screen does not flicker

// ---------- helpers ----------

void text(const String& s, int x, int y, uint32_t color, textdatum_t d = top_left) {
  C.setTextColor(color);
  C.setTextDatum(d);
  C.drawString(s, x, y);
}

bool online() { return WiFi.status() == WL_CONNECTED; }
void toast(const String& msg) { toastMsg = msg; toastUntil = millis() + 2000; }
uint32_t level(float v) { return v >= 90 ? C_ERR : v >= 75 ? C_WARN : C_OK; }
String pct(float v) { return isnan(v) ? String("--") : String((int)roundf(v)) + "%"; }

void bar(int x, int y, int w, int h, float v) {
  C.fillRect(x, y, w, h, TRACK);
  if (!isnan(v)) C.fillRect(x, y, (int)(w * constrain(v, 0.0f, 100.0f) / 100), h, level(v));
}

// ---------- data ----------

float num(JsonVariant v) { return v.isNull() ? NAN : v.as<float>(); }

// Fetches the summary; on failure the previous data stays.
bool fetch() {
  lastFetch = millis();
  if (!online()) return fetchOk = false;
  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(1500);  // keep the UI from freezing long when off the home network
  http.setTimeout(3000);
  http.begin(client, PANEL_URL);
  int code = http.GET();
  String body = code == 200 ? http.getString() : "";
  http.end();
  JsonDocument doc;
  if (code != 200 || deserializeJson(doc, body)) return fetchOk = false;

  std::vector<Host> fresh;
  for (JsonObject o : doc["hosts"].as<JsonArray>()) {
    Host h;
    h.name = o["name"] | "?";
    h.up = o["ok"] | false;
    h.cpu = num(o["cpu"]);
    h.mem = num(o["mem"]);
    h.memUsed = num(o["mu"]);
    h.memTotal = num(o["mt"]);
    h.disk = num(o["disk"]);
    h.diskPath = o["dpath"] | "";
    h.temp = num(o["temp"]);
    h.load = num(o["load"]);
    h.cores = o["cores"] | 0;
    h.gpu = num(o["gpu"]);
    h.vram = num(o["vram"]);
    h.gpuName = o["gname"] | "";
    h.contRun = o["crun"] | -1;
    h.contTotal = o["ctot"] | -1;
    h.activity = o["act"] | "";
    fresh.push_back(h);
  }
  hosts = fresh;
  sel = constrain(sel, 0, max(0, (int)hosts.size() - 1));
  dataAge = doc["age"] | 0;
  lastOk = millis();
  return fetchOk = true;
}

// ---------- drawing ----------

void statusBar() {
  C.fillRect(0, 0, W, 14, BAR);
  text(online() ? WiFi.SSID().substring(0, 16) : String("offline"), 4, 3, online() ? DIM : C_ERR);

  int bat = M5.Power.getBatteryLevel();
  String b = bat >= 0 ? String(bat) + "%" : "--";
  text(b, W - 4, 3, bat >= 0 && bat < 20 ? C_ERR : DIM, top_right);
  int x = W - 4 - C.textWidth(b) - 6;

  int rssi = online() ? WiFi.RSSI() : -127;
  int lv = rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : rssi > -85 ? 1 : 0;
  for (int i = 0; i < 4; i++) C.fillRect(x - 14 + i * 4, 9 - i * 2, 3, 2 + i * 2, i < lv ? ACCENT : MUTED);

  char clock[8] = "--:--";
  time_t now = time(nullptr);
  if (now > 1700000000) {  // clock reads 1970 until NTP syncs
    struct tm t;
    localtime_r(&now, &t);
    strftime(clock, sizeof clock, "%H:%M", &t);
  }
  text(clock, x - 22, 3, TEXT, top_right);
}

void footer(const String& hint) {
  C.fillRect(0, FOOT_Y, W, 135 - FOOT_Y, BAR);
  bool t = millis() < toastUntil;
  text(t ? toastMsg : hint, 4, FOOT_Y + 3, t ? ACCENT : MUTED);
}

// Top right: data age (service reading + our fetch) or an error
void freshness() {
  if (!lastOk) { if (lastFetch) text("unreachable", W - 4, 17, C_ERR, top_right); }
  else text(String(dataAge + (millis() - lastOk) / 1000) + "s ago", W - 4, 17, fetchOk ? MUTED : C_ERR, top_right);
}

void drawHosts() {
  text("HOMELAB", 4, 17, ACCENT);
  const char* cols[] = {"CPU", "RAM", "DISK"};
  for (int c = 0; c < 3; c++) text(cols[c], COL_X[c], 17, MUTED);
  if (!lastOk) freshness();
  if (hosts.empty()) {
    text(!online() ? "waiting for Wi-Fi..." : lastFetch ? "Mac mini unreachable" : "loading...", W / 2, 70, DIM,
         middle_center);
    footer(String(PANEL_URL).substring(7));
    return;
  }
  scroll = constrain(scroll, max(0, sel - ROWS + 1), sel);
  for (int r = 0; r < ROWS && scroll + r < (int)hosts.size(); r++) {
    const Host& h = hosts[scroll + r];
    int y = CONTENT_Y + r * ROW_H;
    if (scroll + r == sel) C.fillRect(0, y - 1, W, ROW_H, SEL);
    C.fillCircle(6, y + 4, 2, h.up ? C_OK : C_ERR);
    text(h.name.substring(0, 11), 12, y + 1, h.up ? TEXT : C_ERR);
    if (!h.up) { text("unreachable", COL_X[0], y + 1, C_ERR); continue; }
    float v[] = {h.cpu, h.mem, h.disk};
    for (int c = 0; c < 3; c++) {
      text(pct(v[c]), COL_X[c], y + 1, isnan(v[c]) ? MUTED : TEXT);
      bar(COL_X[c], y + 9, 44, 2, v[c]);
    }
  }
  int down = std::count_if(hosts.begin(), hosts.end(), [](const Host& h) { return !h.up; });
  String summary = String(hosts.size() - down) + "/" + String(hosts.size()) + " up";
  footer(lastOk && !fetchOk ? String("Mac mini unreachable") : summary + "  -  Enter for details");
}

void drawDetail() {
  if (hosts.empty()) { page = HOSTS; return drawHosts(); }
  const Host& h = hosts[sel];
  text(h.name, 4, 17, ACCENT);
  text(h.up ? "UP" : "UNREACHABLE", 4 + C.textWidth(h.name) + 8, 17, h.up ? C_OK : C_ERR);
  freshness();
  String hint = String(sel + 1) + "/" + String(hosts.size()) + "  j/k next  q back  r refresh";
  if (!h.up) {
    text("SSH connection failed.", 4, CONTENT_Y + 10, DIM);
    text("Powered off, asleep or off the network.", 4, CONTENT_Y + 22, DIM);
    return footer(hint);
  }

  String cpu = pct(h.cpu);
  if (h.cores) cpu += "  " + String(h.cores) + " cores";
  if (!isnan(h.load)) cpu += "  load " + String(h.load, 1);
  if (!isnan(h.temp)) cpu += "  " + String((int)h.temp) + "C";
  String mem = isnan(h.memTotal) ? String("--") : String(h.memUsed, 1) + " / " + String(h.memTotal, 1) + " GB";
  String disk = pct(h.disk) + (h.diskPath.length() ? "  " + h.diskPath : "");
  struct { const char* label; String value; float v; } rows[] = {
      {"CPU", cpu, h.cpu}, {"RAM", mem, h.mem}, {"DISK", disk, h.disk},
  };
  for (int i = 0; i < 3; i++) {
    int y = CONTENT_Y + i * 18;
    text(rows[i].label, 4, y, MUTED);
    text(rows[i].value, 40, y, TEXT);
    bar(40, y + 10, W - 46, 3, rows[i].v);
  }

  int y = CONTENT_Y + 56;
  if (h.gpuName.length()) {
    String gpu = h.gpuName + "  " + pct(h.gpu);
    if (!isnan(h.vram)) gpu += "  VRAM " + pct(h.vram);
    text("GPU", 4, y, MUTED);
    text(gpu, 40, y, TEXT);
    y += 11;
  }
  if (h.contTotal >= 0) {
    text("CONT", 4, y, MUTED);
    text(String(h.contRun) + " / " + String(h.contTotal) + " running", 40, y, h.contRun < h.contTotal ? C_WARN : TEXT);
    y += 11;
  }
  if (h.activity.length()) text(h.activity, 40, y, DIM);
  footer(hint);
}

void drawHelp() {
  text("HELP", 4, 17, ACCENT);
  const char* keys[][2] = {
      {"j k  ; .", "select machine"},  {"Enter 1-9", "details"},      {"q ` del", "back"},
      {"r", "refresh now (auto every 15 s)"}, {"t", "sync clock (NTP)"},
      {"z", "change time zone"},     {"x", "restart"},              {"i", "close help"},
  };
  for (int i = 0; i < N(keys); i++) {
    text(keys[i][0], 4, CONTENT_Y + i * LINE_H, ACCENT);
    text(keys[i][1], 64, CONTENT_Y + i * LINE_H, DIM);
  }
  footer(String("time zone: ") + TZS[tzIdx].label);
}

void drawQuit() {
  text("RESTART", 4, 17, ACCENT);
  text("Restart the device?", W / 2, 66, TEXT, middle_center);
  footer("Enter yes   q cancel");
}

void render() {
  C.fillScreen(BG);
  statusBar();
  switch (page) {
    case HOSTS: drawHosts(); break;
    case DETAIL: drawDetail(); break;
    case HELP: drawHelp(); break;
    case QUIT: drawQuit(); break;
  }
  C.pushSprite(0, 0);
  lastRender = millis();
}

// ---------- network and clock ----------

void applyTz() {
  setenv("TZ", TZS[tzIdx].posix, 1);
  tzset();
}

void startNtp() { configTzTime(TZS[tzIdx].posix, "pool.ntp.org", "time.google.com"); }

// Non-blocking: scans in the background and joins the strongest known network.
void wifiTick() {
  if (online()) {
    if (!ntpStarted) { startNtp(); ntpStarted = true; }
    return;
  }
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;
  if (n >= 0) {
    int best = -1, bestRssi = -999;
    for (int i = 0; i < n; i++)
      for (int k = 0; k < N(WIFI_LIST); k++)
        if (WiFi.SSID(i) == WIFI_LIST[k].ssid && WiFi.RSSI(i) > bestRssi) { best = k; bestRssi = WiFi.RSSI(i); }
    WiFi.scanDelete();
    if (best >= 0) {
      WiFi.begin(WIFI_LIST[best].ssid, WIFI_LIST[best].pass);
      lastScan = millis();
      return;
    }
  }
  // shortcut: hidden SSIDs never show up in a scan; try WiFi.begin on each in turn if one is needed
  if (lastScan == 0 || millis() - lastScan > 20000) {
    WiFi.scanNetworks(true);
    lastScan = millis();
  }
}

// ---------- keys ----------

bool isBack(char k) { return k == 'q' || k == '`' || k == 8; }

void handleKey(char k) {
  k = tolower(k);
  bool up = k == 'k' || k == ';', down = k == 'j' || k == '.';
  int n = hosts.size();

  switch (page) {
    case QUIT:
      if (k == '\n') ESP.restart();
      if (isBack(k)) page = HOSTS;
      return;
    case HELP:
      if (isBack(k) || k == 'i') page = helpReturn;
      return;
    case HOSTS:
    case DETAIL:
      if ((up || down) && n) sel = (sel + (up ? n - 1 : 1)) % n;
      else if (k == '\n' && n) page = DETAIL;
      else if (k >= '1' && k <= '9' && k - '1' < n) { sel = k - '1'; page = DETAIL; }
      else if (isBack(k)) page = HOSTS;
      break;
  }

  switch (k) {
    case 'r':
      toast(fetch() ? "refreshed" : "Mac mini unreachable");
      break;
    case 't':
      if (online()) startNtp();
      toast(online() ? "NTP requested" : "no Wi-Fi");
      break;
    case 'z':
      tzIdx = (tzIdx + 1) % N(TZS);
      prefs.putInt("tz", tzIdx);
      applyTz();
      toast(String("time zone: ") + TZS[tzIdx].label);
      break;
    case 'i': helpReturn = page; page = HELP; break;
    case 'x': page = QUIT; break;
  }
}

// ---------- main loop ----------

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  M5Cardputer.Display.setRotation(1);
  C.createSprite(W, 135);
  C.setFont(&fonts::Font0);
  prefs.begin("panel");
  tzIdx = prefs.getInt("tz", 0) % N(TZS);
  applyTz();
  WiFi.mode(WIFI_STA);
  render();
}

void loop() {
  M5Cardputer.update();
  if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
    auto st = M5Cardputer.Keyboard.keysState();
    if (st.enter) handleKey('\n');
    else if (st.del) handleKey(8);
    else for (char c : st.word) handleKey(c);
    render();
  }
  wifiTick();
  bool due = lastFetch == 0 || millis() - lastFetch > REFRESH_MS;
  if (online() && due) { fetch(); render(); }
  if (millis() - lastRender > 1000) render();  // clock, battery, Wi-Fi, data age
  delay(10);
}
