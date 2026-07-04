#include <TFT_eSPI.h> // Hardware-specific library for the CYD
#include <ArduinoJson.h>

TFT_eSPI tft = TFT_eSPI();       // Invoke custom library

// Define scanning range to match the Python script
const int START_FREQ = 380;
const int END_FREQ = 385;
const int NUM_CHANNELS = (END_FREQ - START_FREQ) + 1;

// Keep track of previous values to prevent screen flicker (only redraw when changed)
int prev_percentages[NUM_CHANNELS] = {0};

void setup() {
  // Initialize Serial with the same baud rate as the Python script
  Serial.begin(115200);
  
  // Initialize the CYD Screen
  tft.init();
  tft.setRotation(1); // Landscape mode
  tft.fillScreen(TFT_BLACK);
  
  // Draw Static UI Frame
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("TETRA Uplink Monitor", 10, 10);
  
  tft.setTextSize(1);
  tft.drawString("Range: 380 - 385 MHz", 10, 32);
  
  // Draw a simple bottom axis line for the graph
  // Screen is 320x240. We'll use Y=200 as the baseline.
  tft.drawFastHLine(10, 200, 300, TFT_SILVER);
  
  // Draw Frequency Labels below the axis line
  int bar_width = 300 / NUM_CHANNELS;
  for (int i = 0; i < NUM_CHANNELS; i++) {
    int x_pos = 10 + (i * bar_width) + (bar_width / 4);
    tft.drawNumber(START_FREQ + i, x_pos, 210);
  }
}

void loop() {
  // Check if Python has sent a full line (ending in '\n')
  if (Serial.available() > 0) {
    String json_payload = Serial.readStringUntil('\n');
    
    // Allocate JSON Document space
    // StaticJsonDocument<256> handles small strings perfectly
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, json_payload);
    
    // If parsing succeeds, update the graph bars
    if (!error) {
      int bar_width = 300 / NUM_CHANNELS;
      int max_bar_height = 140; // Pixels high from the Y=200 baseline
      
      for (int i = 0; i < NUM_CHANNELS; i++) {
        // Look up the frequency key in the JSON object (e.g., doc["380"])
        String freq_key = String(START_FREQ + i);
        int pct = doc[freq_key] | 0; // Default to 0 if key missing
        
        // Map 0-100% to pixel height
        int bar_height = map(pct, 0, 100, 0, max_bar_height);
        int prev_bar_height = map(prev_percentages[i], 0, 100, 0, max_bar_height);
        
        int x_pos = 10 + (i * bar_width) + 5;
        int bar_w = bar_width - 10;
        
        // Only redraw if the value actually changed (stops the screen from flickering)
        if (pct != prev_percentages[i]) {
          // Clear the old bar by drawing a black rectangle over its previous footprint
          tft.fillRect(x_pos, 200 - max_bar_height, bar_w, max_bar_height, TFT_BLACK);
          
          // Determine bar color based on strength
          uint16_t bar_color = TFT_BLUE;
          if (pct > 75) {
            bar_color = TFT_RED;     // Active transmission close by
          } else if (pct > 45) {
            bar_color = TFT_ORANGE;   // Normal background static/weak signal
          } else if (pct > 25) {
            bar_color = TFT_YELLOW;   // Normal background static/weak signal
          }
          
          // Draw the new bar rising up from the baseline
          tft.fillRect(x_pos, 200 - bar_height, bar_w, bar_height, bar_color);
          
          // Save the value for the next loop comparison
          prev_percentages[i] = pct;
        }
      }
    }
  }
}