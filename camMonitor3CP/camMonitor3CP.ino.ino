#include <WiFi.h>
#include <HTTPClient.h>
#include <TJpg_Decoder.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <PubSubClient.h>
#include "time.h"

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

WiFiClient mqttNetClient;  
WiFiClient httpNetClient;  
PubSubClient mqttClient(mqttNetClient);

// ---- FRIGATE CAMERA CONFIGURATION ----
const String streamBase     = "http://192.168.0.111:1984/api/frame.jpeg?src="; 
const String frigateApiBase = "http://192.168.0.241:5000/api/"; 
const String frigateEventsTopic = "frigate/events";

const int totalCameras = 7;
String cameraFeeds[totalCameras] = {
  "CamHBDriveZoom", 
  "CamHBGarage", 
  "CamHBPavement", 
  "CamHBExtra", 
  "CamSLDrive", 
  "CamSLGarden", 
  "CamSLPavement"
};
int currentCamIndex = 0;
int oldCamIndex = 0;
String camLabel = "";
String currentURL = "";
String id = "";
String oldId = "";

// ---- GLOBAL STATE FLAGS & TIMERS ----
bool eventsOn = true;       
bool isShowingEvent = false;
bool eventProcessed = false; 
uint32_t eventTimer = 0;

// Auto-Hiding UI State Rules
bool uiVisible = false;
uint32_t uiTimer = 0;
const uint32_t uiTimeout = 10000; 

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
String getTimestamp();

// ---- DECODER OUTPUT CORE INTERCEPT ----
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (checkTouch()) return 0; 
  tft.pushImage(x, y, w, h, bitmap); 
  return 1; 
}

// ---- LIVE NTP TIMESTAMP GENERATOR ----
String getTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "00:00:00 "; 
  }
  char timeStringBuff[15];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%H:%M:%S ", &timeinfo);
  return String(timeStringBuff);
}

// ---- DYNAMIC CONSOLE PRINTER ENGINE ----
void addLog(String msg) {
  String timedMsg = getTimestamp() + msg;
  Serial.println(timedMsg); 
  consoleLogs[0] = consoleLogs[1];
  consoleLogs[1] = consoleLogs[2];
  consoleLogs[2] = timedMsg;
  
  if (!uiVisible) {
    drawConsole();
  }
}

void drawConsole() {
  tft.drawFastHLine(0, 191, 320, TFT_DARKGREY);
  tft.fillRect(0, 192, 320, 48, TFT_BLACK);
  tft.setTextDatum(TL_DATUM); 
  tft.setTextColor(TFT_GREEN, TFT_BLACK); 
  
  tft.drawString(consoleLogs[0], 0, 194, 1);
  tft.drawString(consoleLogs[1], 0, 208, 1);
  tft.drawString(consoleLogs[2], 0, 222, 1);
}

