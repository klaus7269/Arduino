#include <WiFi.h>
#include <HTTPClient.h>
#include <TJpg_Decoder.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

// ---- WIFI VARIABLES ----
const char* primarySSID = "vodafone09FEBA";
const char* primaryPass = "NT4qgkds2t4zHx2K";
const char* hotspotSSID = "Nicks's S23 Ultra";
const char* hotspotPass = "Password1";

bool useHotspot = false; // Toggle state for Wifi Button

// ---- CAMERA STREAM VARIABLES ----
// Base URL structure - adapt if you use a specific gateway IP
const String streamBase = "http://192.168.1.111:1984/api/frame.jpeg?src="; 

const int totalCameras = 6;
String cameraFeeds[totalCameras] = {
  "CamHBDriveZoom_sub", 
  "CamHBGarage_sub", 
  "CamHBPavement_sub", 
  "CamSLDrive_sub", 
  "CamSLGarden_sub", 
  "CamSLPavement_sub"
};
int currentCamIndex = 0;
String currentURL = "";

// ---- UI BUTTON DEFINITIONS (Bottom of 320x240 screen) ----
// Giving the stream 320x190 pixels, leaving 50 pixels at the bottom for navigation
#define BUTTON_Y 195
#define BUTTON_H 40
#define BTN_CAM_X 10
#define BTN_CAM_W 145
#define BTN_WIFI_X 165
#define BTN_WIFI_W 145

// This function is called by TJpg_Decoder to draw image chunks to the TFT screen
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  // Stop drawing if we go off the stream allocation area (leaving room for buttons)
  if (y >= 190) return 0; 
  tft.pushImage(x, y, w, h, bitmap);
  return 1;
}

void setup() {
  Serial.begin(115200);
  
  tft.init();
  tft.setRotation(1); // Landscape
  tft.fillScreen(TFT_BLACK);
  
  // Calibrate touch coordinates (standard calibration numbers for CYD)
  uint16_t calData[5] = { 326, 3491, 511, 3443, 2 };
  tft.setTouch(calData);

  // Initialize decoder
  TJpgDec.setCallback(tft_output);
  TJpgDec.setJpgScale(2); // Match your sub-stream downscaling requirements

  // Setup Initial Connection
  updateStreamURL();
  connectToWiFi(primarySSID, primaryPass);
  drawUI();
}

