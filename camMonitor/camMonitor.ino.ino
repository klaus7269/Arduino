#include <WiFi.h>
#include <HTTPClient.h>
#include <TJpg_Decoder.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <PubSubClient.h>

TFT_eSPI tft = TFT_eSPI();

// ---- DEDICATED RESISTIVE TOUCH CONFIG (ESP32-2432S028R) ----
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33

SPIClass tsSPI(VSPI); 
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

// ---- NETWORK & MQTT VARIABLES ----
const char* primarySSID = "vodafone09FEBA";
const char* primaryPass = "NT4qgkds2t4zHx2K";
const char* hotspotSSID = "Nicks's S23 Ultra";
const char* hotspotPass = "Password1";
bool useHotspot = false; 

const char* mqttServer   = "192.168.0.111";
const int   mqttPort     = 1883;
const char* mqttUser     = "mqtt_user";
const char* mqttPass     = "Internet123!!";

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ---- FRIGATE CAMERA CONFIGURATION ----
const String streamBase     = "http://192.168.0.111:1984/api/frame.jpeg?src="; 
const String frigateApiBase = "http://192.168.0.241:5000/api/"; 

// Wildcard subscription topic to capture alerts across ALL cameras dynamically
const String frigateWildcardTopic = "frigate/+/person/state";

const int totalCameras = 6;
String cameraFeeds[totalCameras] = {
  "CamHBDriveZoom", 
  "CamHBGarage", 
  "CamHBPavement", 
  "CamSLDrive", 
  "CamSLGarden", 
  "CamSLPavement"
};
int currentCamIndex = 0;
String currentURL = "";

// ---- GLOBAL STATE FLAGS & TIMERS ----
bool eventsOn = false;       
bool isShowingEvent = false;
uint32_t eventTimer = 0;

// Auto-Hiding UI State Rules
bool uiVisible = false;
uint32_t uiTimer = 0;
const uint32_t uiTimeout = 10000; // Time buttons stay on screen (10s)

// Rolling Console Log Array
String consoleLogs[3] = {"", "", ""};

// ---- UI 3-BUTTON POSITION DEFINITIONS ----
#define BUTTON_Y 195
#define BUTTON_H 40
#define BTN_CAM_X 5
#define BTN_CAM_W 140
#define BTN_WIFI_X 150
#define BTN_WIFI_W 80
#define BTN_EVT_X 235
#define BTN_EVT_W 80

// Forward Declarations
void updateStreamURL();
void connectToWiFi(const char* ssid, const char* password);
void drawUI();
void drawConsole();
void addLog(String msg);
void reconnectMQTT();
bool checkTouch();

// ---- DECODER OUTPUT CORE INTERCEPT ----
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (checkTouch()) return 0; // Abort rendering frame instantly if glass is touched
  if (y < 190) { 
    tft.pushImage(x, y, w, h, bitmap);
  }
  return 1; 
}

// ---- DYNAMIC CONSOLE PRINTER ENGINE ----
void addLog(String msg) {
  Serial.println(msg); 
  consoleLogs[0] = consoleLogs[1];
  consoleLogs[1] = consoleLogs[2];
  consoleLogs[2] = msg;
  
  if (!uiVisible) {
    drawConsole();
  }
}

void drawConsole() {
  tft.drawFastHLine(0, 191, 320, TFT_DARKGREY);
  tft.fillRect(0, 192, 320, 48, TFT_BLACK);
  tft.setTextDatum(TL_DATUM); 
  tft.setTextColor(TFT_GREEN, TFT_BLACK); // Classic terminal matrix layout
  
  tft.drawString(consoleLogs[0], 8, 194, 1);
  tft.drawString(consoleLogs[1], 8, 208, 1);
  tft.drawString(consoleLogs[2], 8, 222, 1);
}

// ---- DYNAMIC MQTT WILDCARD CALLBACK INTERCEPTOR ----
void mqttCallback(char* topic, byte* payload, unsigned length) {
  if (!eventsOn) return; // Completely ignore if the UI event toggle is turned OFF

  String message = "";
  for (int i = 0; i < length; i++) { message += (char)payload[i]; }
  
  String topicStr = String(topic);
  // Verify it's a person detection state and the value is greater than 0
  if (topicStr.startsWith("frigate/") && topicStr.endsWith("/person/state") && message.toInt() > 0) {
    
    // Parse the camera name right out from between the slashes
    int firstSlash = topicStr.indexOf('/');
    int secondSlash = topicStr.indexOf('/', firstSlash + 1);
    String triggeredCamera = topicStr.substring(firstSlash + 1, secondSlash);
    
    addLog("ALERT: Person on " + triggeredCamera);
    
    // Switch URL instantly to look at the triggered camera snapshot
    currentURL = frigateApiBase + triggeredCamera + "/latest.jpg?bbox=1";
    isShowingEvent = true;
    eventTimer = millis(); 
    
    tft.fillRect(0, 0, 320, 190, TFT_BLACK); // Flush active canvas frames
  }
}

