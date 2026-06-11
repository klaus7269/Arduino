#include <WiFi.h>
#include <HTTPClient.h>
#include <TJpg_Decoder.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <PubSubClient.h>
#include "time.h"
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <math.h>

// --- DISPLAY & TOUCH SETUP ---
TFT_eSPI tft = TFT_eSPI();

#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33

SPIClass tsSPI(VSPI); 
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

// --- CONFIGURATION ---
const char* ssid = "vodafone09FEBA";
const char* password = "NT4qgkds2t4zHx2K";
String targetPostcode = "S181RF"; 

// --- OPENSKY AUTHENTICATION ---
const char* openskyClientId = "nickcooper123@gmail.com-api-client";
const char* openskyClientSecret = "pkOACvzO80y1tEkCnkdOqK4z7mUEaEW1";

String accessToken = "";
unsigned long tokenExpiry = 0; 
int remainingCredits = 0; 

// --- GLOBAL VARIABLES ---
float myLat = 0.0;
float myLon = 0.0;
float lamin, lamax, lomin, lomax;

float currentZoom = 0.3; 
const float MIN_ZOOM = 0.8;
const float MAX_ZOOM = 0.1;

#define MAX_FLIGHTS 10 
#define TRAIL_LENGTH 6 

// --- MAP LOCATIONS (Max 4 Characters) ---
struct Town {
  String shortName;
  float lat;
  float lon;
};

Town regionalTowns[] = {
  {"SHEF", 53.3811, -1.4701}, {"ROTH", 53.4300, -1.3570}, 
  {"CHES", 53.2350, -1.4280}, {"ECKI", 53.3080, -1.3580},
  {"STAV", 53.2680, -1.3510}, {"DINN", 53.3742, -1.2036},  
  {"WORK", 53.3031, -1.1243}, {"MANS", 53.1435, -1.1963},
  {"BOLS", 53.2285, -1.2882}, {"BARN", 53.5526, -1.4820},
  {"DONN", 53.5228, -1.1284}, {"STCK", 53.4827, -1.5912},
  {"HATH", 53.3298, -1.6568}, {"BAKE", 53.2138, -1.6741},
  {"HOPE", 53.3486, -1.7456}, {"BUXT", 53.2587, -1.9135},
  {"CHPL", 53.3220, -1.9170}, {"GLOS", 53.4440, -1.9490},
  {"CLAY", 53.1664, -1.4137}, {"DARL", 53.1612, -1.5997}, 
  {"MATL", 53.1378, -1.5540}, {"ALFR", 53.0970, -1.3850}
};
const int numTowns = 22; 

// --- FLIGHT DATA CONTAINER ---
struct FlightInfo {
  bool active; 
  long timestamp;
  String icao;
  String callsign;
  String country;
  float lat, lon;      
  float alt_ft;
  float speed_mph; 
  float heading;
  float vert_rate_fpm;
  String squawk;
  int category;
  
  int screenX, screenY; 
  float trailLat[TRAIL_LENGTH];
  float trailLon[TRAIL_LENGTH];
  int trailCount; 
};

FlightInfo flights[MAX_FLIGHTS];
int totalFlights = 0;

// --- STATE MACHINE & SELECTION ---
String selectedIcao = ""; // Tracks the currently locked target

enum NorthMode { NORTH_UP, CUSTOM_NORTH_WAITING, CUSTOM_NORTH_SET };
NorthMode currentNorthMode = NORTH_UP;
float northOffsetAngle = 0.0; 

unsigned long lastApiCheck = 0;
const unsigned long API_INTERVAL = 30000; 

// ==========================================
//                 SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  
  tft.init();
  tft.setRotation(1); 
  tft.fillScreen(TFT_BLACK);
  
  tsSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(tsSPI);
  ts.setRotation(1);

  setenv("TZ", "GMT0BST,M3.5.0/1,M10.5.0", 1);
  tzset();
  
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.print("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(10, 10);
  tft.println("WiFi Connected!");

  if (getCoordinatesFromPostcode(targetPostcode)) {
    applyZoom(); 
    for (int i = 0; i < MAX_FLIGHTS; i++) {
      flights[i].active = false;
    }
    
    tft.println("Authenticating with OpenSky...");
    refreshOpenSkyToken();
    
    delay(1000);
  } else {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.println("Lookup Failed.");
    while(true); 
  }
}

