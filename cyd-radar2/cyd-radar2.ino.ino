#include <WiFi.h>
#include <HTTPClient.h>
#include <TJpg_Decoder.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <PubSubClient.h>
#include "time.h"
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// --- DISPLAY & TOUCH SETUP ---
TFT_eSPI tft = TFT_eSPI();

// Dedicated Touch Pins for ESP32-2432S028R
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
String targetPostcode = "S181RF"; // Center: Dronfield

// --- GLOBAL VARIABLES ---
float myLat = 0.0;
float myLon = 0.0;
float lamin, lamax, lomin, lomax;

#define MAX_FLIGHTS 10 
#define TRAIL_LENGTH 6 // Remembers 6 positions (3 minutes of history)

// --- MAP LOCATIONS (Hardcoded to prevent memory exhaustion) ---
struct Town {
  String shortName;
  float lat;
  float lon;
};

// 22 Locations covering a +/- 0.3 degree radius around Dronfield
Town regionalTowns[] = {
  // Center & Immediate
  {"SHEFF", 53.3811, -1.4701}, 
  {"ROTH",  53.4300, -1.3570}, 
  {"CHEST", 53.2350, -1.4280}, 
  {"ECKIN", 53.3080, -1.3580},
  {"STAVE", 53.2680, -1.3510},

  // East (Notts / Rotherham borders)
  {"DINN",  53.3742, -1.2036},  
  {"WORKS", 53.3031, -1.1243},
  {"MANSF", 53.1435, -1.1963},
  {"BOLSO", 53.2285, -1.2882},

  // North (South Yorks)
  {"BARNS", 53.5526, -1.4820},
  {"DONNY", 53.5228, -1.1284},
  {"STCK",  53.4827, -1.5912},

  // West (Peak District)
  {"HATH",  53.3298, -1.6568}, 
  {"BAKE",  53.2138, -1.6741},
  {"HOPE",  53.3486, -1.7456},
  {"BUXTO", 53.2587, -1.9135},
  {"CHPL",  53.3220, -1.9170}, 
  {"GLOSS", 53.4440, -1.9490},

  // South (Derbyshire)
  {"CLAY",  53.1664, -1.4137}, 
  {"DARL",  53.1612, -1.5997}, 
  {"MATLO", 53.1378, -1.5540},
  {"ALFRE", 53.0970, -1.3850}
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
  
  int trailX[TRAIL_LENGTH];
  int trailY[TRAIL_LENGTH];
  int trailCount; 
};

FlightInfo flights[MAX_FLIGHTS];
int totalFlights = 0;

