#include <WiFi.h>
#include <HTTPClient.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include "time.h"
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <math.h>
#include <TJpg_Decoder.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <sqlite3.h>

// --- GLOBALS & CONFIG ---
SPIClass sdSPI(HSPI); 
sqlite3 *db;
TFT_eSPI tft = TFT_eSPI();

#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33

SPIClass tsSPI(VSPI); 
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

JsonDocument doc; 
volatile bool isDownloading = false; 
int totalDbRecords = 0; // Tracks total planes in DB

// --- CONFIGURATION VARIABLES ---
String configSSID = "vodafone09FEBA";
String configPass = "NT4qgkds2t4zHx2K";
String configAPI = "Ohlb80psGomshc361cxzdqrSDEqUp1s7gINjsnnu5yqiQhT37Cx";
String configOpenSkyID = "nickcooper123@gmail.com-api-client";
String configOpenSkySec = "pkOACvzO80y1tEkCnkdOqK4z7mUEaEW1";
String configPostcode = "S181RF";

// North Persistence
float configNorthOffset = 0.0;
int configNorthMode = 0;

String accessToken = "";
unsigned long tokenExpiry = 0; 
int remainingCredits = 0;
const unsigned long API_INTERVAL = 30000; 

// --- APP STATE MACHINE ---
enum AppMode { RADAR, MAIN_MENU, SUB_MENU, KBD, CONSOLE, DB_VIEWER };
AppMode currentMode = RADAR;
int currentMenuCategory = 0; 

String* activeKbdTarget = nullptr;
String currentInput = "";
bool kbdShift = false;

// --- CONSOLE BUFFER ---
#define MAX_LOGS 100
String appLogs[MAX_LOGS];
int logIndex = 0, logCount = 0;

void sysLog(String msg) {
  String timeStr = String(millis()/1000) + "s: ";
  appLogs[logIndex] = timeStr + msg;
  logIndex = (logIndex + 1) % MAX_LOGS;
  if (logCount < MAX_LOGS) logCount++;
  Serial.println(msg);
}

// --- RADAR VARIABLES ---
float myLat = 0.0, myLon = 0.0;
float lamin, lamax, lomin, lomax;
float currentZoom = 0.3; 
const float MIN_ZOOM = 0.8, MAX_ZOOM = 0.1;
#define MAX_FLIGHTS 10 
#define TRAIL_LENGTH 6 

struct Town { String shortName; float lat; float lon; };
Town regionalTowns[] = {
  {"SHEF", 53.3811, -1.4701}, {"ROTH", 53.4300, -1.3570}, {"CHES", 53.2350, -1.4280}, 
  {"ECKI", 53.3080, -1.3580}, {"STAV", 53.2680, -1.3510}, {"DINN", 53.3742, -1.2036},  
  {"WORK", 53.3031, -1.1243}, {"MANS", 53.1435, -1.1963}, {"BOLS", 53.2285, -1.2882}, 
  {"BARN", 53.5526, -1.4820}, {"DONN", 53.5228, -1.1284}, {"STCK", 53.4827, -1.5912},
  {"HATH", 53.3298, -1.6568}, {"BAKE", 53.2138, -1.6741}, {"HOPE", 53.3486, -1.7456}, 
  {"BUXT", 53.2587, -1.9135}, {"CHPL", 53.3220, -1.9170}, {"DRON", 53.3032, -1.4726},
  {"CLAY", 53.1664, -1.4137}, {"DARL", 53.1612, -1.5997}, {"MATL", 53.1378, -1.5540}
};
const int numTowns = 21; 

struct FlightInfo {
  bool active, needsEnrichment; 
  long timestamp;
  String icao, callsign, registration, aircraftType, manufacturer_and_model, owner_operator, photo_url, squawk;
  bool is_private_operator;
  int year_built, screenX, screenY, trailCount;
  float lat, lon, alt_ft, speed_mph, heading, vert_rate_fpm;
  float trailLat[TRAIL_LENGTH], trailLon[TRAIL_LENGTH];
  
  int times_seen;
  long first_seen;
  long last_seen;
};

FlightInfo flights[MAX_FLIGHTS];
String selectedIcao = ""; 
void enrichAircraft(FlightInfo &f);

enum NorthMode { NORTH_UP, CUSTOM_NORTH_WAITING, CUSTOM_NORTH_SET };
NorthMode currentNorthMode = NORTH_UP;
float northOffsetAngle = 0.0; 
unsigned long lastApiCheck = 0;

// ==========================================
//               SETUP & CONFIG
// ==========================================
void loadConfig() {
  if (SD.exists("/config.json")) {
    File f = SD.open("/config.json", FILE_READ);
    JsonDocument conf; deserializeJson(conf, f);
    configSSID = conf["ssid"] | "vodafone09FEBA";
    configPass = conf["pass"] | "NT4qgkds2t4zHx2K";
    configAPI = conf["api"] | "xOhlb80psGomshc361cxzdqrSDEqUp1s7gINjsnnu5yqiQhT37C";
    configOpenSkyID = conf["os_id"] | "nickcooper123@gmail.com-api-client";
    configOpenSkySec = conf["os_sec"] | "pkOACvzO80y1tEkCnkdOqK4z7mUEaEW1";
    configPostcode = conf["postcode"] | "S181RF";
    configNorthOffset = conf["n_off"] | 0.0;
    configNorthMode = conf["n_mod"] | 0;
    f.close(); sysLog("Config loaded.");
  }
}

