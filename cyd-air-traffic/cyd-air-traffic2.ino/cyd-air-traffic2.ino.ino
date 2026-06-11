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
const float MIN_ZOOM = 0.1;
const float MAX_ZOOM = 0.8;

#define MAX_FLIGHTS 10 
#define TRAIL_LENGTH 6 

// --- MAP LOCATIONS (Max 4 Characters) ---
struct Town {
  String shortName;
  float lat;
  float lon;
};

Town regionalTowns[] = {
  {"SHEF", 53.3811, -1.4701}, {"ROTH",  53.4300, -1.3570}, 
  {"CHES", 53.2350, -1.4280}, {"ECKI", 53.3080, -1.3580},
  {"STAV", 53.2680, -1.3510}, {"DINN",  53.3742, -1.2036},  
  {"WORK", 53.3031, -1.1243}, {"MANS", 53.1435, -1.1963},
  {"BOLS", 53.2285, -1.2882}, {"BARN", 53.5526, -1.4820},
  {"DONN", 53.5228, -1.1284}, {"STCK",  53.4827, -1.5912},
  {"HATH", 53.3298, -1.6568}, {"BAKE",  53.2138, -1.6741},
  {"HOPE", 53.3486, -1.7456}, {"BUXT", 53.2587, -1.9135},
  {"CHPL", 53.3220, -1.9170}, {"GLOS", 53.4440, -1.9490},
  {"CLAY", 53.1664, -1.4137}, {"DARL",  53.1612, -1.5997}, 
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
  // Trail Arrays changed to Lat/Lon to allow dynamic map rotation
  float trailLat[TRAIL_LENGTH];
  float trailLon[TRAIL_LENGTH];
  int trailCount; 
};

FlightInfo flights[MAX_FLIGHTS];
int totalFlights = 0;

// --- STATE MACHINE & COMPASS ORIENTATION ---
enum ScreenState { VIEW_RADAR, VIEW_DETAILS };
ScreenState currentState = VIEW_RADAR;

enum NorthMode { NORTH_UP, CUSTOM_NORTH_WAITING, CUSTOM_NORTH_SET };
NorthMode currentNorthMode = NORTH_UP;
float northOffsetAngle = 0.0; // The angle (in radians) we rotate the map

unsigned long lastApiCheck = 0;
unsigned long detailTimer = 0;
const unsigned long API_INTERVAL = 30000; 
const unsigned long DETAIL_DURATION = 5000; 

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
  if (currentState == VIEW_DETAILS) {
    if (millis() - detailTimer >= DETAIL_DURATION) {
      currentState = VIEW_RADAR;
      drawRadarMap(); 
    }
  } 
  else if (currentState == VIEW_RADAR) {
    
    if (ts.tirqTouched() && ts.touched()) {
      TS_Point p = ts.getPoint();
      int touchX = map(p.x, 300, 3800, 0, 320);
      int touchY = map(p.y, 200, 3800, 0, 240);

      // 1. Check Zoom [+] Button
      if (touchX > 280 && touchX < 315 && touchY > 5 && touchY < 35) {
        if (currentZoom > MIN_ZOOM) {
          currentZoom -= 0.1;
          applyZoom(); drawRadarMap(); delay(250); return; 
        }
      }
      // 2. Check Zoom [-] Button
      if (touchX > 240 && touchX < 275 && touchY > 5 && touchY < 35) {
        if (currentZoom < MAX_ZOOM) {
          currentZoom += 0.1;
          applyZoom(); drawRadarMap(); delay(250); return;
        }
      }

      // 3. Check Custom Compass Toggle Button
      if (touchX > 200 && touchX < 235 && touchY > 5 && touchY < 35) {
        if (currentNorthMode == NORTH_UP) {
          currentNorthMode = CUSTOM_NORTH_WAITING;
        } else {
          // Reset to standard North
          currentNorthMode = NORTH_UP;
          northOffsetAngle = 0.0;
          applyZoom(); 
        }
        drawRadarMap(); delay(300); return;
      }

      // 4. Check if we are waiting for user to tap a new North
      if (currentNorthMode == CUSTOM_NORTH_WAITING) {
        float dy = touchY - 120.0; // Y offset from center
        float dx = touchX - 160.0; // X offset from center
        // atan2 gives angle from center. We add PI/2 because standard North is UP (-PI/2)
        northOffsetAngle = atan2(dy, dx) + 1.570796; 
        
        currentNorthMode = CUSTOM_NORTH_SET;
        applyZoom(); // Recalculate all coordinates with new rotation
        drawRadarMap();
        delay(300); return;
      }

      // 5. Normal Plane Touch (Details View)
      for (int i = 0; i < MAX_FLIGHTS; i++) {
        if (!flights[i].active) continue; 
        if (abs(touchX - flights[i].screenX) < 20 && abs(touchY - flights[i].screenY) < 20) {
          currentState = VIEW_DETAILS;
          detailTimer = millis(); 
          displayFlightData(flights[i]);
          break; 
        }
      }
      delay(200); 
    }

    if (millis() - lastApiCheck >= API_INTERVAL || lastApiCheck == 0) {
      lastApiCheck = millis();
      fetchLocalFlights();
      if (currentState == VIEW_RADAR) {
        drawRadarMap(); 
      }
    }
  }
}

