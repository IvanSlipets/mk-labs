#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <WiFiClient.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <LittleFS.h>
#include <time.h>

// ── Settings ─────────────────────────────────────────────────────────────
const String apiKey   = "fe004823516183c429297030e93eca67";
const String cities[] = {"Lviv,UA", "Zhovkva,UA"};
const String labels[] = {"Lv", "Zh"};
const unsigned long WEATHER_MS = 600000UL;
const unsigned long SWITCH_MS  =   8000UL;

// ── Log Rotation Settings ────────────────────────────────────────────────
const int LOG_FILES   = 5;   
const int MAX_ENTRIES = 120; 
int logFileIndex = 0;
int logEntries   = 0;

// ── LCD & Custom Characters ──────────────────────────────────────────────
LiquidCrystal_I2C lcd(0x26, 16, 2);
byte dropEmpty[8] = {0b00100,0b00100,0b01010,0b01010,0b10001,0b10001,0b10001,0b01110};
byte dropMid[8]   = {0b00100,0b00100,0b01010,0b01010,0b10001,0b11111,0b11111,0b01110};
byte degree[8]    = {0b00110,0b01001,0b01001,0b00110,0b00000,0b00000,0b00000,0b00000};
byte dropHigh[8]  = {0b00100,0b00100,0b01010,0b01110,0b11111,0b11111,0b11111,0b01110};
byte dropFull[8]  = {0b00100,0b00100,0b01110,0b01110,0b11111,0b11111,0b11111,0b01110};

// ── State Variables ──────────────────────────────────────────────────────
float cachedTemp[2] = {0, 0};
int   cachedHum[2]  = {0, 0};
bool  dataReady[2]  = {false, false};

int           currentCity       = 0;
unsigned long lastWeatherUpdate = 0;
unsigned long lastCitySwitch    = 0;
unsigned long lastClockUpdate   = 0;

ESP8266WebServer server(80);

// ═════════════════════════════════════════════════════════════════════════
//  TIME FUNCTIONS
// ═════════════════════════════════════════════════════════════════════════

String isoTimestamp() {
  time_t now = time(nullptr);
  struct tm* s = localtime(&now);     
  char buf[20];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
    s->tm_year + 1900, s->tm_mon + 1, s->tm_mday,
    s->tm_hour, s->tm_min, s->tm_sec);
  return String(buf);
}

void waitNtpSync() {
  lcd.clear();
  lcd.print("NTP sync...");
  time_t now = time(nullptr);
  int tries = 0;
  while (now < 1000000000UL && tries++ < 30) {
    delay(500);
    now = time(nullptr);
  }
}

// ═════════════════════════════════════════════════════════════════════════
//  LCD FUNCTIONS
// ═════════════════════════════════════════════════════════════════════════

void initLcd() {
  lcd.init();
  lcd.backlight();
  lcd.createChar(1, dropEmpty);
  lcd.createChar(2, dropMid);
  lcd.createChar(3, degree);
  lcd.createChar(4, dropHigh);
  lcd.createChar(5, dropFull);
}

void displayClock() {
  time_t now = time(nullptr);
  struct tm* s = localtime(&now);     
  char buf[17];
  snprintf(buf, sizeof(buf), "    %02d:%02d:%02d    ", s->tm_hour, s->tm_min, s->tm_sec);
  lcd.setCursor(0, 0);
  lcd.print(buf);
}

void displayWeather(int idx) {
  lcd.setCursor(0, 1);
  if (!dataReady[idx]) {
    lcd.print("   Loading...   ");
    return;
  }
  float t = cachedTemp[idx];
  int   h = cachedHum[idx];
  lcd.print(labels[idx] + ": ");
  lcd.print(t > 0 ? "+" : "");
  lcd.print(t, 1);
  lcd.write(3);
  lcd.print(" ");
  if       (h < 30) lcd.write(1);
  else if (h < 60) lcd.write(2);
  else if (h < 85) lcd.write(4);
  else             lcd.write(5);
  lcd.print(String(h) + "%  ");
}

void clearWeatherRow() {
  lcd.setCursor(0, 1);
  lcd.print("                ");
}

// ═════════════════════════════════════════════════════════════════════════
//  WEATHER FUNCTIONS
// ═════════════════════════════════════════════════════════════════════════

void fetchWeather(int idx) {
  WiFiClient client;
  HTTPClient http;
  String url = "http://api.openweathermap.org/data/2.5/weather?q=" + cities[idx]
               + "&appid=" + apiKey + "&units=metric";
  http.begin(client, url);
  if (http.GET() > 0) {
    JsonDocument doc;
    deserializeJson(doc, http.getString());
    cachedTemp[idx] = doc["main"]["temp"];
    cachedHum[idx]  = doc["main"]["humidity"];
    dataReady[idx]  = true;
  }
  http.end();
}

