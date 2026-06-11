#include <Wire.h>
#include <SPI.h>

#define XPT2046_CS 33

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  Serial.println("\n--- CYD Hardware Diagnostic Scan ---");

  // 1. Scan for I2C Chips (Capacitive Screen Check)
  Wire.begin(21, 22); // Standard I2C pins for some CYD variants
  Serial.println("Scanning I2C bus...");
  byte error, address;
  int nDevices = 0;
  for (address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    if (error == 0) {
      Serial.print("-> Found I2C device at address 0x");
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
      nDevices++;
    }
  }
  if (nDevices == 0) Serial.println("No I2C devices found.");

  // 2. Test SPI Touch Chip (Resistive Screen Check)
  pinMode(XPT2046_CS, OUTPUT);
  digitalWrite(XPT2046_CS, LOW);
  
  SPI.begin(14, 12, 13, XPT2046_CS); // SCLK, MISO, MOSI, CS
  SPI.beginTransaction(SPISettings(2500000, MSBFIRST, SPI_MODE0));
  
  // Send a command to read raw X coordinate
  SPI.transfer(0x90); 
  uint16_t msb = SPI.transfer(0x00);
  uint16_t lsb = SPI.transfer(0x00);
  SPI.endTransaction();
  digitalWrite(XPT2046_CS, HIGH);

  uint16_t rawResult = (msb << 5) | (lsb >> 3);
  Serial.print("Raw SPI Touch Chip response: ");
  Serial.println(rawResult);
  Serial.println("------------------------------------");
}

void loop() {
  // Nothing here, just running setup once
}