// ==========================================
//               MATH HELPERS
// ==========================================
float mapFloat(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// Applies Rotation Matrix to GPS Coordinates
void calculateScreenCoords(float lat, float lon, int &sx, int &sy) {
  // Translate to flat plane relative to 0,0 center
  float cx = mapFloat(lon, lomin, lomax, -160.0, 160.0);
  float cy = mapFloat(lat, lamin, lamax, 120.0, -120.0); // Y is inverted

  if (currentNorthMode == NORTH_UP) {
    sx = (int)(cx + 160.0);
    sy = (int)(cy + 120.0);
  } else {
    // Apply 2D Rotation Matrix
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

String getCompassDirection(float degrees) {
  const char* dirs[] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", 
                        "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
  int index = (int)((degrees + 11.25) / 22.5) % 16;
  return String(dirs[index]);
}

// Draws N/S/E/W dynamically relative to the rotation
void drawRotatedLabel(const char* lbl, int radius, float angleOffNorth, uint16_t color) {
  float totalAngle = angleOffNorth + northOffsetAngle - 1.570796; // -PI/2 is UP
  int x = 160 + cos(totalAngle) * radius;
  int y = 120 + sin(totalAngle) * radius;
  
  tft.setTextColor(color);
  tft.setTextSize(1);
  tft.setCursor(x - 3, y - 4); // Center offset
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
    
    http.end();
    return true;
  }
  
  Serial.printf("Auth Failed. HTTP Code: %d\n", httpCode);
  http.end();
  return false;
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
  if (accessToken == "" || millis() > tokenExpiry) {
    refreshOpenSkyToken();
  }

  tft.fillRect(0, 0, 150, 20, TFT_BLACK);
  tft.setCursor(5, 5);
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW);
  tft.print("SCANNING...");

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.useHTTP10(true); 

  String url = "https://opensky-network.org/api/states/all?lamin=" + String(lamin, 4) + 
               "&lomin=" + String(lomin, 4) + "&lamax=" + String(lamax, 4) + "&lomax=" + String(lomax, 4) + "&extended=1";             
  
  http.begin(client, url);
  http.setUserAgent("ESP32-FlightTracker/1.0");
  http.addHeader("Authorization", "Bearer " + accessToken);

  const char* headerKeys[] = {"X-Rate-Limit-Remaining"};
  http.collectHeaders(headerKeys, 1);

  int httpCode = http.GET();
  
  if (http.hasHeader("X-Rate-Limit-Remaining")) {
    remainingCredits = http.header("X-Rate-Limit-Remaining").toInt();
  }

  if (httpCode == HTTP_CODE_OK) {
    JsonDocument filter;
    filter["time"] = true;
    filter["states"][0] = true; 

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    
    if (error) {
      http.end(); return;
    }

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
    tft.setCursor(5, 5);
    tft.setTextColor(TFT_RED);
    tft.print("RATE LIMITED");
  } 
  else {
    tft.fillRect(0, 0, 150, 20, TFT_BLACK);
    tft.setCursor(5, 5);
    tft.setTextColor(TFT_ORANGE);
    tft.printf("HTTP ERROR: %d", httpCode);
  }
  
  http.end();
}

// ==========================================
//               UI DRAWING
// ==========================================

void drawRadarMap() {
  tft.fillScreen(TFT_BLACK);

  tft.drawCircle(160, 120, 40, TFT_DARKGREEN);
  tft.drawCircle(160, 120, 80, TFT_DARKGREEN);
  tft.drawCircle(160, 120, 120, TFT_DARKGREEN);
  
  float totalSpanNm = (currentZoom * 2.0) * 60.0; 
  float milesPerPixel = totalSpanNm / 240.0;
  
  tft.setTextColor(TFT_DARKGREEN);
  tft.setTextSize(1);
  tft.setCursor(162, 82); tft.printf("%.0fnm", milesPerPixel * 40);
  tft.setCursor(162, 42); tft.printf("%.0fnm", milesPerPixel * 80);

  // --- DRAW ROTATING COMPASS ROSE ---
  // Highlight 'N' in Red if custom rotation is active
  drawRotatedLabel("N", 115, 0.0,      (currentNorthMode != NORTH_UP) ? TFT_RED : TFT_DARKGREEN);
  drawRotatedLabel("E", 115, 1.570796, TFT_DARKGREEN); // PI/2
  drawRotatedLabel("S", 115, 3.14159,  TFT_DARKGREEN); // PI
  drawRotatedLabel("W", 115, 4.71238,  TFT_DARKGREEN); // 3*PI/2

  // --- UI BUTTONS ---
  tft.fillRect(280, 5, 30, 25, TFT_DARKGREY); 
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(289, 10); tft.print("+");
  
  tft.fillRect(245, 5, 30, 25, TFT_DARKGREY); 
  tft.setCursor(254, 10); tft.print("-");

  // COMPASS TOGGLE BUTTON
  tft.fillRect(200, 5, 35, 25, TFT_DARKGREY);
  if (currentNorthMode == CUSTOM_NORTH_WAITING) {
    tft.drawRect(200, 5, 35, 25, TFT_YELLOW); // Highlight border while waiting
  }
  // Draw Icon: Compass circle with needle
  tft.drawCircle(217, 17, 8, TFT_WHITE);
  int nx = 217 + sin(northOffsetAngle) * 8;
  int ny = 17 - cos(northOffsetAngle) * 8;
  tft.drawLine(217, 17, nx, ny, (currentNorthMode == CUSTOM_NORTH_SET) ? TFT_RED : TFT_WHITE);

  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY);
  tft.setCursor(5, 225); 
  tft.printf("CR: %d", remainingCredits);

  // REGIONAL TOWNS
  for(int i = 0; i < numTowns; i++) {
     int tx, ty;
     calculateScreenCoords(regionalTowns[i].lat, regionalTowns[i].lon, tx, ty);
     
     if (tx >= 0 && tx <= 320 && ty >= 0 && ty <= 240) {
         tft.fillRect(tx, ty, 2, 2, TFT_CYAN); 
         tft.setTextColor(TFT_DARKGREY);
         tft.setCursor(tx + 4, ty - 4); 
         tft.print(regionalTowns[i].shortName);
     }
  }

  // CENTER POINT
  tft.fillCircle(160, 120, 3, TFT_RED);
  tft.setTextColor(TFT_RED);
  tft.setCursor(165, 125);
  tft.print("DRON");

  // PLANES
  for (int i = 0; i < MAX_FLIGHTS; i++) {
    if (!flights[i].active) continue;

    int px = flights[i].screenX;
    int py = flights[i].screenY;
    
    // --- DRAW DYNAMIC ROTATED TRAILS ---
    for (int t = 0; t < flights[i].trailCount; t++) {
        int tx, ty;
        calculateScreenCoords(flights[i].trailLat[t], flights[i].trailLon[t], tx, ty);
        if (t == 0) {
            tft.drawLine(px, py, tx, ty, TFT_DARKGREY);
        } else {
            int prevX, prevY;
            calculateScreenCoords(flights[i].trailLat[t-1], flights[i].trailLon[t-1], prevX, prevY);
            tft.drawLine(prevX, prevY, tx, ty, TFT_DARKGREY);
        }
    }

    tft.fillRect(px - 2, py - 2, 4, 4, TFT_WHITE);
    
    // Apply map rotation offset to the aircraft's heading
    float rad = (flights[i].heading * 3.14159 / 180.0) + northOffsetAngle;
    int vectorX = px + (sin(rad) * 15); 
    int vectorY = py - (cos(rad) * 15); 
    tft.drawLine(px, py, vectorX, vectorY, TFT_WHITE);

    tft.setTextColor(TFT_WHITE);
    tft.setCursor(px + 6, py - 6);
    tft.print(flights[i].callsign);
    
    int alt_k = round(flights[i].alt_ft / 1000.0);
    tft.setTextColor(TFT_LIGHTGREY);
    tft.setCursor(px + 6, py + 4);
    tft.printf("%.0fmph %dk", flights[i].speed_mph, alt_k);
  }

  // DRAW "WAITING" OVERLAY
  if (currentNorthMode == CUSTOM_NORTH_WAITING) {
    tft.fillRect(80, 110, 160, 20, TFT_BLACK);
    tft.setTextColor(TFT_YELLOW);
    tft.setCursor(105, 116);
    tft.print("TAP TO SET NORTH");
  }
}

void displayFlightData(FlightInfo f) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(3);
  tft.setCursor(10, 10);
  tft.print(f.callsign);

  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(10, 45);
  tft.printf("ICAO: %s  |  SQWK: %s", f.icao.c_str(), f.squawk.c_str());

  int alt_k = round(f.alt_ft / 1000.0);

  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, 75);
  tft.printf("Alt:   %dk FT", alt_k);
  tft.setCursor(10, 105);
  tft.printf("Speed: %.0f MPH", f.speed_mph);
  tft.setCursor(10, 135);
  tft.printf("Hdg:   %.0f deg (%s)", f.heading, getCompassDirection(f.heading).c_str());

  tft.setCursor(10, 165);
  tft.print("VRate: ");
  if (f.vert_rate_fpm > 0) tft.setTextColor(TFT_GREEN, TFT_BLACK);
  else if (f.vert_rate_fpm < 0) tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
  else tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.printf("%.0f FT/MIN", f.vert_rate_fpm);
}