void saveConfig() {
  File f = SD.open("/config.json", FILE_WRITE);
  JsonDocument conf;
  conf["ssid"] = configSSID; conf["pass"] = configPass; conf["api"] = configAPI;
  conf["os_id"] = configOpenSkyID; conf["os_sec"] = configOpenSkySec; conf["postcode"] = configPostcode;
  conf["n_off"] = configNorthOffset; conf["n_mod"] = configNorthMode;
  serializeJson(conf, f); f.close(); sysLog("Config saved.");
}

void setup() {
  Serial.begin(115200);
  tft.init(); tft.setRotation(1); tft.fillScreen(TFT_BLACK);
  tsSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(tsSPI); ts.setRotation(1);
  setenv("TZ", "GMT0BST,M3.5.0/1,M10.5.0", 1); tzset();
  
  tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.setTextSize(2); tft.setCursor(10, 10);
  tft.println("Mounting SD...");
  sdSPI.begin(18, 19, 23, 5); 

  if (!SD.begin(5, sdSPI, 4000000)) {
    tft.setTextColor(TFT_RED); tft.println("SD Hardware FAILED."); sysLog("SD FAILED.");
  } else {
    SD.mkdir("/photos"); loadConfig(); 
    
    // Apply saved North orientation
    northOffsetAngle = configNorthOffset;
    currentNorthMode = (NorthMode)configNorthMode;

    sqlite3_initialize();
    if (sqlite3_open("/sd/aircraft.db", &db) == SQLITE_OK) {
        char *err = 0;
        // Create table with all columns at once to prevent migration errors
        const char* sql = "CREATE TABLE IF NOT EXISTS aircraft_v2 ("
                          "icao TEXT PRIMARY KEY, "
                          "reg TEXT, "
                          "type TEXT, "
                          "manufacturer TEXT, "
                          "operator TEXT, "
                          "is_private INTEGER, "
                          "year_built INTEGER, "
                          "photo_url TEXT, "
                          "times_seen INTEGER DEFAULT 0, "
                          "first_seen INTEGER DEFAULT 0, "
                          "last_seen INTEGER DEFAULT 0);";
        
        if (sqlite3_exec(db, sql, NULL, 0, &err) != SQLITE_OK) {
            Serial.printf("SQL Create Error: %s\n", err);
            sqlite3_free(err);
        }
        
        sqlite3_stmt *res;
        if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM aircraft_v2;", -1, &res, NULL) == SQLITE_OK) {
            if (sqlite3_step(res) == SQLITE_ROW) totalDbRecords = sqlite3_column_int(res, 0);
        }
        sqlite3_finalize(res);
        tft.setTextColor(TFT_GREEN); tft.printf("DB Ready. Records: %d\n", totalDbRecords);
    }
  }  

  tft.setTextColor(TFT_WHITE); tft.println("Connecting WiFi...");
  WiFi.begin(configSSID.c_str(), configPass.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) { delay(500); attempts++; }
  
  if (WiFi.status() == WL_CONNECTED) {
    if (getCoordinatesFromPostcode(configPostcode)) {
      applyZoom(); for (int i=0; i<MAX_FLIGHTS; i++) flights[i].active = false;
      refreshOpenSkyToken(); syncMissingPhotos();
    }
  } else {
    tft.setTextColor(TFT_RED); tft.println("WiFi Failed. Tap for Menu.");
    while(!ts.touched()) delay(100); 
    currentMode = MAIN_MENU; drawMenu();
  }
}

// ==========================================
//               UI ENGINES
// ==========================================
char kbdLayout[2][4][10] = {
  { {'1','2','3','4','5','6','7','8','9','0'}, {'q','w','e','r','t','y','u','i','o','p'},
    {'a','s','d','f','g','h','j','k','l','-'}, {'z','x','c','v','b','n','m','<','^','*'} },
  { {'!','@','#','$','%','^','&','*','(',')'}, {'Q','W','E','R','T','Y','U','I','O','P'},
    {'A','S','D','F','G','H','J','K','L','_'}, {'Z','X','C','V','B','N','M','<','^','*'} }
};

void drawKeyboard() {
  tft.fillScreen(TFT_BLACK); tft.fillRect(5, 5, 310, 40, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE); tft.setTextSize(2); tft.setCursor(15, 17); tft.print(currentInput);

  for(int r=0; r<4; r++) {
    for(int c=0; c<10; c++) {
      int x = c*32; int y = 60+(r*45);
      tft.drawRect(x, y, 32, 45, TFT_WHITE); char key = kbdLayout[kbdShift?1:0][r][c];
      tft.setCursor(x+8, y+15);
      if (key=='<') { tft.setTextColor(TFT_RED); tft.print("BS"); }
      else if (key=='^') { tft.setTextColor(TFT_CYAN); tft.print("SH"); }
      else if (key=='*') { tft.setTextColor(TFT_GREEN); tft.print("OK"); }
      else { tft.setTextColor(TFT_WHITE); tft.print(key); }
    }
  }
}

