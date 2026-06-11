#include <WiFi.h>
#include <HTTPClient.h>
#include <TJpg_Decoder.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <PubSubClient.h>
#include "time.h"
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// Display Setup
TFT_eSPI tft = TFT_eSPI();

// ---- DEDICATED RESISTIVE TOUCH CONFIG (ESP32-2432S028R) ----
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

// Type your postcode here (no spaces is safest for the API)
String targetPostcode = "S181RF"; 

// Variables to hold our local coordinates
float myLat = 0.0;
float myLon = 0.0;

// --- DATA CONTAINER ---
struct FlightInfo {
  long timestamp;
  String icao;
  String callsign;
  String country;
  float alt_ft;
  float speed_mph;
  float heading;
  float vert_rate_fpm;
  String squawk;
  int category;
};

void setup() {
  Serial.begin(115200);
  
  // Initialize Display
  tft.init();
  tft.setRotation(1); // Landscape for CYD
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  
  // Connect to WiFi
  tft.setCursor(10, 10);
  tft.print("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(10, 10);
  tft.println("WiFi Connected!");

  // Step 1: Get Coordinates from Postcode
  if (getCoordinatesFromPostcode(targetPostcode)) {
    tft.printf("Lat: %.4f\nLon: %.4f\n", myLat, myLon);
    delay(2000);
  } else {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.println("Postcode Lookup Failed.");
    while(true); // Halt
  }

  // Set Timezone to UK (GMT / BST)
  setenv("TZ", "GMT0BST,M3.5.0/1,M10.5.0", 1);
  tzset();
}

void loop() {
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 10);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.println("Scanning overhead airspace...");
  
  // Step 2: Fetch Flights
  fetchLocalFlights();

  // OpenSky allows unauthenticated requests every ~10-15 seconds, 
  // but to avoid rate limits, polling every 30 seconds is safe.
  delay(30000); 
}

// --- FUNCTION: Get Lat/Lon from UK Postcode ---
bool getCoordinatesFromPostcode(String postcode) {
  WiFiClientSecure client;
  client.setInsecure(); // Bypass SSL certificate validation for simplicity
  HTTPClient http;
  
  String url = "https://api.postcodes.io/postcodes/" + postcode;
  http.begin(client, url);
  
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
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

// --- FUNCTION: Get Flights from OpenSky ---
void fetchLocalFlights() {
  tft.println("Fetching flights...");
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  
  // Force HTTP/1.0
  http.useHTTP10(true); 

  float lamin = myLat - 0.2;
  float lamax = myLat + 0.2;
  float lomin = myLon - 0.2;
  float lomax = myLon + 0.2;

  // I added "&extended=1" to the end of the URL per the API documentation
  String url = "https://opensky-network.org/api/states/all?lamin=" + String(lamin, 4) + 
               "&lomin=" + String(lomin, 4) + "&lamax=" + String(lamax, 4) + "&lomax=" + String(lomax, 4) + "&extended=1";             
  
  http.begin(client, url);
  http.setUserAgent("ESP32-FlightTracker/1.0");

  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    JsonDocument filter;
    filter["time"] = true;
    filter["states"][0] = true; 

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    
    if (error) {
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.setCursor(10, 10);
      tft.println("JSON Parse Failed:");
      tft.println(error.c_str()); 
      http.end();
      return;
    }

    if (doc["states"].isNull() || doc["states"].size() == 0) {
      tft.fillScreen(TFT_BLACK);
      tft.setCursor(10, 10);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.println("No flights detected");
      tft.println("in your immediate area.");
      http.end();
      return;
    }

    // --- EXTRACT ALL DATA ---
    FlightInfo flight;
    JsonArray state = doc["states"][0];
    
    flight.timestamp = doc["time"].as<long>();         // Top level time
    
    flight.icao = state[0].as<String>();               // Index 0: ICAO24
    
    flight.callsign = state[1].as<String>();           // Index 1: Callsign
    flight.callsign.trim();
    if(flight.callsign.length() == 0) flight.callsign = "UNKNOWN";

    flight.country = state[2].as<String>();            // Index 2: Country

    // Index 7: Altitude (Meters to Feet)
    flight.alt_ft = state[7].as<float>() * 3.28084;
    
    // Index 9: Velocity (M/S to MPH)
    flight.speed_mph = state[9].as<float>() * 2.23694; 
    
    // Index 10: True Track (Heading in degrees)
    flight.heading = state[10].as<float>();            
    
    // Index 11: Vertical Rate (M/S to Feet-Per-Minute)
    flight.vert_rate_fpm = state[11].as<float>() * 196.85; 
    
    // Index 14: Squawk Code
    flight.squawk = state[14].as<String>();
    if(flight.squawk == "null") flight.squawk = "NONE";

    // Index 17: Aircraft Category
    flight.category = state[17].as<int>();

    // Send the populated struct to the display function
    displayFlightData(flight);
  } else {
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(10, 10);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.println("API Rate Limit Reached.");
    tft.println("Waiting 30s...");
  }
  http.end();
}