// ==========================================
//                 MAIN LOOP
// ==========================================
void loop() {
  
  // --- TOUCH SCREEN HANDLING ---
  if (ts.tirqTouched() && ts.touched()) {
    TS_Point p = ts.getPoint();
    int touchX = map(p.x, 300, 3800, 0, 320);
    int touchY = map(p.y, 200, 3800, 0, 240);

    // 1. Zoom [+] Button
    if (touchX > 280 && touchX < 315 && touchY > 5 && touchY < 35) {
      if (currentZoom > MAZ_ZOOM) {
        currentZoom += 0.1;
        applyZoom(); drawRadarMap(); delay(250); return; 
      }
    }
    // 2. Zoom [-] Button
    if (touchX > 240 && touchX < 275 && touchY > 5 && touchY < 35) {
      if (currentZoom < MIN_ZOOM) {
        currentZoom -= 0.1;
        applyZoom(); drawRadarMap(); delay(250); return;
      }
    }

    // 3. Custom Compass Toggle
    if (touchX > 200 && touchX < 235 && touchY > 5 && touchY < 35) {
      if (currentNorthMode == NORTH_UP) currentNorthMode = CUSTOM_NORTH_WAITING;
      else {
        currentNorthMode = NORTH_UP;
        northOffsetAngle = 0.0;
        applyZoom(); 
      }
      drawRadarMap(); delay(300); return;
    }

    // 4. Set Custom North Target
    if (currentNorthMode == CUSTOM_NORTH_WAITING) {
      float dy = touchY - 120.0; 
      float dx = touchX - 160.0; 
      northOffsetAngle = atan2(dy, dx) + 1.570796; 
      
      currentNorthMode = CUSTOM_NORTH_SET;
      applyZoom(); 
      drawRadarMap();
      delay(300); return;
    }

    // 5. Plane Selection (HUD Lock)
    bool planeTouched = false;
    for (int i = 0; i < MAX_FLIGHTS; i++) {
      if (!flights[i].active) continue; 
      if (abs(touchX - flights[i].screenX) < 20 && abs(touchY - flights[i].screenY) < 20) {
        selectedIcao = flights[i].icao; // Lock target
        planeTouched = true;
        drawRadarMap(); 
        delay(250); 
        break; 
      }
    }

    // 6. Deselect (Tap empty airspace to clear HUD)
    if (!planeTouched && touchY > 40) {
      if (selectedIcao != "") {
        selectedIcao = "";
        drawRadarMap();
        delay(250);
      }
    }
  }

  // --- API POLLING ---
  if (millis() - lastApiCheck >= API_INTERVAL || lastApiCheck == 0) {
    lastApiCheck = millis();
    fetchLocalFlights();
    drawRadarMap(); 
  }
}