void fetchAllWeather() {
  fetchWeather(0);
  fetchWeather(1);
}

// ═════════════════════════════════════════════════════════════════════════
//  LOG FILE FUNCTIONS
// ═════════════════════════════════════════════════════════════════════════

String logPath(int idx) {
  return "/log" + String(idx) + ".csv";
}

void saveState() {
  File f = LittleFS.open("/state.dat", "w");
  if (!f) return;
  f.println(logFileIndex);
  f.println(logEntries);
  f.close();
}

void loadState() {
  if (!LittleFS.exists("/state.dat")) return;
  File f = LittleFS.open("/state.dat", "r");
  if (!f) return;
  int fi = f.readStringUntil('\n').toInt();
  int en = f.readStringUntil('\n').toInt();
  f.close();
  logFileIndex = (fi >= 0 && fi < LOG_FILES)  ? fi : 0;
  logEntries   = (en >= 0 && en < MAX_ENTRIES) ? en : 0;
}

void createLogHeader(String path) {
  File f = LittleFS.open(path, "w");
  if (f) {
    f.println("timestamp,city,temp_c,humidity_pct");
    f.close();
  }
}

void initCurrentLog() {
  String path = logPath(logFileIndex);
  if (!LittleFS.exists(path)) createLogHeader(path);
}

void rotateLogs() {
  logFileIndex = (logFileIndex + 1) % LOG_FILES;
  logEntries   = 0;
  String path  = logPath(logFileIndex);
  if (LittleFS.exists(path)) LittleFS.remove(path);
  createLogHeader(path);
  saveState();
  Serial.printf("[LOG] Rotation → log%d.csv\n", logFileIndex);
}

void logWeather() {
  if (logEntries >= MAX_ENTRIES) rotateLogs();

  File f = LittleFS.open(logPath(logFileIndex), "a");
  if (!f) { Serial.println("[LOG] Open error"); return; }

  String ts = isoTimestamp();
  for (int i = 0; i < 2; i++) {
    if (!dataReady[i]) continue;
    f.print(ts);              f.print(",");
    f.print(labels[i]);       f.print(",");
    f.print(cachedTemp[i], 1);f.print(",");
    f.println(cachedHum[i]);
    logEntries++;
  }
  f.close();
  saveState();
  Serial.printf("[LOG] log%d.csv — %d/%d\n", logFileIndex, logEntries, MAX_ENTRIES);
}

void initFilesystem() {
  if (!LittleFS.begin()) {
    Serial.println("[FS] Mount error — formatting...");
    LittleFS.format();
    LittleFS.begin();
  }
  loadState();
  initCurrentLog();
  Serial.printf("[LOG] Start: log%d.csv, %d/%d entries\n",
    logFileIndex, logEntries, MAX_ENTRIES);
}

// ═════════════════════════════════════════════════════════════════════════
//  WEB SERVER FUNCTIONS
// ═════════════════════════════════════════════════════════════════════════

String buildIndexPage() {
  FSInfo fs;
  LittleFS.info(fs);
  unsigned long usedKb  = fs.usedBytes  / 1024;
  unsigned long totalKb = fs.totalBytes / 1024;

  String html =
    "<!DOCTYPE html><html><head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Meteo Logs</title>"
    "<style>"
      "body{font-family:monospace;max-width:600px;margin:40px auto;padding:0 16px;"
           "background:#111;color:#ccc}"
      "h2{color:#7cf;margin-bottom:4px}"
      "p.sub{color:#666;font-size:.85em;margin-top:0}"
      "table{width:100%;border-collapse:collapse;margin-top:20px}"
      "th{text-align:left;color:#7cf;border-bottom:1px solid #333;padding:6px 8px}"
      "td{padding:6px 8px;border-bottom:1px solid #222}"
      "tr:hover td{background:#1a1a1a}"
      "a{color:#7cf;text-decoration:none}"
      "a:hover{text-decoration:underline}"
      ".cur{color:#6f6}"
      ".fs{color:#888;font-size:.8em;margin-top:20px}"
    "</style></head><body>"
    "<h2>&#127781; Meteo Logs</h2>"
    "<p class='sub'>" + isoTimestamp() + " &nbsp;|&nbsp; "
    + WiFi.localIP().toString() + "</p>"
    "<table><tr><th>File</th><th>Size</th><th>Status</th><th></th></tr>";

  for (int i = 0; i < LOG_FILES; i++) {
    String path = logPath(i);
    bool exists = LittleFS.exists(path);
    html += "<tr><td>";
    if (exists) {
      html += "<a href='/log" + String(i) + ".csv'>log" + String(i) + ".csv</a>";
    } else {
      html += "<span style='color:#555'>log" + String(i) + ".csv</span>";
    }
    html += "</td><td>";
    if (exists) {
      File f = LittleFS.open(path, "r");
      html += String(f.size()) + " B";
      f.close();
    } else {
      html += "—";
    }
    html += "</td><td>";
    if (i == logFileIndex)
      html += "<span class='cur'>&#9679; active (" + String(logEntries)
           + "/" + String(MAX_ENTRIES) + ")</span>";
    else if (exists)
      html += "archive";
    else
      html += "<span style='color:#555'>empty</span>";
    html += "</td><td>";
    if (exists)
      html += "<a href='/delete?f=" + String(i) + "'>&#128465;</a>";
    html += "</td></tr>";
  }

  html += "</table>"
    "<p class='fs'>LittleFS: " + String(usedKb) + " / " + String(totalKb) + " KB</p>"
    "</body></html>";
  return html;
}