// ---- MQTT DIRECT EVENTS TOPIC PARSER ----
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (!eventsOn) return; 

  if (String(topic) == frigateEventsTopic) {
    String payloadStr = "";
    payloadStr.reserve(length); 
    for (unsigned int i = 0; i < length; i++) {
      payloadStr += (char)payload[i];
    }
    
    if (payloadStr.indexOf("\"type\": \"new\"") != -1 || payloadStr.indexOf("\"type\": \"update\"") != -1) {
      
      int camKey = payloadStr.indexOf("\"camera\": \"");
      String triggeredCamera = "UnknownCam";
      if (camKey != -1) {
        int start = camKey + 11; 
        int end = payloadStr.indexOf("\"", start);
        triggeredCamera = payloadStr.substring(start, end);
      }
      
      int labelKey = payloadStr.indexOf("\"label\": \"");
      String detectionLabel = "object";
      if (labelKey != -1) {
        int start = labelKey + 10; 
        int end = payloadStr.indexOf("\"", start);
        detectionLabel = payloadStr.substring(start, end);
      }

      oldId = id;
      int idKey = payloadStr.indexOf("\"id\": \"");
      String idLabel = "000.000";
      if (idKey != -1) {
        int start = idKey + 7; 
        int end = payloadStr.indexOf("\"", start);
        idLabel = payloadStr.substring(start, end);
        id = idLabel;
      }      
      
      bool matchFound = false;
      for (int i = 0; i < totalCameras; i++) {
        if (cameraFeeds[i] == triggeredCamera) {
          currentCamIndex = i; 
          matchFound = true;
          break;
        }
      }
      
      if (!matchFound) {
        addLog("SYS: No array match for [" + triggeredCamera + "]");
      } else {
        if (oldId != id) {
          addLog(detectionLabel + " on " + triggeredCamera + " " + id);
          
          httpNetClient.stop(); 
          currentURL = frigateApiBase + triggeredCamera + "/latest.jpg?bbox=1";
          
          isShowingEvent = true;
          eventProcessed = false; 
          eventTimer = millis(); 
          tft.fillRect(0, 0, 320, 190, TFT_BLACK); 
        }
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  
  tft.init();
  tft.setRotation(1); 
  tft.fillScreen(TFT_BLACK);
  tft.setSwapBytes(true); 
  
  tsSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(tsSPI);
  ts.setRotation(1); 

  TJpgDec.setCallback(tft_output);

  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(4096); 

  addLog("SYS: Initializing Panel Viewer...");
  updateStreamURL();
  connectToWiFi(primarySSID, primaryPass);
  drawConsole();
}

void loop() {
  checkTouch();

  if (uiVisible && (millis() - uiTimer > uiTimeout)) {
    uiVisible = false;
    drawConsole();
  }

  if (!mqttClient.connected() && WiFi.status() == WL_CONNECTED) {
    reconnectMQTT();
  }
  mqttClient.loop();

  if (isShowingEvent && (millis() - eventTimer > 8000)) {
    isShowingEvent = false;
    eventProcessed = false;
    oldCamIndex = -1;
    updateStreamURL(); 
    tft.fillRect(0, 0, 320, 190, TFT_BLACK);
    if (uiVisible) drawUI(); else drawConsole();
  }

  if (isShowingEvent && eventProcessed) {
    delay(100); 
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    // --- HTTP request with retry and safer handling ---
    const int maxRetries = 2;
    int attempt = 0;
    int httpCode = -99;
    bool success = false;

    while (attempt <= maxRetries && !success) {
      HTTPClient http;
      http.begin(httpNetClient, currentURL); 
      http.setTimeout(10000);        // increased timeout to 10s
      http.setReuse(false);         // avoid reusing stale connections
      // perform GET
      httpCode = http.GET();

      if (httpCode == HTTP_CODE_OK) {
        WiFiClient* stream = http.getStreamPtr();
        // Reduced buffer size to reduce heap pressure
        static uint8_t jpgBuffer[48 * 1024]; 
        int bytesRead = 0;
        
        // Read loop: allow longer total window and only reset timeout when bytes arrive
        uint32_t lastByteMillis = millis();
        const uint32_t totalWindow = 3000; // total read window
        while (http.connected() && (millis() - lastByteMillis < totalWindow) && bytesRead < (int)sizeof(jpgBuffer)) {
          if (checkTouch()) { 
            http.end(); 
            httpNetClient.stop(); 
            return; 
          }

          int availableBytes = stream->available();
          if (availableBytes > 0) {
            int toRead = min(availableBytes, (int)(sizeof(jpgBuffer) - bytesRead));
            int r = stream->readBytes(&jpgBuffer[bytesRead], toRead);
            if (r > 0) {
              bytesRead += r;
              lastByteMillis = millis(); // reset only when we actually read bytes
            } else {
              // no progress, small delay
              delay(1);
            }
          } else {
            delay(1);
          }
        }

        if (bytesRead > 0) {
          // Enforce top viewport limits cleanly
          tft.setViewport(0, 0, 320, 190);
          
          // Centralized evaluation block using the exact camera resolution maps
          if (cameraFeeds[currentCamIndex].indexOf("Drive") != -1) {
            // Drive Cameras (896x512 / 2 = 448x256) -> Center-crop inside viewport box
            TJpgDec.setJpgScale(2);
            TJpgDec.drawJpg(-64, -33, jpgBuffer, bytesRead);
          } else {
            // Standard Cameras (640x360 / 2 = 320x180) -> Vertical align center (pads 5px top/bottom)
            TJpgDec.setJpgScale(2);
            TJpgDec.drawJpg(0, 5, jpgBuffer, bytesRead);
          }
          
          tft.resetViewport(); 
          
          if (isShowingEvent) {
            eventProcessed = true;
          }
          success = true;
        } else {
          // No bytes read despite OK code
          addLog("HTTP: OK but no bytes read");
        }

        http.end();
      } else {
        // Negative codes indicate client errors/timeouts
        if (httpCode < 0) {
          addLog("HTTP Error (client): " + String(httpCode));
        } else {
          addLog("HTTP Error: " + String(httpCode));
        }
        http.end();
        httpNetClient.stop();
        // small backoff before retry
        delay(200 + attempt * 200);
      }

      attempt++;
    } // end retry loop

    if (!success) {
      // If repeated failures, try reconnecting WiFi as a recovery step
      static uint32_t lastRecovery = 0;
      if (millis() - lastRecovery > 15000) {
        addLog("HTTP: repeated failures, restarting WiFi");
        lastRecovery = millis();
        WiFi.disconnect(true);
        delay(500);
        if (useHotspot) connectToWiFi(hotspotSSID, hotspotPass);
        else connectToWiFi(primarySSID, primaryPass);
      }
    }
  }
  delay(10);
}

bool checkTouch() {
  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    uint16_t t_x = map(p.x, 200, 3800, 0, 320);
    uint16_t t_y = map(p.y, 240, 3800, 0, 240);
    
    if (!uiVisible) {
      uiVisible = true;
      uiTimer = millis();
      drawUI(); 
      delay(300); 
      return true; 
    } else {
      uiTimer = millis();
      if (t_y >= BUTTON_Y && t_y <= (BUTTON_Y + BUTTON_H)) {
        isShowingEvent = false; 
        eventProcessed = false;
        
        if (t_x >= BTN_CAM_X && t_x <= (BTN_CAM_X + BTN_CAM_W)) {
          currentCamIndex = (currentCamIndex + 1) % totalCameras;
          updateStreamURL();
          addLog("CAM: " + cameraFeeds[currentCamIndex]);
          uiVisible = false; 
          tft.fillRect(0, 0, 320, 190, TFT_BLACK);
          drawConsole();
          delay(350);
          return true; 
        }
        
        if (t_x >= BTN_WIFI_X && t_x <= (BTN_WIFI_X + BTN_WIFI_W)) {
          useHotspot = !useHotspot;
          uiVisible = false;
          tft.fillRect(0, 0, 320, 190, TFT_BLACK); 
          drawConsole();
          if (useHotspot) connectToWiFi(hotspotSSID, hotspotPass);
          else connectToWiFi(primarySSID, primaryPass);
          delay(350); 
          return true; 
        }

        if (t_x >= BTN_EVT_X && t_x <= (BTN_EVT_X + BTN_EVT_W)) {
          eventsOn = !eventsOn;
          uiVisible = false;
          addLog(eventsOn ? "EVENTS MODE: ENABLED" : "EVENTS MODE: DISABLED");
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
  if (millis() - lastMqttAttempt < 5000) return; 
  lastMqttAttempt = millis();
  
  addLog("MQTT: Connecting...");
  String clientId = "CYD_Monitor_Client-";
  clientId += String(random(0xffff), HEX);
  
  if (mqttClient.connect(clientId.c_str(), mqttUser, mqttPass)) {
    addLog("MQTT: Broker Link Active!");
    mqttClient.subscribe(frigateEventsTopic.c_str());
  } else {
    addLog("MQTT Fail, rc=" + String(mqttClient.state()));
  }
}

void updateStreamURL() {
  httpNetClient.stop(); 
  // Unified endpoint path mapping format
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
    addLog("WiFi: Connected!");
    
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    setenv("TZ", "GMT0BST,M3.5.0/1,M10.5.0", 1);
    tzset();
    
    delay(500); 
    addLog("CLOCK: Time synchronized.");
  } else {
    addLog("WiFi: Failed.");
  }
}

void drawUI() {
  tft.drawFastHLine(0, 191, 320, TFT_DARKGREY);
  tft.fillRect(0, 192, 320, 48, TFT_BLACK);
  tft.setTextDatum(MC_DATUM); 

  camLabel = cameraFeeds[currentCamIndex];
  tft.fillRoundRect(BTN_CAM_X, BUTTON_Y, BTN_CAM_W, BUTTON_H, 4, TFT_BLUE);
  tft.drawRoundRect(BTN_CAM_X, BUTTON_Y, BTN_CAM_W, BUTTON_H, 4, TFT_WHITE);
  tft.setTextColor(TFT_WHITE);
  tft.drawString(camLabel, BTN_CAM_X + (BTN_CAM_W / 2), BUTTON_Y + (BUTTON_H / 2), 1);

  uint16_t wifiBtnColor = useHotspot ? TFT_GREEN : TFT_NAVY;
  String wifiLabel = useHotspot ? "Hotspot" : "Home";
  tft.fillRoundRect(BTN_WIFI_X, BUTTON_Y, BTN_WIFI_W, BUTTON_H, 4, wifiBtnColor);
  tft.drawRoundRect(BTN_WIFI_X, BUTTON_Y, BTN_WIFI_W, BUTTON_H, 4, TFT_WHITE);
  tft.drawString(wifiLabel, BTN_WIFI_X + (BTN_WIFI_W / 2), BUTTON_Y + (BUTTON_H / 2), 1);

  uint16_t evtBtnColor = eventsOn ? TFT_GREEN : TFT_MAROON;
  String evtLabel = eventsOn ? "Evts: ON" : "Evts: OFF";
  tft.fillRoundRect(BTN_EVT_X, BUTTON_Y, BTN_EVT_W, BUTTON_H, 4, evtBtnColor);
  tft.drawRoundRect(BTN_EVT_X, BUTTON_Y, BTN_EVT_W, BUTTON_H, 4, TFT_WHITE);
  tft.drawString(evtLabel, BTN_EVT_X + (BTN_EVT_W / 2), BUTTON_Y + (BUTTON_H / 2), 1);
}
