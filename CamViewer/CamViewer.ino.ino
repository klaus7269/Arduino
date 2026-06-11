#include <WiFi.h>
#include <HTTPClient.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>


/*  
http://192.168.0.111:1984/api/frame.jpeg?src=CamHBGarage_sub  works
http://100.95.160.37:1984/api/frame.jpeg?src=CamSLDrive_sub   fails


*/

// --- UPDATE THESE ---
const char* ssid = "vodafone09FEBA";
const char* password = "NT4qgkds2t4zHx2K";
// The IP of the server running go2rtc (e.g., your Home Assistant IP)
const char* streamUrl = "http://192.168.0.111:1984/api/frame.jpeg?src=CamHBDriveZoom_sub";

TFT_eSPI tft = TFT_eSPI();

// This function is called by the TJpg_Decoder to draw the image to the TFT
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (y >= tft.height()) return 0; // Stop drawing if we go off screen
  tft.pushImage(x, y, w, h, bitmap);
  return 1;
}

void setup() {
  Serial.begin(115200);
 
  // Initialize Display
  tft.begin();
  tft.setRotation(1); // Landscape mode (320x240)
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Connecting to WiFi...", 10, 10, 2);

  // Connect to WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  tft.fillScreen(TFT_BLACK);
  tft.drawString("WiFi Connected!", 10, 10, 2);

  // Setup JPEG Decoder
  // If your sub-stream is 640x480, set this to 2 to scale it down to 320x240
  // If your sub-stream is 320x240, set this to 1
  TJpgDec.setJpgScale(2);
  TJpgDec.setSwapBytes(true); // Fixes color inversion on TFTs
  TJpgDec.setCallback(tft_output);
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(streamUrl);
   
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
      int len = http.getSize();
      uint8_t* buff = (uint8_t*)malloc(len); // Allocate memory for the image
     
      if (buff) {
        WiFiClient * stream = http.getStreamPtr();
        size_t size = stream->readBytes(buff, len);
       
        // Decode and draw the JPEG to the screen
        TJpgDec.drawJpg(0, 0, buff, size);
        free(buff); // Always free the memory!
      } else {
        Serial.println("Not enough memory for image buffer");
      }
    } else {
      Serial.printf("HTTP GET failed, error: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end();
  }
 
  // Fetch a new frame every 200ms (~5 frames per second)
  // Adjust this depending on your Wi-Fi strength so the ESP32 doesn't crash
  delay(200);
}