/*
void loop() {
  // 1. Handle UI Touch Inputs
  uint16_t t_x = 0, t_y = 0;
  if (tft.getTouch(&t_x, &t_y)) {
    
    // ------ ADD THESE DIAGNOSTIC LINES ------
    Serial.print("Touch Detected! Raw X: ");
    Serial.print(t_x);
    Serial.print(" | Raw Y: ");
    Serial.println(t_y);
    // ----------------------------------------

    // Check if touch is vertically inside the button row
    if (t_y >= BUTTON_Y && t_y <= (BUTTON_Y + BUTTON_H)) {
      
      // CAMERA BUTTON PRESSED
      if (t_x >= BTN_CAM_X && t_x <= (BTN_CAM_X + BTN_CAM_W)) {
        currentCamIndex = (currentCamIndex + 1) % totalCameras;
        updateStreamURL();
        drawUI();
        delay(300); // Simple debounce
      }
      
      // WIFI BUTTON PRESSED
      if (t_x >= BTN_WIFI_X && t_x <= (BTN_WIFI_X + BTN_WIFI_W)) {
        useHotspot = !useHotspot;
        tft.fillRect(0, 0, 320, 190, TFT_BLACK); // Clear stream zone
        drawUI();
        
        if (useHotspot) {
          connectToWiFi(hotspotSSID, hotspotPass);
        } else {
          connectToWiFi(primarySSID, primaryPass);
        }
        delay(300);
      }
    }
  }

  // 2. Fetch and render stream frame if connected
// 2. Fetch and render stream frame if connected
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(currentURL);
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
      int totalBytes = http.getSize();
      WiFiClient* stream = http.getStreamPtr();

      // Allocate a temporary buffer memory block for the frame
      // 40KB is typically plenty for a downscaled 320x240 sub-stream JPEG frame
      static uint8_t jpgBuffer[40 * 1024]; 
      int bytesRead = 0;

      if (totalBytes > 0 && totalBytes < sizeof(jpgBuffer)) {
        // Read the network stream completely into our memory buffer
        while (http.connected() && bytesRead < totalBytes) {
          int available = stream->available();
          if (available > 0) {
            int toRead = min(available, (int)(totalBytes - bytesRead));
            stream->readBytes(&jpgBuffer[bytesRead], toRead);
            bytesRead += toRead;
          }
          delay(1); // Small yield to prevent core lockups
        }

        // Pass the filled memory buffer to the version of drawJpg your library expects
        if (bytesRead == totalBytes) {
          TJpgDec.drawJpg(0, 0, jpgBuffer, totalBytes);
        }
      }
    }
    http.end();
  } else {
    // Show reconnection warning if Wi-Fi dropped out
    tft.setCursor(10, 80);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextFont(2);
    tft.print("Connecting to Wi-Fi...");
  }
}
*/
void loop() {
  // Read the raw pressure/resistance value (Z)
  uint16_t raw_z = tft.getTouchRawZ();
  
  // If the raw pressure is above a tiny threshold, someone is touching the glass!
  if (raw_z > 20) { 
    uint16_t raw_x = 0;
    uint16_t raw_y = 0;
    tft.getTouchRaw(&raw_x, &raw_y);
    
    Serial.print("!!! HARDWARE ALIVE !!! Raw X: ");
    Serial.print(raw_x);
    Serial.print(" | Raw Y: ");
    Serial.print(raw_y);
    Serial.print(" | Pressure Z: ");
    Serial.println(raw_z);
  }

  delay(50); // Simple delay to make the Serial Monitor readable
}

// ---- HELPER FUNCTIONS ----

void updateStreamURL() {
  currentURL = streamBase + cameraFeeds[currentCamIndex];
  Serial.println("Switched to URL: " + currentURL);
}

void connectToWiFi(const char* ssid, const char* password) {
  Serial.print("Connecting to: ");
  Serial.println(ssid);
  
  WiFi.disconnect();
  WiFi.begin(ssid, password);
  
  tft.fillRect(0, 50, 320, 40, TFT_BLACK);
  tft.setCursor(40, 60);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.print("Connecting to WiFi...");

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  tft.fillRect(0, 50, 320, 40, TFT_BLACK); // Clear text when connected
}

void drawUI() {
  // Draw Bottom Control Panel Background Divider Line
  tft.drawFastHLine(0, 191, 320, TFT_DARKGREY);
  tft.fillRect(0, 192, 320, 48, TFT_BLACK);

  // 1. Camera Feed Button
  tft.fillRoundRect(BTN_CAM_X, BUTTON_Y, BTN_CAM_W, BUTTON_H, 4, TFT_BLUE);
  tft.drawRoundRect(BTN_CAM_X, BUTTON_Y, BTN_CAM_W, BUTTON_H, 4, TFT_WHITE);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(MC_DATUM); // Middle-Center alignment
  tft.drawString(cameraFeeds[currentCamIndex], BTN_CAM_X + (BTN_CAM_W / 2), BUTTON_Y + (BUTTON_H / 2), 1);

  // 2. Wi-Fi Source Button
  uint16_t wifiBtnColor = useHotspot ? TFT_GREEN : TFT_NAVY;
  String wifiLabel = useHotspot ? "S23 Hotspot" : "Home WiFi";
  
  tft.fillRoundRect(BTN_WIFI_X, BUTTON_Y, BTN_WIFI_W, BUTTON_H, 4, wifiBtnColor);
  tft.drawRoundRect(BTN_WIFI_X, BUTTON_Y, BTN_WIFI_W, BUTTON_H, 4, TFT_WHITE);
  tft.drawString(wifiLabel, BTN_WIFI_X + (BTN_WIFI_W / 2), BUTTON_Y + (BUTTON_H / 2), 1);
}