void handleKeyboardTouch(int tx, int ty) {
  if (ty < 60) return; 
  int col = tx/32; int row = (ty-60)/45;
  if (col>9 || row>3) return;
  
  char key = kbdLayout[kbdShift?1:0][row][col];
  if (key == '<') { if (currentInput.length()>0) currentInput.remove(currentInput.length()-1); } 
  else if (key == '^') { kbdShift = !kbdShift; } 
  else if (key == '*') { 
    if (activeKbdTarget != nullptr) *activeKbdTarget = currentInput;
    saveConfig();
    if (activeKbdTarget == &configPostcode) {
        tft.fillScreen(TFT_BLACK); tft.setCursor(10,10); tft.print("Locating...");
        getCoordinatesFromPostcode(configPostcode); applyZoom();
    }
    currentMode = SUB_MENU; drawSubMenu(); return;
  } else { currentInput += key; }
  drawKeyboard(); 
}

void drawMenu() {
  tft.fillScreen(TFT_BLACK); tft.setTextSize(2);
  tft.setTextColor(TFT_YELLOW); tft.setCursor(20, 10); tft.print("--- MAIN MENU ---");
  
  const char* btns[] = {"Settings (Keys & WiFi)", "Location", "Screen Config", "Debug Tools", "<- Exit to Radar"};
  for(int i=0; i<5; i++) {
    int y = 40 + (i*36);
    tft.drawRect(20, y, 280, 32, TFT_CYAN);
    tft.setTextColor(TFT_WHITE); tft.setCursor(30, y+8); tft.print(btns[i]);
  }
}

void drawSubMenu() {
  tft.fillScreen(TFT_BLACK); tft.setTextSize(2);
  tft.setTextColor(TFT_YELLOW); tft.setCursor(20, 5); 
  
  int btnCount = 0; const char* btns[6];
  if (currentMenuCategory == 0) {
    tft.print("- SETTINGS -"); btnCount=6;
    btns[0]="WiFi SSID"; btns[1]="WiFi Password"; btns[2]="SkyLink API Key"; 
    btns[3]="OpenSky Client Id"; btns[4]="OpenSky Secret"; btns[5]="<- Back";
  } else if (currentMenuCategory == 1) {
    tft.print("- LOCATION -"); btnCount=4;
    btns[0]="Postcode"; btns[1]="Set North"; btns[2]="Reset North"; btns[3]="<- Back";
  } else if (currentMenuCategory == 2) {
    tft.print("- SCREEN -"); tft.printf(" (Z: %.1f)", currentZoom); btnCount=3;
    btns[0]="Zoom Out (-)"; btns[1]="Zoom In (+)"; btns[2]="<- Back";
  } else if (currentMenuCategory == 3) {
    tft.print("- DEBUG -"); btnCount=3;
    btns[0]="System Console"; btns[1]="Database Viewer"; btns[2]="<- Back";
  }

  for(int i=0; i<btnCount; i++) {
    int y = 35 + (i*32);
    tft.drawRect(20, y, 280, 28, TFT_CYAN);
    tft.setTextColor(TFT_WHITE); tft.setCursor(30, y+6); tft.print(btns[i]);
  }
}

void drawConsole() {
  tft.fillScreen(TFT_BLACK); tft.setTextSize(1); tft.setTextColor(TFT_YELLOW);
  tft.setCursor(5,5); tft.println("--- SYSTEM CONSOLE ---"); tft.setTextColor(TFT_GREEN);
  int printed = 0; int idx = logIndex - 1; if (idx < 0) idx = MAX_LOGS - 1;
  while(printed < 24 && logCount > printed) {
    if (appLogs[idx] != "") tft.println(appLogs[idx]);
    idx--; if (idx < 0) idx = MAX_LOGS - 1; printed++;
  }
  tft.fillRect(260, 0, 60, 25, TFT_RED); tft.setTextColor(TFT_WHITE); tft.setTextSize(2); tft.setCursor(265, 5); tft.print("EXIT");
}

void drawDBViewer() {
  tft.fillScreen(TFT_BLACK); tft.setTextSize(1); tft.setTextColor(TFT_CYAN);
  tft.setCursor(5,5); tft.println("--- VISITORS ---");

  if (db != nullptr) {
    sqlite3_stmt *res;
    // Querying with manufacturer
    if (sqlite3_prepare_v2(db, "SELECT icao, manufacturer, times_seen, first_seen, last_seen FROM aircraft_v2 ORDER BY times_seen DESC LIMIT 5;", -1, &res, NULL) == SQLITE_OK) {
      
      int y = 20;
      bool dataFound = false;
      char fTime[16], lTime[16];

      while (sqlite3_step(res) == SQLITE_ROW) {
        dataFound = true;
        String icao = String((const char*)sqlite3_column_text(res, 0));
        String mfr = String((const char*)sqlite3_column_text(res, 1));
        int seen = sqlite3_column_int(res, 2);
        long first = sqlite3_column_int(res, 3);
        long last = sqlite3_column_int(res, 4);

        formatTime(first, fTime);
        formatTime(last, lTime);

        tft.setTextColor(TFT_YELLOW); tft.setCursor(5, y);
        tft.printf("%s (Seen: %d)", icao.c_str(), seen);
        tft.setTextColor(TFT_WHITE); tft.setCursor(5, y+10);
        tft.printf("Mfr: %s", mfr.length() > 20 ? mfr.substring(0,20) : mfr.c_str());
        tft.setCursor(5, y+20);
        tft.printf("1st: %s", fTime);
        tft.setCursor(5, y+30);
        tft.printf("Lst: %s", lTime);
        
        tft.drawFastHLine(5, y+42, 310, TFT_DARKGREY);
        y += 48; // Space for next card
        if (y > 200) break; // Don't run off screen
      }
      
      if (!dataFound) {
        tft.setTextColor(TFT_ORANGE); tft.setCursor(5, 40); tft.println("No data yet.");
      }
      sqlite3_finalize(res);
    }
  }
  
  // EXIT Button
  tft.fillRect(260, 0, 60, 25, TFT_RED); 
  tft.setTextColor(TFT_WHITE); tft.setTextSize(2); tft.setCursor(265, 5); tft.print("EXIT");
}