void setup() {
  Serial.begin(115200);
  
  tft.init();
  tft.setRotation(1); 
  tft.fillScreen(TFT_BLACK);
  tft.setSwapBytes(true); // Keeps your colors fixed!
  
  tsSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(tsSPI);
  ts.setRotation(1); 

  TJpgDec.setCallback(tft_output);
  TJpgDec.setJpgScale(2); 

  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setCallback(mqttCallback);

  addLog("SYS: Initializing Panel Viewer...");
  updateStreamURL();
  connectToWiFi(primarySSID, primaryPass);
  drawConsole();
}

void loop() {
  checkTouch();

  // Handle UI Menu Disappear Timeout Window
  if (uiVisible && (millis() - uiTimer > uiTimeout)) {
    uiVisible = false;
    drawConsole();
  }

  // Keep background connection to HA broker alive
  if (!mqttClient.connected() && WiFi.status() == WL_CONNECTED) {
    reconnectMQTT();
  }
  mqttClient.loop();

  // Clear locked event frame after 8 seconds of activity
  if (isShowingEvent && (millis() - eventTimer > 8000)) {
    isShowingEvent = false;
    updateStreamURL(); 
    tft.fillRect(0, 0, 320, 190, TFT_BLACK);
    if (uiVisible) drawUI(); else drawConsole();
  }

  // Network Frame Fetch Engine
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(espClient, currentURL);
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
      WiFiClient* stream = http.getStreamPtr();
      static uint8_t jpgBuffer[64 * 1024]; 
      int bytesRead = 0;
      
      uint32_t timeout = millis();
      while (http.connected() && (millis() - timeout < 1500) && bytesRead < sizeof(jpgBuffer)) {
        if (checkTouch()) { http.end(); return; }

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
    } else {
      // If feed stops loading, log the exact HTTP error to screen console every 4 seconds
      static uint32_t lastErrTime = 0;
      if (millis() - lastErrTime > 4000) {
        addLog("HTTP Error: Code " + String(httpCode));
        addLog("Target: " + currentURL);
        lastErrTime = millis();
      }
    }
    http.end();
  }
  delay(10);
}

// ---- INTERACTIVE WAKE / HIDE STATE ENGINE ----
bool checkTouch() {
  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    uint16_t t_x = map(p.x, 200, 3800, 0, 320);
    uint16_t t_y = map(p.y, 240, 3800, 0, 240);
    
    if (!uiVisible) {
      // WAKE STATE: Show the hidden menu buttons on first tap anywhere
      uiVisible = true;
      uiTimer = millis();
      drawUI(); 
      delay(300); // Protection debounce
      return true; 
    } else {
      // Extend visibility window if user is actively touching panel
      uiTimer = millis();
      
      // Look for inputs explicitly within bottom bar grid placement
      if (t_y >= BUTTON_Y && t_y <= (BUTTON_Y + BUTTON_H)) {
        isShowingEvent = false; 
        
        // BUTTON 1: CAMERA MANUALLY CYCLED
        if (t_x >= BTN_CAM_X && t_x <= (BTN_CAM_X + BTN_CAM_W)) {
          currentCamIndex = (currentCamIndex + 1) % totalCameras;
          updateStreamURL();
          addLog("CAM: Cycle -> " + cameraFeeds[currentCamIndex]);
          uiVisible = false; // Hide menu instantly upon click execution
          tft.fillRect(0, 0, 320, 190, TFT_BLACK);
          drawConsole();
          delay(350);
          return true; 
        }
        
        // BUTTON 2: WI-FI NETWORK SWITCHED
        if (t_x >= BTN_WIFI_X && t_x <= (BTN_WIFI_X + BTN_WIFI_W)) {
          useHotspot = !useHotspot;
          uiVisible = false; // Hide menu instantly
          tft.fillRect(0, 0, 320, 190, TFT_BLACK); 
          drawConsole();
          if (useHotspot) connectToWiFi(hotspotSSID, hotspotPass);
          else connectToWiFi(primarySSID, primaryPass);
          delay(350); 
          return true; 
        }

        // BUTTON 3: EVENT AUTO-MONITOR TOGGLED
        if (t_x >= BTN_EVT_X && t_x <= (BTN_EVT_X + BTN_EVT_W)) {
          eventsOn = !eventsOn;
          uiVisible = false; // Hide menu instantly
          addLog(eventsOn ? "MODE: Event Alerts ON" : "MODE: Event Alerts OFF");
          tft.fillRect(0, 0, 320, 190, TFT_BLACK);
          drawConsole();
          delay(350);
          return true;
        }
      }
    }
  }
  return false; 
}