// --- STATE MACHINE ---
enum ScreenState { VIEW_RADAR, VIEW_DETAILS };
ScreenState currentState = VIEW_RADAR;

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

  // Set Timezone to UK (GMT / BST)
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
    // Zoomed out to +/- 0.3 degrees to capture all 22 towns
    lamin = myLat - 0.3;
    lamax = myLat + 0.3;
    lomin = myLon - 0.3;
    lomax = myLon + 0.3;
    
    // Initialize all flight slots as empty
    for (int i = 0; i < MAX_FLIGHTS; i++) {
      flights[i].active = false;
    }
    
    delay(1000);
  } else {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.println("Postcode Lookup Failed.");
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
    
    // Check Touch Screen
    if (ts.tirqTouched() && ts.touched()) {
      TS_Point p = ts.getPoint();
      int touchX = map(p.x, 300, 3800, 0, 320);
      int touchY = map(p.y, 200, 3800, 0, 240);

      for (int i = 0; i < MAX_FLIGHTS; i++) {
        if (!flights[i].active) continue; 
        
        // If touch is within 20 pixels of a plane
        if (abs(touchX - flights[i].screenX) < 20 && abs(touchY - flights[i].screenY) < 20) {
          currentState = VIEW_DETAILS;
          detailTimer = millis(); 
          displayFlightData(flights[i]);
          break; 
        }
      }
      delay(200); // Debounce touch
    }

    // Poll API every 30 seconds
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

String getAircraftCategory(int cat) {
  switch (cat) {
    case 1: return "No ADS-B Info";
    case 2: return "Light (<15.5k lbs)";
    case 3: return "Small (15-75k lbs)";
    case 4: return "Large (75-300k lbs)";
    case 6: return "Heavy (>300k lbs)";
    case 8: return "Rotorcraft";
    case 14: return "UAV / Drone";
    default: return "Unknown";
  }
}

String getCompassDirection(float degrees) {
  const char* dirs[] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", 
                        "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
  int index = (int)((degrees + 11.25) / 22.5) % 16;
  return String(dirs[index]);
}

// ==========================================
//               NETWORK APIs
// ==========================================
bool getCoordinatesFromPostcode(String postcode) {
  WiFiClientSecure client;
  client.setInsecure(); 
  HTTPClient http;
  
  String url = "https://api.postcodes.io/postcodes/" + postcode;
  http.begin(client, url);
  
  if (http.GET() == HTTP_CODE_OK) {
    JsonDocument doc;
    deserializeJson(doc, http.getStream());
    myLon = doc["result"]["longitude"];
    myLat = doc["result"]["latitude"];
    http.end();
    return true;
  }
  http.end();
  return false;
}

void fetchLocalFlights() {
  tft.fillRect(0, 0, 320, 20, TFT_BLACK);
  tft.setCursor(5, 2);
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW);
  tft.print("Scanning Airspace...");

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.useHTTP10(true); // Force HTTP 1.0 to prevent chunked encoding crash

  String url = "https://opensky-network.org/api/states/all?lamin=" + String(lamin, 4) + 
               "&lomin=" + String(lomin, 4) + "&lamax=" + String(lamax, 4) + "&lomax=" + String(lomax, 4) + "&extended=1";             
  
  http.begin(client, url);
  http.setUserAgent("ESP32-FlightTracker/1.0");

  if (http.GET() == HTTP_CODE_OK) {
    JsonDocument filter;
    filter["time"] = true;
    filter["states"][0] = true; 

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    
    if (error) {
      Serial.println("JSON Parse Failed");
      http.end();
      return;
    }

    bool updatedThisCycle[MAX_FLIGHTS] = {false};
    totalFlights = 0;

    if (!doc["states"].isNull()) {
      JsonArray states = doc["states"];
      
      for (int i = 0; i < states.size(); i++) {
        JsonArray state = states[i];
        String currentIcao = state[0].as<String>();

        int targetIndex = -1;
        
        // Find existing plane
        for(int j = 0; j < MAX_FLIGHTS; j++) {
            if(flights[j].active && flights[j].icao == currentIcao) {
                targetIndex = j;
                break;
            }
        }

        // Or find empty slot for new plane
        if (targetIndex == -1) {
            for(int j = 0; j < MAX_FLIGHTS; j++) {
                if(!flights[j].active) {
                    targetIndex = j;
                    break;
                }
            }
        }

        if (targetIndex == -1) continue; // Array full

        // Shift trail history
        if (flights[targetIndex].active) {
            for (int t = TRAIL_LENGTH - 1; t > 0; t--) {
                flights[targetIndex].trailX[t] = flights[targetIndex].trailX[t-1];
                flights[targetIndex].trailY[t] = flights[targetIndex].trailY[t-1];
            }
            flights[targetIndex].trailX[0] = flights[targetIndex].screenX;
            flights[targetIndex].trailY[0] = flights[targetIndex].screenY;

            if(flights[targetIndex].trailCount < TRAIL_LENGTH) {
                flights[targetIndex].trailCount++;
            }
        } else {
            flights[targetIndex].trailCount = 0;
        }

        // Update live stats
        flights[targetIndex].icao = currentIcao;
        flights[targetIndex].timestamp = doc["time"].as<long>();
        
        flights[targetIndex].callsign = state[1].as<String>();
        flights[targetIndex].callsign.trim();
        if(flights[targetIndex].callsign.length() == 0) flights[targetIndex].callsign = "UNKNOWN";

        flights[targetIndex].country = state[2].as<String>();
        flights[targetIndex].lon = state[5].as<float>();
        flights[targetIndex].lat = state[6].as<float>();

        flights[targetIndex].screenX = (int)mapFloat(flights[targetIndex].lon, lomin, lomax, 0, 320);
        flights[targetIndex].screenY = (int)mapFloat(flights[targetIndex].lat, lamin, lamax, 240, 0);

        flights[targetIndex].alt_ft = state[7].as<float>() * 3.28084;
        flights[targetIndex].speed_mph = state[9].as<float>() * 2.23694; 
        flights[targetIndex].heading = state[10].as<float>();            
        flights[targetIndex].vert_rate_fpm = state[11].as<float>() * 196.85; 
        
        flights[targetIndex].squawk = state[14].as<String>();
        if(flights[targetIndex].squawk == "null") flights[targetIndex].squawk = "NONE";
        flights[targetIndex].category = state[17].as<int>();

        flights[targetIndex].active = true;
        updatedThisCycle[targetIndex] = true;
        totalFlights++;
      }
    }

    // Deactivate planes that left the airspace
    for(int i = 0; i < MAX_FLIGHTS; i++) {
        if (!updatedThisCycle[i]) {
            flights[i].active = false;
        }
    }
  }
  http.end();
}