// ==========================================
//               MAIN LOOP
// ==========================================
void loop() {
  if (ts.tirqTouched() && ts.touched()) {
    TS_Point p = ts.getPoint();
    int tx = map(p.x, 300, 3800, 0, 320); int ty = map(p.y, 200, 3800, 0, 240);

    if (currentMode == RADAR) {
      // Menu button
      if (tx < 40 && ty < 40) { currentMode = MAIN_MENU; drawMenu(); delay(300); return; }
      
      // Screen tap to configure custom north
      if (currentNorthMode == CUSTOM_NORTH_WAITING) {
        northOffsetAngle = atan2(ty - 120.0, tx - 160.0) + 1.570796; 
        currentNorthMode = CUSTOM_NORTH_SET; 
        configNorthOffset = northOffsetAngle; configNorthMode = 2; saveConfig(); // Persist setting
        applyZoom(); drawRadarMap(); delay(300); return;
      }
      
      // Tap Plane
      bool pTouched = false;
      for (int i=0; i<MAX_FLIGHTS; i++) {
        if (flights[i].active && abs(tx-flights[i].screenX)<20 && abs(ty-flights[i].screenY)<20) { selectedIcao=flights[i].icao; pTouched=true; drawRadarMap(); delay(250); break; }
      }
      // Deselect
      if (!pTouched && ty>40 && selectedIcao!="") { selectedIcao=""; drawRadarMap(); delay(250); }

    } else if (currentMode == MAIN_MENU) {
      if (ty < 40) return;
      int btn = (ty - 40) / 36;
      if (btn >= 0 && btn <= 3) { currentMenuCategory = btn; currentMode = SUB_MENU; drawSubMenu(); delay(300); }
      else if (btn == 4) { if(WiFi.status() != WL_CONNECTED) ESP.restart(); currentMode = RADAR; drawRadarMap(); delay(300); }
      
    } else if (currentMode == SUB_MENU) {
      if (ty < 35) return;
      int btn = (ty - 35) / 32;
      
      if (currentMenuCategory == 0) { // SETTINGS
        if(btn==0){ currentMode=KBD; activeKbdTarget=&configSSID; currentInput=configSSID; kbdShift=false; drawKeyboard(); delay(300); }
        else if(btn==1){ currentMode=KBD; activeKbdTarget=&configPass; currentInput=configPass; kbdShift=false; drawKeyboard(); delay(300); }
        else if(btn==2){ currentMode=KBD; activeKbdTarget=&configAPI; currentInput=configAPI; kbdShift=false; drawKeyboard(); delay(300); }
        else if(btn==3){ currentMode=KBD; activeKbdTarget=&configOpenSkyID; currentInput=configOpenSkyID; kbdShift=false; drawKeyboard(); delay(300); }
        else if(btn==4){ currentMode=KBD; activeKbdTarget=&configOpenSkySec; currentInput=configOpenSkySec; kbdShift=false; drawKeyboard(); delay(300); }
        else if(btn==5){ currentMode=MAIN_MENU; drawMenu(); delay(300); }
      } else if (currentMenuCategory == 1) { // LOCATION
        if(btn==0){ currentMode=KBD; activeKbdTarget=&configPostcode; currentInput=configPostcode; kbdShift=false; drawKeyboard(); delay(300); }
        else if(btn==1){ currentMode=RADAR; currentNorthMode=CUSTOM_NORTH_WAITING; drawRadarMap(); delay(300); }
        else if(btn==2){ currentNorthMode=NORTH_UP; northOffsetAngle=0.0; configNorthMode=0; configNorthOffset=0.0; saveConfig(); currentMode=RADAR; applyZoom(); drawRadarMap(); delay(300); } // Reset North
        else if(btn==3){ currentMode=MAIN_MENU; drawMenu(); delay(300); }
      } else if (currentMenuCategory == 2) { // SCREEN
        if(btn==0){ if(currentZoom>MIN_ZOOM) currentZoom-=0.1; applyZoom(); drawSubMenu(); delay(200); }
        else if(btn==1){ if(currentZoom<MAX_ZOOM) currentZoom+=0.1; applyZoom(); drawSubMenu(); delay(200); }
        else if(btn==2){ currentMode=MAIN_MENU; drawMenu(); delay(300); }
      } else if (currentMenuCategory == 3) { // DEBUG
        if(btn==0){ currentMode=CONSOLE; drawConsole(); delay(300); }
        else if(btn==1){ currentMode=DB_VIEWER; drawDBViewer(); delay(300); }
        else if(btn==2){ currentMode=MAIN_MENU; drawMenu(); delay(300); }
      }
      
    } else if (currentMode == KBD) { handleKeyboardTouch(tx, ty); delay(150); 
    } else if (currentMode == CONSOLE || currentMode == DB_VIEWER) {
      if (tx > 250 && ty < 40) { currentMode = SUB_MENU; drawSubMenu(); delay(300); } 
    }
  }

  if (currentMode == RADAR) {
    for (int i=0; i<MAX_FLIGHTS; i++) if (flights[i].active && flights[i].needsEnrichment) { enrichAircraft(flights[i]); if (selectedIcao==flights[i].icao) drawRadarMap(); break; }
    if (millis() - lastApiCheck >= API_INTERVAL || lastApiCheck == 0) { lastApiCheck=millis(); fetchLocalFlights(); drawRadarMap(); }
  }
}