// ==========================================
//               MATH HELPERS
// ==========================================
float mapFloat(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void calculateScreenCoords(float lat, float lon, int &sx, int &sy) {
  float cx = mapFloat(lon, lomin, lomax, -160.0, 160.0);
  float cy = mapFloat(lat, lamin, lamax, 120.0, -120.0); 

  if (currentNorthMode == NORTH_UP) {
    sx = (int)(cx + 160.0);
    sy = (int)(cy + 120.0);
  } else {
    float rx = cx * cos(northOffsetAngle) - cy * sin(northOffsetAngle);
    float ry = cx * sin(northOffsetAngle) + cy * cos(northOffsetAngle);
    sx = (int)(rx + 160.0);
    sy = (int)(ry + 120.0);
  }
}

void applyZoom() {
  lamin = myLat - currentZoom; lamax = myLat + currentZoom;
  lomin = myLon - currentZoom; lomax = myLon + currentZoom;

  for (int i = 0; i < MAX_FLIGHTS; i++) {
    if (flights[i].active) {
      calculateScreenCoords(flights[i].lat, flights[i].lon, flights[i].screenX, flights[i].screenY);
    }
  }
}

// --- NEW: Draws true aerodynamic triangles ---
void drawAircraftShape(int x, int y, float heading, uint16_t color) {
  float rad = (heading * 3.14159 / 180.0) + northOffsetAngle;
  
  // Nose
  int x1 = x + sin(rad) * 8;
  int y1 = y - cos(rad) * 8;
  
  // Bottom Right
  float radBR = rad + 2.356; // + 135 degrees
  int x2 = x + sin(radBR) * 6;
  int y2 = y - cos(radBR) * 6;
  
  // Bottom Left
  float radBL = rad - 2.356; // - 135 degrees
  int x3 = x + sin(radBL) * 6;
  int y3 = y - cos(radBL) * 6;
  
  tft.fillTriangle(x1, y1, x2, y2, x3, y3, color);
}

String getCompassDirection(float degrees) {
  const char* dirs[] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", 
                        "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
  int index = (int)((degrees + 11.25) / 22.5) % 16;
  return String(dirs[index]);
}

void drawRotatedLabel(const char* lbl, int radius, float angleOffNorth, uint16_t color) {
  float totalAngle = angleOffNorth + northOffsetAngle - 1.570796; 
  int x = 160 + cos(totalAngle) * radius;
  int y = 120 + sin(totalAngle) * radius;
  tft.setTextColor(color); tft.setTextSize(1);
  tft.setCursor(x - 3, y - 4); 
  tft.print(lbl);
}

// ==========================================
//               NETWORK APIs
// ==========================================
bool refreshOpenSkyToken() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  
  http.begin(client, "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  
  String payload = "grant_type=client_credentials&client_id=" + String(openskyClientId) + "&client_secret=" + String(openskyClientSecret);
  int httpCode = http.POST(payload);
  
  if (httpCode == HTTP_CODE_OK) {
    JsonDocument doc;
    deserializeJson(doc, http.getStream());
    accessToken = doc["access_token"].as<String>();
    long expiresIn = doc["expires_in"].as<long>(); 
    tokenExpiry = millis() + ((expiresIn - 60) * 1000); 
    http.end(); return true;
  }
  http.end(); return false;
}

bool getCoordinatesFromPostcode(String postcode) {
  WiFiClientSecure client;
  client.setInsecure(); 
  HTTPClient http;
  String url = "https://api.postcodes.io/postcodes/" + postcode;
  http.begin(client, url);
  if (http.GET() == HTTP_CODE_OK) {
    JsonDocument doc; deserializeJson(doc, http.getStream());
    myLon = doc["result"]["longitude"]; myLat = doc["result"]["latitude"];
    http.end(); return true;
  }
  http.end(); return false;
}

void fetchLocalFlights() {
  if (accessToken == "" || millis() > tokenExpiry) refreshOpenSkyToken();

  tft.fillRect(0, 0, 150, 20, TFT_BLACK);
  tft.setCursor(5, 5); tft.setTextSize(1); tft.setTextColor(TFT_YELLOW);
  tft.print("SCANNING...");

  WiFiClientSecure client; client.setInsecure(); HTTPClient http; http.useHTTP10(true); 

  String url = "https://opensky-network.org/api/states/all?lamin=" + String(lamin, 4) + 
               "&lomin=" + String(lomin, 4) + "&lamax=" + String(lamax, 4) + "&lomax=" + String(lomax, 4) + "&extended=1";             
  
  http.begin(client, url);
  http.setUserAgent("ESP32-FlightTracker/1.0");
  http.addHeader("Authorization", "Bearer " + accessToken);
  
  const char* headerKeys[] = {"X-Rate-Limit-Remaining"};
  http.collectHeaders(headerKeys, 1);
  int httpCode = http.GET();
  
  if (http.hasHeader("X-Rate-Limit-Remaining")) remainingCredits = http.header("X-Rate-Limit-Remaining").toInt();

  if (httpCode == HTTP_CODE_OK) {
    JsonDocument filter; filter["time"] = true; filter["states"][0] = true; 
    JsonDocument doc; DeserializationError error = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    if (error) { http.end(); return; }

    bool updatedThisCycle[MAX_FLIGHTS] = {false};
    totalFlights = 0;

    if (!doc["states"].isNull()) {
      JsonArray states = doc["states"];
      for (int i = 0; i < states.size(); i++) {
        JsonArray state = states[i];
        String currentIcao = state[0].as<String>();

        int targetIndex = -1;
        for(int j = 0; j < MAX_FLIGHTS; j++) {
            if(flights[j].active && flights[j].icao == currentIcao) { targetIndex = j; break; }
        }
        if (targetIndex == -1) {
            for(int j = 0; j < MAX_FLIGHTS; j++) {
                if(!flights[j].active) { targetIndex = j; break; }
            }
        }
        if (targetIndex == -1) continue; 

        if (flights[targetIndex].active) {
            for (int t = TRAIL_LENGTH - 1; t > 0; t--) {
                flights[targetIndex].trailLat[t] = flights[targetIndex].trailLat[t-1];
                flights[targetIndex].trailLon[t] = flights[targetIndex].trailLon[t-1];
            }
            flights[targetIndex].trailLat[0] = flights[targetIndex].lat;
            flights[targetIndex].trailLon[0] = flights[targetIndex].lon;
            if(flights[targetIndex].trailCount < TRAIL_LENGTH) { flights[targetIndex].trailCount++; }
        } else {
            flights[targetIndex].trailCount = 0;
        }

        flights[targetIndex].icao = currentIcao;
        flights[targetIndex].timestamp = doc["time"].as<long>();
        
        flights[targetIndex].callsign = state[1].as<String>();
        flights[targetIndex].callsign.trim();
        if(flights[targetIndex].callsign.length() == 0) flights[targetIndex].callsign = "UKN";

        flights[targetIndex].lon = state[5].as<float>();
        flights[targetIndex].lat = state[6].as<float>();
        calculateScreenCoords(flights[targetIndex].lat, flights[targetIndex].lon, flights[targetIndex].screenX, flights[targetIndex].screenY);

        flights[targetIndex].alt_ft = state[7].as<float>() * 3.28084;
        flights[targetIndex].speed_mph = state[9].as<float>() * 2.23694; 
        flights[targetIndex].heading = state[10].as<float>();            
        flights[targetIndex].vert_rate_fpm = state[11].as<float>() * 196.85; 
        flights[targetIndex].squawk = state[14].as<String>();

        flights[targetIndex].active = true;
        updatedThisCycle[targetIndex] = true;
        totalFlights++;
      }
    }

    for(int i = 0; i < MAX_FLIGHTS; i++) {
        if (!updatedThisCycle[i]) flights[i].active = false;
    }
  } 
  else if (httpCode == 429) {
    tft.fillRect(0, 0, 150, 20, TFT_BLACK); 
    tft.setCursor(5, 5); tft.setTextColor(TFT_RED); tft.print("RATE LIMITED");
  } 
  else {
    tft.fillRect(0, 0, 150, 20, TFT_BLACK);
    tft.setCursor(5, 5); tft.setTextColor(TFT_ORANGE); tft.printf("HTTP ERROR: %d", httpCode);
  }
  
  http.end();
}

// ==========================================
//               UI DRAWING
// ==========================================

void drawRadarMap() {
  tft.fillScreen(TFT_BLACK);

  // RADAR RINGS
  tft.drawCircle(160, 120, 40, TFT_DARKGREEN);
  tft.drawCircle(160, 120, 80, TFT_DARKGREEN);
  tft.drawCircle(160, 120, 120, TFT_DARKGREEN);
  
  float totalSpanNm = (currentZoom * 2.0) * 60.0; 
  float milesPerPixel = totalSpanNm / 240.0;
  
  tft.setTextColor(TFT_DARKGREEN); tft.setTextSize(1);
  tft.setCursor(162, 82); tft.printf("%.0fnm", milesPerPixel * 40);
  tft.setCursor(162, 42); tft.printf("%.0fnm", milesPerPixel * 80);

  // ROTATING COMPASS ROSE
  drawRotatedLabel("N", 115, 0.0,      (currentNorthMode != NORTH_UP) ? TFT_RED : TFT_DARKGREEN);
  drawRotatedLabel("E", 115, 1.570796, TFT_DARKGREEN); 
  drawRotatedLabel("S", 115, 3.14159,  TFT_DARKGREEN); 
  drawRotatedLabel("W", 115, 4.71238,  TFT_DARKGREEN); 

  // UI BUTTONS
  tft.fillRect(280, 5, 30, 25, TFT_DARKGREY); 
  tft.setTextColor(TFT_WHITE); tft.setTextSize(2); tft.setCursor(289, 10); tft.print("+");
  
  tft.fillRect(245, 5, 30, 25, TFT_DARKGREY); 
  tft.setCursor(254, 10); tft.print("-");

  tft.fillRect(200, 5, 35, 25, TFT_DARKGREY);
  if (currentNorthMode == CUSTOM_NORTH_WAITING) tft.drawRect(200, 5, 35, 25, TFT_YELLOW); 
  
  tft.drawCircle(217, 17, 8, TFT_WHITE);
  int nx = 217 + sin(northOffsetAngle) * 8;
  int ny = 17 - cos(northOffsetAngle) * 8;
  tft.drawLine(217, 17, nx, ny, (currentNorthMode == CUSTOM_NORTH_SET) ? TFT_RED : TFT_WHITE);

  tft.setTextSize(1); tft.setTextColor(TFT_LIGHTGREY); tft.setCursor(5, 225); 
  tft.printf("CR: %d", remainingCredits);

  // REGIONAL TOWNS
  for(int i = 0; i < numTowns; i++) {
     int tx, ty;
     calculateScreenCoords(regionalTowns[i].lat, regionalTowns[i].lon, tx, ty);
     if (tx >= 0 && tx <= 320 && ty >= 0 && ty <= 240) {
         tft.fillRect(tx, ty, 2, 2, TFT_CYAN); 
         tft.setTextColor(TFT_DARKGREY); tft.setCursor(tx + 4, ty - 4); 
         tft.print(regionalTowns[i].shortName);
     }
  }

  // CENTER POINT
  tft.fillCircle(160, 120, 3, TFT_RED);
  tft.setTextColor(TFT_RED); tft.setCursor(165, 125); tft.print("DRON");

  // PLANES
  int selectedIndex = -1; // Keep track of the HUD target

  for (int i = 0; i < MAX_FLIGHTS; i++) {
    if (!flights[i].active) continue;

    int px = flights[i].screenX;
    int py = flights[i].screenY;
    
    // --- COLOR CODING BY ALTITUDE ---
    uint16_t planeColor = TFT_WHITE; // > 25,000 ft (Cruising)
    if (flights[i].alt_ft < 10000) planeColor = TFT_GREEN;      // Landing / Low Alt
    else if (flights[i].alt_ft < 25000) planeColor = TFT_CYAN;  // Mid Alt Climbing/Descending

    // DRAW TRAILS
    for (int t = 0; t < flights[i].trailCount; t++) {
        int tx, ty; calculateScreenCoords(flights[i].trailLat[t], flights[i].trailLon[t], tx, ty);
        if (t == 0) tft.drawLine(px, py, tx, ty, TFT_DARKGREY);
        else {
            int prevX, prevY; calculateScreenCoords(flights[i].trailLat[t-1], flights[i].trailLon[t-1], prevX, prevY);
            tft.drawLine(prevX, prevY, tx, ty, TFT_DARKGREY);
        }
    }

    // DRAW AIRCRAFT TRIANGLE
    drawAircraftShape(px, py, flights[i].heading, planeColor);

    // CHECK SELECTION LOCK
    if (flights[i].icao == selectedIcao) {
      selectedIndex = i; 
      tft.drawCircle(px, py, 14, TFT_YELLOW); // Draw locking ring
    }

    // STANDARD DATA BLOCK
    tft.setTextColor(planeColor);
    tft.setCursor(px + 8, py - 8);
    tft.print(flights[i].callsign);
    
    int alt_k = round(flights[i].alt_ft / 1000.0);
    tft.setTextColor(TFT_LIGHTGREY);
    tft.setCursor(px + 8, py + 2);
    tft.printf("%.0f %dk", flights[i].speed_mph, alt_k);
  }

  // --- DRAW ON-SCREEN HUD OVERLAY ---
  if (selectedIndex != -1) {
    FlightInfo f = flights[selectedIndex];
    
    // Draw Box in Bottom Right
    tft.fillRect(170, 145, 145, 90, TFT_BLACK); 
    tft.drawRect(170, 145, 145, 90, TFT_YELLOW); 

    tft.setTextColor(TFT_YELLOW); tft.setTextSize(2);
    tft.setCursor(175, 150); tft.print(f.callsign);

    tft.setTextSize(1);
    tft.setTextColor(TFT_LIGHTGREY);
    tft.setCursor(175, 170); tft.printf("SQWK: %s", f.squawk.c_str());

    tft.setTextColor(TFT_WHITE);
    tft.setCursor(175, 185); tft.printf("Alt:  %.0f FT", f.alt_ft);
    tft.setCursor(175, 198); tft.printf("Spd:  %.0f MPH", f.speed_mph);
    tft.setCursor(175, 211); tft.printf("Hdg:  %.0f (%s)", f.heading, getCompassDirection(f.heading).c_str());
    
    tft.setCursor(175, 224); tft.print("V/S:  ");
    if (f.vert_rate_fpm > 0) tft.setTextColor(TFT_GREEN);
    else if (f.vert_rate_fpm < 0) tft.setTextColor(TFT_MAGENTA);
    tft.printf("%.0f", f.vert_rate_fpm);
  }

  if (currentNorthMode == CUSTOM_NORTH_WAITING) {
    tft.fillRect(80, 110, 160, 20, TFT_BLACK);
    tft.setTextColor(TFT_YELLOW); tft.setCursor(105, 116);
    tft.print("TAP TO SET NORTH");
  }
}