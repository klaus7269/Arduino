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

// --- GLOBAL VARIABLES ---
float myLat = 0.0;
float myLon = 0.0;
float lamin, lamax, lomin, lomax; // Radar Boundaries

// We will track up to 10 local flights to save memory
#define MAX_FLIGHTS 10 

struct FlightInfo {
  long timestamp;
  String icao;
  String callsign;
  String country;
  float lat, lon;      // GPS Coordinates
  float alt_ft;
  float speed_mph;
  float heading;
  float vert_rate_fpm;
  String squawk;
  int category;
  int screenX, screenY; // Where it lives on the TFT screen
};

FlightInfo flights[MAX_FLIGHTS];
int totalFlights = 0;

// --- STATE MACHINE (For Touch & Timers) ---
enum ScreenState { VIEW_RADAR, VIEW_DETAILS };
ScreenState currentState = VIEW_RADAR;

unsigned long lastApiCheck = 0;
unsigned long detailTimer = 0;
const unsigned long API_INTERVAL = 30000; // 30 seconds
const unsigned long DETAIL_DURATION = 5000; // 5 seconds

// ==========================================
//                 SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  
  // Initialize Display
  tft.init();
  tft.setRotation(1); 
  tft.fillScreen(TFT_BLACK);
  
  // Initialize Touch
  tsSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(tsSPI);
  ts.setRotation(1);

  // Set Timezone to UK (GMT / BST)
  setenv("TZ", "GMT0BST,M3.5.0/1,M10.5.0", 1);
  tzset();
  
  // Connect to WiFi
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

  // Step 1: Get Center Coordinates
  if (getCoordinatesFromPostcode(targetPostcode)) {
    // Create a 0.2 degree radar bounding box
    lamin = myLat - 0.2;
    lamax = myLat + 0.2;
    lomin = myLon - 0.2;
    lomax = myLon + 0.2;
    delay(1000);
  } else {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.println("Postcode Lookup Failed.");
    while(true); // Halt
  }
}

// ==========================================
//                 MAIN LOOP
// ==========================================
void loop() {
  // 1. Check if we are viewing a plane's details
  if (currentState == VIEW_DETAILS) {
    // If 5 seconds have passed, return to Radar
    if (millis() - detailTimer >= DETAIL_DURATION) {
      currentState = VIEW_RADAR;
      drawRadarMap(); 
    }
  } 
  
  // 2. Check if we are on the Radar Map
  else if (currentState == VIEW_RADAR) {
    
    // -- Handle Touch Events --
    if (ts.tirqTouched() && ts.touched()) {
      TS_Point p = ts.getPoint();
      
      // Calibrate standard CYD touch coordinates to 320x240 pixels
      int touchX = map(p.x, 300, 3800, 0, 320);
      int touchY = map(p.y, 200, 3800, 0, 240);

      // Check if touch is near any plotted plane (within a 20 pixel radius)
      for (int i = 0; i < totalFlights; i++) {
        if (abs(touchX - flights[i].screenX) < 20 && abs(touchY - flights[i].screenY) < 20) {
          // Plane Touched! Switch to Detail View
          currentState = VIEW_DETAILS;
          detailTimer = millis(); 
          displayFlightData(flights[i]);
          break; // Stop checking
        }
      }
      delay(200); // Debounce touch to prevent double-taps
    }

    // -- Handle API Polling --
    // We fetch immediately on boot (lastApiCheck == 0), then every 30s
    if (millis() - lastApiCheck >= API_INTERVAL || lastApiCheck == 0) {
      lastApiCheck = millis();
      fetchLocalFlights();
      if (currentState == VIEW_RADAR) {
        drawRadarMap(); // Redraw map with new data
      }
    }
  }
}

// ==========================================
//               MATH HELPERS
// ==========================================

// Arduino's built-in map() only works with integers. We need floats for GPS.
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
  http.useHTTP10(true); 

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

    // Reset flight count
    totalFlights = 0;

    if (!doc["states"].isNull()) {
      JsonArray states = doc["states"];
      
      // Loop through up to MAX_FLIGHTS, or however many the API returned
      for (int i = 0; i < states.size() && i < MAX_FLIGHTS; i++) {
        JsonArray state = states[i];
        
        flights[i].timestamp = doc["time"].as<long>();
        flights[i].icao = state[0].as<String>();
        flights[i].callsign = state[1].as<String>();
        flights[i].callsign.trim();
        if(flights[i].callsign.length() == 0) flights[i].callsign = "UNKNOWN";

        flights[i].country = state[2].as<String>();
        
        // Grab Coordinates
        flights[i].lon = state[5].as<float>();
        flights[i].lat = state[6].as<float>();

        // MATH MAGIC: Translate Earth GPS to 320x240 screen pixels
        // X = Longitude (Min to Max -> 0 to 320)
        flights[i].screenX = (int)mapFloat(flights[i].lon, lomin, lomax, 0, 320);
        // Y = Latitude (Min to Max -> 240 to 0) Inverted! North is UP, but Y=0 is UP.
        flights[i].screenY = (int)mapFloat(flights[i].lat, lamin, lamax, 240, 0);

        flights[i].alt_ft = state[7].as<float>() * 3.28084;
        flights[i].speed_mph = state[9].as<float>() * 2.23694; 
        flights[i].heading = state[10].as<float>();            
        flights[i].vert_rate_fpm = state[11].as<float>() * 196.85; 
        
        flights[i].squawk = state[14].as<String>();
        if(flights[i].squawk == "null") flights[i].squawk = "NONE";
        flights[i].category = state[17].as<int>();

        totalFlights++;
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

  // Draw Grid Lines representing the center (Your Postcode)
  tft.drawLine(160, 0, 160, 240, TFT_DARKGREY); // Center Longitude
  tft.drawLine(0, 120, 320, 120, TFT_DARKGREY); // Center Latitude

  // Draw Center Dot
  tft.fillCircle(160, 120, 3, TFT_RED);
  tft.setTextColor(TFT_RED);
  tft.setTextSize(1);
  tft.setCursor(165, 125);
  tft.print("HOME");

  if (totalFlights == 0) {
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(20, 20);
    tft.print("Airspace Clear");
    return;
  }

  // Draw all planes
  tft.setTextSize(1);
  for (int i = 0; i < totalFlights; i++) {
    int px = flights[i].screenX;
    int py = flights[i].screenY;
    
    // Draw Plane Dot (Cyan)
    tft.fillCircle(px, py, 4, TFT_CYAN);
    
    // Print Callsign slightly offset from the dot
    tft.setTextColor(TFT_GREEN);
    tft.setCursor(px + 6, py - 4);
    tft.print(flights[i].callsign);
  }
}

void displayFlightData(FlightInfo f) {
  tft.fillScreen(TFT_BLACK);

  // -- ROW 1: Callsign --
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(3);
  tft.setCursor(10, 10);
  tft.print(f.callsign);

  // -- ROW 2: ICAO and Squawk --
  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(10, 45);
  tft.printf("ICAO: %s  |  SQWK: %s", f.icao.c_str(), f.squawk.c_str());

  // -- ROW 3: Primary Telemetry --
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

  // -- ROW 4: Metadata --
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