// ==========================================
//          DATABASE, API & RENDERING
// ==========================================
void enrichAircraft(FlightInfo &f) {
    long now = time(nullptr);
    sysLog("Processing: " + f.icao);

    // 1. Check if we already have this plane in the DB (Update existing stats)
    if (db != nullptr) {
        sqlite3_stmt *res = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT reg, times_seen, first_seen FROM aircraft_v2 WHERE icao = ?;", -1, &res, NULL) == SQLITE_OK) {
            sqlite3_bind_text(res, 1, f.icao.c_str(), -1, SQLITE_STATIC);
            if (sqlite3_step(res) == SQLITE_ROW) {
                f.times_seen = sqlite3_column_int(res, 1) + 1;
                f.first_seen = sqlite3_column_int(res, 2);
                sqlite3_finalize(res);

                sqlite3_stmt *upd;
                if (sqlite3_prepare_v2(db, "UPDATE aircraft_v2 SET times_seen = ?, last_seen = ? WHERE icao = ?;", -1, &upd, NULL) == SQLITE_OK) {
                    sqlite3_bind_int(upd, 1, f.times_seen);
                    sqlite3_bind_int(upd, 2, now);
                    sqlite3_bind_text(upd, 3, f.icao.c_str(), -1, SQLITE_STATIC);
                    sqlite3_step(upd); sqlite3_finalize(upd);
                }
                f.needsEnrichment = false; return;
            }
        }
        if (res != nullptr) sqlite3_finalize(res);
    }

    // 2. Default values (if API fails, these will be saved)
    f.registration = "UNKNOWN";
    f.aircraftType = "UNKNOWN";
    f.times_seen = 1;
    f.first_seen = now;
    f.last_seen = now;

    // 3. Attempt API Enrichment
    WiFiClientSecure client; client.setInsecure(); HTTPClient http;
    http.begin(client, "https://skylink-api.p.rapidapi.com/aircraft/icao24/" + f.icao + "?photos=true");
    http.addHeader("x-rapidapi-key", configAPI);
    
    if (http.GET() == HTTP_CODE_OK) {
        doc.clear(); deserializeJson(doc, http.getStream());
        JsonObject aircraft = doc["aircraft"];
        
        f.registration = aircraft["registration"] | "UKN";
        f.aircraftType = aircraft["type_name"] | "UKN";

        if (aircraft["photos"].is<JsonArray>() && aircraft["photos"].size() > 0) {
            f.photo_url = aircraft["photos"][0]["image"].as<String>();
            downloadPhoto(f.photo_url, f.icao);
        }
        sysLog("API Enriched: " + f.icao);
    } else {
        sysLog("API Fallback: Logging basic ICAO only");
    }
    http.end();

    // 4. Always insert into DB (even if API failed, record will be "UNKNOWN")
    if (db != nullptr) {
        sqlite3_stmt *ins;
        const char* insSql = "INSERT INTO aircraft_v2 (icao, reg, type, manufacturer, times_seen, first_seen, last_seen) VALUES (?,?,?,?,?,?,?);";
        if (sqlite3_prepare_v2(db, insSql, -1, &ins, NULL) == SQLITE_OK) {
            sqlite3_bind_text(ins, 1, f.icao.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(ins, 2, f.registration.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(ins, 3, f.aircraftType.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(ins, 4, f.manufacturer_and_model.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int(ins, 5, f.times_seen);
            sqlite3_bind_int(ins, 6, f.first_seen);
            sqlite3_bind_int(ins, 7, f.last_seen);
            sqlite3_step(ins); sqlite3_finalize(ins);
            totalDbRecords = getDbCount(); 
        }
    }
    f.needsEnrichment = false;
}

void downloadPhoto(String url, String icao) {
  char path[64]; snprintf(path, sizeof(path), "/photos/%s.jpg", icao.c_str());
  if (SD.exists(path)) return; 

  isDownloading = true; 
  WiFiClientSecure client; client.setInsecure(); HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.begin(client, url); http.setUserAgent("Mozilla/5.0 Windows NT 10.0");
  
  if (http.GET() == HTTP_CODE_OK) {
    File file = SD.open(path, FILE_WRITE);
    if (file) { http.writeToStream(&file); file.close(); sysLog("Photo DL: " + icao); }
  } http.end(); isDownloading = false; 
}

void syncMissingPhotos() {
  if (db == nullptr) return; sqlite3_stmt *res;
  if (sqlite3_prepare_v2(db, "SELECT icao, photo_url FROM aircraft_v2 WHERE photo_url IS NOT NULL AND photo_url != '';", -1, &res, NULL) == SQLITE_OK) {
    while (sqlite3_step(res) == SQLITE_ROW) {
      String icao = String((const char*)sqlite3_column_text(res, 0)); String url = String((const char*)sqlite3_column_text(res, 1));
      char path[64]; snprintf(path, sizeof(path), "/photos/%s.jpg", icao.c_str());
      if (!SD.exists(path)) downloadPhoto(url, icao);
    }
  } sqlite3_finalize(res); sysLog("Sync Complete");
}

bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
   tft.pushImage(x + 165, y + 130, w, h, bitmap); return true;
}

void drawRadarMap() {
  if (isDownloading) return; tft.fillScreen(TFT_BLACK);
  tft.fillRect(0, 0, 30, 30, TFT_DARKGREY); tft.setTextColor(TFT_WHITE); tft.setTextSize(2); tft.setCursor(7, 7); tft.print("M");

  tft.drawCircle(160, 120, 40, TFT_DARKGREEN); tft.drawCircle(160, 120, 80, TFT_DARKGREEN); tft.drawCircle(160, 120, 120, TFT_DARKGREEN);
  float milesPerPixel = ((currentZoom * 2.0) * 60.0) / 240.0;
  tft.setTextColor(TFT_DARKGREEN); tft.setTextSize(1);
  tft.setCursor(162, 82); tft.printf("%.0fnm", milesPerPixel * 40); tft.setCursor(162, 42); tft.printf("%.0fnm", milesPerPixel * 80);

  // Top Right Text Info


// Inside drawRadarMap()
  int activeCount = 0;
  for (int i=0; i<MAX_FLIGHTS; i++) if (flights[i].active) activeCount++;
  
  tft.setTextSize(1); tft.setTextColor(TFT_LIGHTGREY);
  tft.setCursor(230, 5); tft.printf("Vis: %d", activeCount);
  tft.setCursor(230, 15); tft.printf("DB : %d", getDbCount()); // Dynamically refresh
  tft.setCursor(5, 225); tft.printf("CR: %d", remainingCredits);

  // Draw N, S, E, W markers at the edge of the circle
  // We use the northOffsetAngle to ensure they always point relative to your orientation
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREEN);

  // Helper to draw text at a specific angle relative to the center (160, 120)
  auto drawMarker = [&](const char* label, float angleOffset, uint16_t color) {
    float angle = angleOffset + northOffsetAngle - 1.570796; // Adjust for vertical
    int x = 160 + cos(angle) * 115; // 115 is just inside the 120px radius circle
    int y = 120 + sin(angle) * 115;
    tft.setTextColor(color);
    tft.setCursor(x - 3, y - 4);
    tft.print(label);
  };

  // N is red if in custom mode to indicate rotation
  drawMarker("N", 0.0, (currentNorthMode != NORTH_UP) ? TFT_RED : TFT_DARKGREEN);
  drawMarker("E", 1.570796, TFT_DARKGREEN);
  drawMarker("S", 3.14159, TFT_DARKGREEN);
  drawMarker("W", 4.71238, TFT_DARKGREEN);

  for(int i=0; i<numTowns; i++) {
     int tx = mapFloat(regionalTowns[i].lon, lomin, lomax, -160, 160); int ty = mapFloat(regionalTowns[i].lat, lamin, lamax, 120, -120);
     if (currentNorthMode != NORTH_UP) { float rx = tx*cos(northOffsetAngle)-ty*sin(northOffsetAngle); float ry = tx*sin(northOffsetAngle)+ty*cos(northOffsetAngle); tx=rx; ty=ry; }
     tx+=160; ty+=120;
     if (tx>=0 && tx<=320 && ty>=0 && ty<=240) { tft.fillRect(tx, ty, 2, 2, TFT_CYAN); tft.setTextColor(TFT_DARKGREY); tft.setCursor(tx+4, ty-4); tft.print(regionalTowns[i].shortName); }
  }

  int selectedIndex = -1; 
  for (int i = 0; i < MAX_FLIGHTS; i++) {
    if (!flights[i].active) continue;
    int px = flights[i].screenX; int py = flights[i].screenY;
    uint16_t pC = (flights[i].alt_ft < 10000) ? TFT_GREEN : ((flights[i].alt_ft < 25000) ? TFT_CYAN : TFT_WHITE); 
    
    for (int t=0; t<flights[i].trailCount; t++) {
        int tx = mapFloat(flights[i].trailLon[t], lomin, lomax, -160, 160); int ty = mapFloat(flights[i].trailLat[t], lamin, lamax, 120, -120);
        if(currentNorthMode != NORTH_UP){
          float rx = tx*cos(northOffsetAngle)-ty*sin(northOffsetAngle); 
          float ry = tx*sin(northOffsetAngle)+ty*cos(northOffsetAngle); 
          tx=rx; 
          ty=ry;}
          tx+=160; 
          ty+=120;
        if(t==0) 
          tft.drawLine(px, py, tx, ty, TFT_DARKGREY);
        else {
           int pvx = mapFloat(flights[i].trailLon[t-1], lomin, lomax, -160, 160); 
           int pvy = mapFloat(flights[i].trailLat[t-1], lamin, lamax, 120, -120);
           if(currentNorthMode != NORTH_UP){
             float rx = pvx*cos(northOffsetAngle)-pvy*sin(northOffsetAngle); 
             float ry = pvx*sin(northOffsetAngle)+pvy*cos(northOffsetAngle); 
             pvx=rx; 
             pvy=ry;
           }
           tft.drawLine(pvx+160, pvy+120, tx, ty, TFT_DARKGREY);
        }
    }
    float rad = (flights[i].heading * 3.14159 / 180.0) + northOffsetAngle;
    tft.fillTriangle(px+sin(rad)*8, py-cos(rad)*8, px+sin(rad+2.35)*6, py-cos(rad+2.35)*6, px+sin(rad-2.35)*6, py-cos(rad-2.35)*6, pC);

    if (flights[i].icao == selectedIcao) { selectedIndex = i; tft.drawCircle(px, py, 14, TFT_YELLOW); }
    tft.setTextColor(pC); tft.setCursor(px + 8, py - 8); tft.print(flights[i].callsign);
    tft.setTextColor(TFT_LIGHTGREY); tft.setCursor(px + 8, py + 2); tft.printf("%.0f %dk", flights[i].speed_mph, round(flights[i].alt_ft/1000.0));
  }

  if (selectedIndex != -1) {
    FlightInfo f = flights[selectedIndex];
    tft.fillRect(160, 125, 155, 110, TFT_BLACK); tft.drawRect(160, 125, 155, 110, TFT_YELLOW); 
    char path[64]; snprintf(path, sizeof(path), "/photos/%s.jpg", f.icao.c_str());
    if (SD.exists(path)) { TJpgDec.setJpgScale(8); TJpgDec.setSwapBytes(true); TJpgDec.setCallback(tft_output); TJpgDec.drawSdJpg(85, 5, path); } 
    else { tft.fillRect(245, 130, 65, 50, TFT_DARKGREY); tft.setTextColor(TFT_WHITE); tft.setCursor(250, 150); tft.print("NO IMG"); }
    
    tft.setTextColor(TFT_YELLOW); tft.setTextSize(2); tft.setCursor(165, 130); tft.print(f.callsign);
    tft.setTextSize(1); tft.setTextColor(TFT_CYAN); tft.setCursor(165, 150); tft.printf("%s", f.registration.c_str());
    tft.setCursor(165, 160); tft.printf("%s", f.aircraftType.c_str());
    String op = f.owner_operator; if(op.length()>14) op=op.substring(0,14)+"..";
    tft.setTextColor(TFT_LIGHTGREY); tft.setCursor(165, 175); tft.printf("Op: %s", op.c_str());
    tft.setCursor(165, 185); if(f.year_built>1900) tft.printf("Built: %d", f.year_built); else tft.print("Built: UKN");
    tft.setTextColor(TFT_WHITE); tft.setCursor(165, 200); tft.printf("Alt: %.0f FT", f.alt_ft);
    tft.setCursor(165, 210); tft.printf("Spd: %.0f MPH", f.speed_mph);
    tft.setCursor(165, 220); tft.print("V/S: "); if (f.vert_rate_fpm>0) tft.setTextColor(TFT_GREEN); else if(f.vert_rate_fpm<0) tft.setTextColor(TFT_MAGENTA); tft.printf("%.0f", f.vert_rate_fpm);
  }
}

// --- MATH & API FETCHING ---
float mapFloat(float x, float in_min, float in_max, float out_min, float out_max) { return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min; }
void applyZoom() {
  lamin = myLat - currentZoom; lamax = myLat + currentZoom; lomin = myLon - currentZoom; lomax = myLon + currentZoom;
  for (int i = 0; i < MAX_FLIGHTS; i++) {
    if (flights[i].active) {
       float cx = mapFloat(flights[i].lon, lomin, lomax, -160.0, 160.0); float cy = mapFloat(flights[i].lat, lamin, lamax, 120.0, -120.0); 
       if (currentNorthMode == NORTH_UP) { flights[i].screenX = cx+160; flights[i].screenY = cy+120; } 
       else { flights[i].screenX = cx*cos(northOffsetAngle)-cy*sin(northOffsetAngle)+160; flights[i].screenY = cx*sin(northOffsetAngle)+cy*cos(northOffsetAngle)+120; }
    }
  }
}

bool refreshOpenSkyToken() {
  WiFiClientSecure client; client.setInsecure(); HTTPClient http;
  http.begin(client, "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  if (http.POST("grant_type=client_credentials&client_id=" + configOpenSkyID + "&client_secret=" + configOpenSkySec) == HTTP_CODE_OK) {
    doc.clear(); deserializeJson(doc, http.getStream());
    accessToken = doc["access_token"].as<String>();
    tokenExpiry = millis() + ((doc["expires_in"].as<long>() - 60) * 1000); 
    http.end(); sysLog("OpenSky Token Updated"); return true;
  }
  http.end(); sysLog("OpenSky Token FAILED"); return false;
}

bool getCoordinatesFromPostcode(String postcode) {
  WiFiClientSecure client; client.setInsecure(); HTTPClient http;
  http.begin(client, "https://api.postcodes.io/postcodes/" + postcode);
  if (http.GET() == HTTP_CODE_OK) {
    doc.clear(); deserializeJson(doc, http.getStream());
    myLon = doc["result"]["longitude"]; myLat = doc["result"]["latitude"]; http.end(); 
    sysLog("GPS Updated for " + postcode); return true;
  }
  http.end(); sysLog("GPS Lookup FAILED"); return false;
}

void fetchLocalFlights() {
  if (accessToken == "" || millis() > tokenExpiry) refreshOpenSkyToken();
  tft.fillRect(0, 220, 100, 20, TFT_BLACK); tft.setCursor(5, 225); tft.setTextColor(TFT_YELLOW); tft.print("SCANNING...");
  WiFiClientSecure client; client.setInsecure(); HTTPClient http; http.useHTTP10(true); 
  String url = "https://opensky-network.org/api/states/all?lamin=" + String(lamin, 4) + "&lomin=" + String(lomin, 4) + "&lamax=" + String(lamax, 4) + "&lomax=" + String(lomax, 4) + "&extended=1";             
  http.begin(client, url); http.setUserAgent("ESP32-FlightTracker/1.0"); http.addHeader("Authorization", "Bearer " + accessToken);
  const char* hKeys[] = {"X-Rate-Limit-Remaining"}; http.collectHeaders(hKeys, 1);
  int httpCode = http.GET(); if (http.hasHeader("X-Rate-Limit-Remaining")) remainingCredits = http.header("X-Rate-Limit-Remaining").toInt();
  if (httpCode == HTTP_CODE_OK) {
    JsonDocument filter; filter["time"] = true; filter["states"][0] = true; 
    doc.clear(); if (!deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter))) {
      bool updated[MAX_FLIGHTS] = {false};
      if (!doc["states"].isNull()) {
        JsonArray states = doc["states"];
        for (int i = 0; i < states.size(); i++) {
          String cIcao = states[i][0].as<String>(); int tIdx = -1;
          for(int j=0; j<MAX_FLIGHTS; j++) { if(flights[j].active && flights[j].icao == cIcao) { tIdx = j; break; } }
          if(tIdx == -1) { for(int j=0; j<MAX_FLIGHTS; j++) { if(!flights[j].active) { tIdx = j; break; } } }
          if(tIdx == -1) continue; 
          if(flights[tIdx].active) {
              for(int t=TRAIL_LENGTH-1; t>0; t--) { flights[tIdx].trailLat[t] = flights[tIdx].trailLat[t-1]; flights[tIdx].trailLon[t] = flights[tIdx].trailLon[t-1]; }
              flights[tIdx].trailLat[0] = flights[tIdx].lat; flights[tIdx].trailLon[0] = flights[tIdx].lon;
              if(flights[tIdx].trailCount < TRAIL_LENGTH) flights[tIdx].trailCount++;
          } else {
              flights[tIdx].trailCount=0; flights[tIdx].needsEnrichment=true; flights[tIdx].registration="FETCHING"; flights[tIdx].aircraftType="..."; flights[tIdx].owner_operator="..."; flights[tIdx].year_built=0;
          }
          flights[tIdx].icao = cIcao; 
          flights[tIdx].timestamp = doc["time"].as<long>();
          flights[tIdx].callsign = states[i][1].as<String>(); 
          flights[tIdx].callsign.trim(); 
          if(flights[tIdx].callsign=="") flights[tIdx].callsign="UKN";
          flights[tIdx].lon = states[i][5].as<float>(); flights[tIdx].lat = states[i][6].as<float>();
          flights[tIdx].alt_ft = states[i][7].as<float>()*3.28084; 
          flights[tIdx].speed_mph = states[i][9].as<float>()*2.23694; 
          flights[tIdx].heading = states[i][10].as<float>(); 
          flights[tIdx].vert_rate_fpm = states[i][11].as<float>()*196.85; 
          flights[tIdx].active = true; updated[tIdx] = true;
        }
      }
      for(int i=0; i<MAX_FLIGHTS; i++) if(!updated[i]) flights[i].active = false;
      applyZoom();
    }
  } else sysLog("HTTP ERR: " + String(httpCode));
  http.end();
}

int getDbCount() {
  if (db == nullptr) return 0;
  sqlite3_stmt *res;
  int count = 0;
  if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM aircraft_v2;", -1, &res, NULL) == SQLITE_OK) {
    if (sqlite3_step(res) == SQLITE_ROW) {
      count = sqlite3_column_int(res, 0);
    }
  }
  sqlite3_finalize(res);
  return count;
}

void formatTime(long epoch, char* buffer) {
    struct tm *ptm = gmtime((time_t *)&epoch);
    strftime(buffer, 15, "%d/%m/%y %H:%M", ptm);
}