// --- FUNCTION: Render Data to CYD ---
void displayFlightData(FlightInfo f) {
  tft.fillScreen(TFT_BLACK);

  // --- ROW 1: Callsign (Large) ---
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(3);
  tft.setCursor(10, 10);
  tft.print(f.callsign);

  // --- ROW 2: ICAO and Squawk (Small) ---
  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(10, 45);
  tft.printf("ICAO: %s  |  SQWK: %s", f.icao.c_str(), f.squawk.c_str());

  // --- ROW 3: Primary Telemetry (Medium) ---
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  
  tft.setCursor(10, 75);
  tft.printf("Alt:   %.0f FT", f.alt_ft);
  
  tft.setCursor(10, 105);
  tft.printf("Speed: %.0f MPH", f.speed_mph);
  
  tft.setCursor(10, 135);
  // We use .c_str() because printf expects standard C-strings, not Arduino Strings
  tft.printf("Hdg:   %.0f deg (%s)", f.heading, getCompassDirection(f.heading).c_str());

  // Color-code the vertical rate (Green for climb, Magenta for descend)
  tft.setCursor(10, 165);
  tft.print("VRate: ");
  if (f.vert_rate_fpm > 0) tft.setTextColor(TFT_GREEN, TFT_BLACK);
  else if (f.vert_rate_fpm < 0) tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
  tft.printf("%.0f FT/MIN", f.vert_rate_fpm);

// --- ROW 4: Metadata (Small) ---
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  
  tft.setCursor(10, 195);
  tft.print("Origin: ");
  tft.print(f.country);

  // --- NEW: Render the Category ---
  tft.setCursor(10, 210);
  tft.print("Type:   ");
  tft.print(getAircraftCategory(f.category));

  // --- Convert Epoch to HH:MM:SS ---
  time_t rawtime = f.timestamp;
  struct tm * timeinfo = localtime(&rawtime);
  
  char timeString[9]; 
  strftime(timeString, sizeof(timeString), "%H:%M:%S", timeinfo);

  tft.setCursor(10, 225);
  tft.print("Update: ");
  tft.print(timeString);
}

// --- FUNCTION: Translate Category Integer to String ---
String getAircraftCategory(int cat) {
  switch (cat) {
    case 1: return "No ADS-B Info";
    case 2: return "Light (< 15,500 lbs)";
    case 3: return "Small (15k - 75k lbs)";
    case 4: return "Large (75k - 300k lbs)";
    case 5: return "High Vortex Large";
    case 6: return "Heavy (> 300,000 lbs)";
    case 7: return "High Performance";
    case 8: return "Rotorcraft (Helicopter)";
    case 9: return "Glider / Sailplane";
    case 10: return "Lighter-than-air";
    case 11: return "Skydiver / Parachutist";
    case 12: return "Ultralight / Paraglider";
    case 14: return "Unmanned Aerial Vehicle";
    case 15: return "Spacecraft";
    case 16: return "Emergency Vehicle (Surface)";
    case 17: return "Service Vehicle (Surface)";
    default: return "Unknown / Not Provided";
  }
}

// --- FUNCTION: Translate Degrees to Compass Direction ---
String getCompassDirection(float degrees) {
  // Array of 16 compass points
  const char* directions[] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", 
                              "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
  
  // Shift by 11.25 degrees to center the slices, divide by 22.5, and use modulo 16
  int index = (int)((degrees + 11.25) / 22.5) % 16;
  
  return String(directions[index]);
}