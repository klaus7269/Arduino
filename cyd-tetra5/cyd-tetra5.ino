#include <TFT_eSPI.h> // Hardware-specific library for the CYD
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>

TFT_eSPI tft = TFT_eSPI(); 

// --- 1. Spectrum Graph Constants ---
const int START_FREQ = 380;
const int END_FREQ = 385;
const int NUM_CHANNELS = (END_FREQ - START_FREQ) + 1;
const int BASELINE_Y = 190; 
const int MAX_BAR_H = 130;  

// --- 2. Advanced Signal Tracking Data ---
int prev_percentages[NUM_CHANNELS] = {0}; 
int peak_pixels[NUM_CHANNELS] = {0}; 
int live_percentages[NUM_CHANNELS] = {0}; // Holds our new raw byte values

unsigned long lastPeakFallTime = 0;
const int PEAK_FALL_DELAY_MS = 3000; 

// --- 3. BLE Configuration ---
static BLEUUID serviceUUID("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
static BLEUUID charUUID   ("6E400002-B5A3-F393-E0A9-E50E24DCCA9E");

static boolean doConnect = false;
static boolean connected = false;
static boolean startScan = true; 

static BLERemoteCharacteristic* pRemoteCharacteristic;
static BLEAdvertisedDevice* myDevice;

volatile bool newBLEDataAvailable = false;

// --- 4. GRAPHICS RENDERING FUNCTIONS ---

void updateStatusIcon(bool isConnected) {
  if (isConnected) {
    tft.fillRect(260, 10, 50, 20, TFT_GREEN);
    tft.setTextColor(TFT_BLACK, TFT_GREEN);
    tft.setTextSize(1);
    tft.drawString(" RPI ", 272, 16);
  } else {
    tft.fillRect(260, 10, 50, 20, TFT_RED);
    tft.setTextColor(TFT_WHITE, TFT_RED);
    tft.setTextSize(1);
    tft.drawString(" SCAN ", 269, 16);
  }
}

void runPeakDecayEngine() {
  if (millis() - lastPeakFallTime < PEAK_FALL_DELAY_MS) return;
  lastPeakFallTime = millis();
  
  int bar_width = 300 / NUM_CHANNELS;
  for (int i = 0; i < NUM_CHANNELS; i++) {
    if (peak_pixels[i] >= BASELINE_Y - 4) continue; 
    
    int x_pos = 10 + (i * bar_width) + 5;
    int bar_w = bar_width - 10;
    
    tft.fillRect(x_pos, peak_pixels[i], bar_w, 4, TFT_BLACK);
    peak_pixels[i]++; 
    
    int currentSignalH = map(prev_percentages[i], 0, 100, 0, MAX_BAR_H);
    int currentSignalTopY = BASELINE_Y - currentSignalH;
    if (peak_pixels[i] > currentSignalTopY - 4) {
        peak_pixels[i] = currentSignalTopY - 4;
    }

    tft.fillRect(x_pos, peak_pixels[i], bar_w, 4, TFT_RED);
  }
}

void drawStaticUI() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("TETRA RF Monitor", 10, 10);
  
  tft.setTextSize(1);
  tft.setTextColor(TFT_SILVER, TFT_BLACK);
  tft.drawString("Range: 380-385 MHz", 10, 32);
  
  tft.drawFastHLine(10, BASELINE_Y, 300, TFT_SILVER);
  
  int bar_width = 300 / NUM_CHANNELS;
  for (int i = 0; i < NUM_CHANNELS; i++) {
    int x_pos = 10 + (i * bar_width) + (bar_width / 4);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawNumber(START_FREQ + i, x_pos - 4, BASELINE_Y + 8);
  }
  updateStatusIcon(false);
}

// --- 5. BLE LOGIC HANDLERS ---

// Instantly captures the 6 incoming bytes
static void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, 
                           uint8_t* pData, size_t length, bool isNotify) {
  if (length >= NUM_CHANNELS) {
    for (int i = 0; i < NUM_CHANNELS; i++) {
      live_percentages[i] = pData[i];
    }
    newBLEDataAvailable = true; 
  }
}

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) { connected = true; }
  void onDisconnect(BLEClient* pclient) { 
    connected = false; 
    doConnect = false;
    updateStatusIcon(false);
    startScan = true; 
  }
};

