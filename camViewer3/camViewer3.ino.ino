#include <WiFi.h>
#include <HTTPClient.h>
#include <TJpg_Decoder.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

TFT_eSPI tft = TFT_eSPI();

// ---- DEDICATED RESISTIVE TOUCH CONFIG (ESP32-2432S028R) ----
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33

// FIX: Use VSPI here to prevent colliding with the display's HSPI channel
SPIClass tsSPI(VSPI); 
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

// ---- WIFI VARIABLES ----
const char* primarySSID = "vodafone09FEBA";
const char* primaryPass = "NT4qgkds2t4zHx2K";
const char* hotspotSSID = "Nicks's S23 Ultra";
const char* hotspotPass = "Password1";

bool useHotspot = false; 

// ---- CAMERA STREAM VARIABLES ----
const String streamBase = "http://192.168.0.111:1984/api/frame.jpeg?src="; 

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

// ---- UI BUTTON DEFINITIONS ----
#define BUTTON_Y 195
#define BUTTON_H 40
#define BTN_CAM_X 10
#define BTN_CAM_W 145
#define BTN_WIFI_X 165
#define BTN_WIFI_W 145

// FIX: Always return 1 so the JPEG decoder doesn't abort the entire frame
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (y < 190) { 
    tft.pushImage(x, y, w, h, bitmap);
  }
  return 1; 
}

void setup() {
  Serial.begin(115200);
  
  tft.init();
  tft.setRotation(1); // Landscape orientation
  tft.fillScreen(TFT_BLACK);
  
  // Initialize VSPI bus for the touch controller completely separate from the screen
  tsSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(tsSPI);
  ts.setRotation(1); // Match display rotation

  TJpgDec.setCallback(tft_output);
  TJpgDec.setJpgScale(2); 

  tft.setSwapBytes(true);
  
  updateStreamURL();
  connectToWiFi(primarySSID, primaryPass);
  drawUI();
}

void loop() {
  // 1. Handle UI Touch Inputs via Isolated VSPI Bus
  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    
    // Map raw analog touchscreen values to screen pixels
    uint16_t t_x = map(p.x, 200, 3800, 0, 320);
    uint16_t t_y = map(p.y, 240, 3800, 0, 240);
    
    Serial.print("Touch Position -> X: "); Serial.print(t_x);
    Serial.print(" | Y: "); Serial.println(t_y);

    // Verify if touch falls inside button panel row
    if (t_y >= BUTTON_Y && t_y <= (BUTTON_Y + BUTTON_H)) {
      
      // CAMERA BUTTON ACTION
      if (t_x >= BTN_CAM_X && t_x <= (BTN_CAM_X + BTN_CAM_W)) {
        currentCamIndex = (currentCamIndex + 1) % totalCameras;
        updateStreamURL();
        tft.fillRect(0, 0, 320, 190, TFT_BLACK); // Clear video canvas safely
        drawUI();
        delay(400); // Debounce
      }
      
      // WIFI BUTTON ACTION
      if (t_x >= BTN_WIFI_X && t_x <= (BTN_WIFI_X + BTN_WIFI_W)) {
        useHotspot = !useHotspot;
        tft.fillRect(0, 0, 320, 190, TFT_BLACK); 
        drawUI();
        
        if (useHotspot) {
          connectToWiFi(hotspotSSID, hotspotPass);
        } else {
          connectToWiFi(primarySSID, primaryPass);
        }
        delay(400); 
      }
    }
  }

  // 2. Stream Fetch and Render (Dynamic Stream Buffering)
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(currentURL);
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
      WiFiClient* stream = http.getStreamPtr();
      static uint8_t jpgBuffer[64 * 1024]; // Expanded to 64KB for safety overhead
      int bytesRead = 0;
      
      uint32_t timeout = millis();
      while (http.connected() && (millis() - timeout < 1500) && bytesRead < sizeof(jpgBuffer)) {
        int availableBytes = stream->available();
        if (availableBytes > 0) {
          int toRead = min(availableBytes, (int)(sizeof(jpgBuffer) - bytesRead));
          stream->readBytes(&jpgBuffer[bytesRead], toRead);
          bytesRead += toRead;
          timeout = millis();
        }
        delay(1);
      }

      if (bytesRead > 0) {
        TJpgDec.drawJpg(0, 0, jpgBuffer, bytesRead);
      }
    }
    http.end();
  } else {
    tft.setCursor(10, 80);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextFont(2);
    tft.print("Connecting to Wi-Fi...");
  }
}

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
  tft.fillRect(0, 50, 320, 40, TFT_BLACK); 
}

void drawUI() {
  tft.drawFastHLine(0, 191, 320, TFT_DARKGREY);
  tft.fillRect(0, 192, 320, 48, TFT_BLACK);

  // Camera Switcher Button
  tft.fillRoundRect(BTN_CAM_X, BUTTON_Y, BTN_CAM_W, BUTTON_H, 4, TFT_BLUE);
  tft.drawRoundRect(BTN_CAM_X, BUTTON_Y, BTN_CAM_W, BUTTON_H, 4, TFT_WHITE);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(MC_DATUM); 
  tft.drawString(cameraFeeds[currentCamIndex], BTN_CAM_X + (BTN_CAM_W / 2), BUTTON_Y + (BUTTON_H / 2), 1);

  // Profile Network Toggler Button
  uint16_t wifiBtnColor = useHotspot ? TFT_GREEN : TFT_NAVY;
  String wifiLabel = useHotspot ? "S23 Hotspot" : "Home WiFi";
  
  tft.fillRoundRect(BTN_WIFI_X, BUTTON_Y, BTN_WIFI_W, BUTTON_H, 4, wifiBtnColor);
  tft.drawRoundRect(BTN_WIFI_X, BUTTON_Y, BTN_WIFI_W, BUTTON_H, 4, TFT_WHITE);
  tft.drawString(wifiLabel, BTN_WIFI_X + (BTN_WIFI_W / 2), BUTTON_Y + (BUTTON_H / 2), 1);
}