void handleRoot() {
  server.send(200, "text/html; charset=utf-8", buildIndexPage());
}

void handleLogFile(int idx) {
  String path = logPath(idx);
  if (!LittleFS.exists(path)) {
    server.send(404, "text/plain", "Not found");
    return;
  }
  File f = LittleFS.open(path, "r");
  server.sendHeader("Content-Disposition",
    "attachment; filename=\"log" + String(idx) + ".csv\"");
  server.streamFile(f, "text/csv");
  f.close();
}

void handleDelete() {
  if (!server.hasArg("f")) { server.send(400, "text/plain", "Bad request"); return; }
  int idx = server.arg("f").toInt();
  if (idx < 0 || idx >= LOG_FILES) { server.send(400, "text/plain", "Bad index"); return; }
  if (idx == logFileIndex) { server.send(403, "text/plain", "Cannot delete active log"); return; }
  String path = logPath(idx);
  if (LittleFS.exists(path)) LittleFS.remove(path);
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}

void initWebServer() {
  server.on("/", handleRoot);
  server.on("/log0.csv", []() { handleLogFile(0); });
  server.on("/log1.csv", []() { handleLogFile(1); });
  server.on("/log2.csv", []() { handleLogFile(2); });
  server.on("/log3.csv", []() { handleLogFile(3); });
  server.on("/log4.csv", []() { handleLogFile(4); });
  server.on("/delete",   handleDelete);
  server.begin();
  Serial.print("[WEB] http://");
  Serial.println(WiFi.localIP());
}

// ═════════════════════════════════════════════════════════════════════════
//  WIFIMAGER CALLBACKS
// ═════════════════════════════════════════════════════════════════════════

void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.println("[WM] Entered config mode");
  lcd.clear();
  lcd.print("Connect to Wi-Fi");
  lcd.setCursor(0, 1);
  lcd.print("AP: Meteo-Setup");
}

// ═════════════════════════════════════════════════════════════════════════
//  TIMERS
// ═════════════════════════════════════════════════════════════════════════

void tickClock() {
  if (millis() - lastClockUpdate < 1000) return;
  lastClockUpdate += 1000;
  displayClock();
}

void tickCitySwitch() {
  if (millis() - lastCitySwitch < SWITCH_MS) return;
  currentCity    = 1 - currentCity;
  lastCitySwitch = millis();
  clearWeatherRow();
  displayWeather(currentCity);
}

void tickWeather() {
  if (millis() - lastWeatherUpdate < WEATHER_MS) return;
  lastWeatherUpdate = millis();
  fetchAllWeather();
  logWeather();
  displayWeather(currentCity);
  lastClockUpdate = millis();  
}

// ═════════════════════════════════════════════════════════════════════════
//  SETUP / LOOP
// ═════════════════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  initLcd();

  lcd.print("WiFi Config...");
  WiFiManager wm;
  wm.setAPCallback(configModeCallback);

  if (!wm.autoConnect("Meteo-Setup")) {
    lcd.clear(); lcd.print("Setup Failed");
    delay(3000); ESP.restart();
  }
  
  lcd.clear(); 
  lcd.print("Connected!");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP()); 
  delay(4000);

  configTime("EET-2EEST,M3.5.0/3,M10.5.0/4", "pool.ntp.org", "time.nist.gov");
  
  waitNtpSync();

  initFilesystem();
  initWebServer();

  lcd.clear(); lcd.print("Weather...");
  fetchAllWeather();
  logWeather();
  displayWeather(currentCity);

  lastWeatherUpdate = millis();
  lastCitySwitch    = millis();
  lastClockUpdate   = millis();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    lcd.clear(); lcd.print("WiFi Lost");
    delay(5000); return;
  }

  tickClock();
  tickCitySwitch();
  tickWeather();
  server.handleClient();
}