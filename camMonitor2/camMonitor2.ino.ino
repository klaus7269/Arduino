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
}

void loop() {
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(10, 10);
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
  tft.println("fetchLocalFlight");
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  
  // Create a bounding box roughly +/- 0.3 degrees (~25 miles) around the postcode
  float lamin = myLat - 0.3;
  float lamax = myLat + 0.3;
  float lomin = myLon - 0.3;
  float lomax = myLon + 0.3;

  tft.println("lamin=" + String(lamin) + " lomin=" + String(lomin));
  tft.println("lamax=" + String(lamax) + " lomax=" + String(lomax));

  String url = "https://opensky-network.org/api/states/all?lamin=" + String(lamin) + 
               "&lomin=" + String(lomin) + "&lamax=" + String(lamax) + "&lomax=" + String(lomax);
               
  http.begin(client, url);
  int httpCode = http.GET();
  tft.println("httpCode=" + String(httpCode));
  if (httpCode == HTTP_CODE_OK) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, http.getStream());
    
    if (error) {
      Serial.println("JSON Parse Failed");
      return;
    }

    // Check if there are any flights overhead
    if (doc["states"].isNull() || doc["states"].size() == 0) {
      tft.fillScreen(TFT_BLACK);
      tft.setCursor(10, 10);
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.println("No flights detected");
      tft.println("in your immediate area.");
      http.end();
      return;
    }

    // Grab the first (closest/random in array) flight
    JsonArray flightData = doc["states"][0];
    
    String callsign = flightData[1].as<String>();
    callsign.trim(); // Remove trailing spaces
    float altitude_m = flightData[7].as<float>();
    float velocity_ms = flightData[9].as<float>();
    
    // Convert to UK standard aviation units (Feet and MPH/Knots)
    float altitude_ft = altitude_m * 3.28084;
    float speed_mph = velocity_ms * 2.23694;

    displayFlightData(callsign, altitude_ft, speed_mph);
    
  } else {
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(10, 10);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.println("API Rate Limit Reached.");
    tft.println("Waiting...");
  }
  http.end();
}

// --- FUNCTION: Render Data to CYD ---
void displayFlightData(String callsign, float alt, float speed) {
  tft.fillScreen(TFT_BLACK);
  
  // Callsign Header
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(3);
  tft.setCursor(10, 10);
  tft.print("FLIGHT: ");
  tft.println(callsign.length() > 0 ? callsign : "UNKNOWN");

  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  
  // Live Telemetry
  tft.setCursor(10, 60);
  tft.printf("Alt:   %.0f FT\n", alt);
  tft.setCursor(10, 90);
  tft.printf("Speed: %.0f MPH\n", speed);

  // --- PLACEHOLDERS FOR METADATA ---
  // To populate these, you would need to pass the 'callsign' variable 
  // into a secondary paid API (like AviationEdge or FlightAware)
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.setCursor(10, 140);
  tft.println("Route: LHR -> JFK (Mock)");
  tft.setCursor(10, 170);
  tft.println("Type:  Boeing 777 (Mock)");
}