bool connectToServer() {
    BLEClient* pClient  = BLEDevice::createClient();
    pClient->setClientCallbacks(new MyClientCallback());
    pClient->connect(myDevice);
    
    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) { pClient->disconnect(); return false; }
    
    pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
    if (pRemoteCharacteristic == nullptr) { pClient->disconnect(); return false; }

    if(pRemoteCharacteristic->canNotify()) {
      pRemoteCharacteristic->registerForNotify(notifyCallback);
      
      BLERemoteDescriptor* pBufDesc = pRemoteCharacteristic->getDescriptor(BLEUUID((uint16_t)0x2902));
      if (pBufDesc != nullptr) {
          uint8_t val[] = {0x01, 0x00};
          pBufDesc->writeValue(val, 2, true);
      }
    }
    return true;
}

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceUUID)) {
      BLEDevice::getScan()->stop();
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
    }
  }
};

// --- 6. CORE ENGINE LOOPS ---

void setup() {
  Serial.begin(115200);
  
  tft.init();
  tft.setRotation(1); 
  drawStaticUI();

  for (int i = 0; i < NUM_CHANNELS; i++) {
      peak_pixels[i] = BASELINE_Y - 4;
  }

  BLEDevice::init("CYD-Spectrum-Graph");
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349); 
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true); 
}

void loop() {
  
  // A. Auto-Reconnect Engine
  if (doConnect == true) {
    if (connectToServer()) {
      updateStatusIcon(true);
    } else {
      startScan = true; 
    }
    doConnect = false;
  }

  // B. Safe Background Scanner
  if (startScan) {
    startScan = false;
    BLEDevice::getScan()->clearResults(); 
    BLEDevice::getScan()->start(0, false); 
  }

  // C. Run background peak decay drop engine
  runPeakDecayEngine();

  // D. Main Rendering Update Loop
  if (newBLEDataAvailable) {
    newBLEDataAvailable = false; 
    
    int bar_width = 300 / NUM_CHANNELS;
    
    for (int i = 0; i < NUM_CHANNELS; i++) {
      int pct = live_percentages[i]; // Grab the raw byte value mapped directly to this channel
      
      int x_pos = 10 + (i * bar_width) + 5;
      int bar_w = bar_width - 10;
      int bar_height = map(pct, 0, 100, 0, MAX_BAR_H);
      int currentTopY = BASELINE_Y - bar_height;
      
      if (pct != prev_percentages[i]) {
        tft.fillRect(x_pos, BASELINE_Y - MAX_BAR_H, bar_w, MAX_BAR_H, TFT_BLACK);
        
        uint16_t bar_color = TFT_BLUE;
        if (pct > 75)       bar_color = 0x3A0; 
        else if (pct > 45)  bar_color = 0x12C; 
        else if (pct > 25)  bar_color = 0x10A; 
        
        tft.fillRect(x_pos, currentTopY, bar_w, bar_height, bar_color);
        
        int label_x = 10 + (i * bar_width) + (bar_width / 4);
        tft.fillRect(label_x - 6, BASELINE_Y + 22, 32, 12, TFT_BLACK); 
        
        tft.setTextSize(1);
        if (pct > 35) {
          tft.setTextColor(TFT_GREEN, TFT_BLACK); 
        } else {
          tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
        }
        tft.drawNumber(pct, label_x - 2, BASELINE_Y + 22);
        
        prev_percentages[i] = pct;
      }
      
      if (peak_pixels[i] != 0) {
          tft.fillRect(x_pos, peak_pixels[i], bar_w, 4, TFT_BLACK);
      }

      if (bar_height > (BASELINE_Y - peak_pixels[i])) {
        peak_pixels[i] = currentTopY - 4; 
      }
      
      if (peak_pixels[i] > BASELINE_Y - 4) {
          peak_pixels[i] = BASELINE_Y - 4;
      }
      
      tft.fillRect(x_pos, peak_pixels[i], bar_w, 4, TFT_RED);
    }
  }
}