// ==========================================
//               UI DRAWING
// ==========================================

void drawRadarMap() {
  tft.fillScreen(TFT_BLACK);

  // --- DRAW REGIONAL TOWNS ---
  tft.setTextSize(1);
  for(int i = 0; i < numTowns; i++) {
     int tx = (int)mapFloat(regionalTowns[i].lon, lomin, lomax, 0, 320);
     int ty = (int)mapFloat(regionalTowns[i].lat, lamin, lamax, 240, 0);
     
     // Only draw if within screen bounds
     if (tx >= 0 && tx <= 320 && ty >= 0 && ty <= 240) {
         tft.fillCircle(tx, ty, 2, TFT_DARKGREY); 
         tft.setTextColor(TFT_DARKGREY);
         tft.setCursor(tx + 4, ty - 4); 
         tft.print(regionalTowns[i].shortName);
     }
  }

  // Draw Crosshairs and Dronfield Center
  tft.drawLine(160, 0, 160, 240, TFT_DARKGREY); 
  tft.drawLine(0, 120, 320, 120, TFT_DARKGREY); 
  tft.fillCircle(160, 120, 3, TFT_RED);
  tft.setTextColor(TFT_RED);
  tft.setCursor(165, 125);
  tft.print("HOME");

  if (totalFlights == 0) {
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(20, 20);
    tft.print("Airspace Clear");
    return;
  }

  // Draw planes and trails
  tft.setTextSize(1);
  for (int i = 0; i < MAX_FLIGHTS; i++) {
    if (!flights[i].active) continue;

    int px = flights[i].screenX;
    int py = flights[i].screenY;
    
    // Draw Trail Line
    for (int t = 0; t < flights[i].trailCount; t++) {
        if (t == 0) {
            tft.drawLine(px, py, flights[i].trailX[0], flights[i].trailY[0], TFT_DARKGREY);
        } else {
            tft.drawLine(flights[i].trailX[t-1], flights[i].trailY[t-1], flights[i].trailX[t], flights[i].trailY[t], TFT_DARKGREY);
        }
    }

    // Draw Plane Dot
    tft.fillCircle(px, py, 4, TFT_CYAN);
    tft.setTextColor(TFT_GREEN);
    tft.setCursor(px + 6, py - 4);
    tft.print(flights[i].callsign);
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

  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, 75);
  tft.printf("Alt:   %.0f FT", f.alt_ft);
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

  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(10, 195);
  tft.print("Origin: ");
  tft.print(f.country);

  tft.setCursor(10, 210);
  tft.print("Type:   ");
  tft.print(getAircraftCategory(f.category));

  time_t rawtime = f.timestamp;
  struct tm * timeinfo = localtime(&rawtime);
  char timeString[9]; 
  strftime(timeString, sizeof(timeString), "%H:%M:%S", timeinfo);

  tft.setCursor(10, 225);
  tft.print("Update: ");
  tft.print(timeString); 
}