void reconnectMQTT() {
  if (mqttClient.connected()) return;
  
  static uint32_t lastMqttAttempt = 0;
  if (millis() - lastMqttAttempt < 5000) return; // Non-blocking rate limiter
  lastMqttAttempt = millis();
  
  addLog("MQTT: Linking to Broker...");
  String clientId = "CYD_Monitor_Client-";
  clientId += String(random(0xffff), HEX);
  
  if (mqttClient.connect(clientId.c_str(), mqttUser, mqttPass)) {
    addLog("MQTT: Broker Connection Online!");
    mqttClient.subscribe(frigateWildcardTopic.c_str());
    addLog("MQTT: Wildcard Active");
  } else {
    addLog("MQTT Connect Fail, rc=" + String(mqttClient.state()));
  }
}

void updateStreamURL() {
  // TIP: Change or remove the "_sub" suffix string modification below 
  // depending on what your console output reports back for HTTP Codes!
  currentURL = streamBase + cameraFeeds[currentCamIndex] + "_sub";
}

void connectToWiFi(const char* ssid, const char* password) {
  WiFi.disconnect();
  WiFi.begin(ssid, password);
  addLog("WiFi: Connecting to " + String(ssid));
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 15) {
    delay(500);
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    addLog("WiFi: Online! IP: " + WiFi.localIP().toString());
  } else {
    addLog("WiFi: Connection Failed.");
  }
}

void drawUI() {
  tft.drawFastHLine(0, 191, 320, TFT_DARKGREY);
  tft.fillRect(0, 192, 320, 48, TFT_BLACK);
  tft.setTextDatum(MC_DATUM); 

  // 1. Camera Selector Button Layout
  String camLabel = cameraFeeds[currentCamIndex];
  tft.fillRoundRect(BTN_CAM_X, BUTTON_Y, BTN_CAM_W, BUTTON_H, 4, TFT_BLUE);
  tft.drawRoundRect(BTN_CAM_X, BUTTON_Y, BTN_CAM_W, BUTTON_H, 4, TFT_WHITE);
  tft.setTextColor(TFT_WHITE);
  tft.drawString(camLabel, BTN_CAM_X + (BTN_CAM_W / 2), BUTTON_Y + (BUTTON_H / 2), 1);

  // 2. Wi-Fi Selector Button Layout
  uint16_t wifiBtnColor = useHotspot ? TFT_GREEN : TFT_NAVY;
  String wifiLabel = useHotspot ? "Hotspot" : "Home";
  tft.fillRoundRect(BTN_WIFI_X, BUTTON_Y, BTN_WIFI_W, BUTTON_H, 4, wifiBtnColor);
  tft.drawRoundRect(BTN_WIFI_X, BUTTON_Y, BTN_WIFI_W, BUTTON_H, 4, TFT_WHITE);
  tft.drawString(wifiLabel, BTN_WIFI_X + (BTN_WIFI_W / 2), BUTTON_Y + (BUTTON_H / 2), 1);

  // 3. Events Automator Button Layout
  uint16_t evtBtnColor = eventsOn ? TFT_GREEN : TFT_MAROON;
  String evtLabel = eventsOn ? "Evts: ON" : "Evts: OFF";
  tft.fillRoundRect(BTN_EVT_X, BUTTON_Y, BTN_EVT_W, BUTTON_H, 4, evtBtnColor);
  tft.drawRoundRect(BTN_EVT_X, BUTTON_Y, BTN_EVT_W, BUTTON_H, 4, TFT_WHITE);
  tft.drawString(evtLabel, BTN_EVT_X + (BTN_EVT_W / 2), BUTTON_Y + (BUTTON_H / 2), 1);
}