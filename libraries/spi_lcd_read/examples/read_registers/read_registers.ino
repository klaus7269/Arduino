#include <spi_lcd_read.h>

// Define your display SPI pins
// (These are for the Cheap Yellow Display family of boards)
#define LCD_DC    2  // Data / Command
#define LCD_SCK  14  // SPI clock
#define LCD_MISO 12  // SPI MISO
#define LCD_MOSI 13  // SPI MOSI
#define LCD_CS   15  // LCD chip select

SPIClass spi = SPIClass(VSPI);
SPI_LCD_READ slr;

void setup() {
  Serial.begin(115200);

  slr.begin(&spi, LCD_SCK, LCD_MISO, LCD_MOSI, LCD_CS, LCD_DC);
  
  diag_reg("RDDID", 0x04, 3);
  diag_reg("RDDST", 0x09, 4);
  diag_reg("0x0A",  0x0A, 1);
  diag_reg("0x0B",  0x0B, 1);
  diag_reg("0x0C",  0x0C, 1);
  diag_reg("0x0D",  0x0D, 1);
  diag_reg("0x0E",  0x0E, 1);
  diag_reg("0x0F",  0x0F, 1);
  diag_reg("ID4",   0xD3, 4);
  diag_reg("RDID1", 0xDA, 1);
  diag_reg("RDID2", 0xDB, 1);
  diag_reg("RDID3", 0xDC, 1);

}

void loop() {

}

void diag_reg(char * name, uint8_t reg, uint8_t num) {
  uint8_t buffer[16];
  Serial.printf("%s (0x%02x):", name, reg);
  if (num == 1) {
    Serial.printf(" %02x", slr.readByte(reg));
  } else {
    Serial.print(" seq: ");
    slr.readBufferSeq(reg, buffer, num);
    for (int i = 0; i < num; i++) {
      Serial.printf("%02x ", buffer[i]);
    }
    Serial.print("- idx: ");
    slr.readBufferIndex(reg, buffer, num);
    for (int i = 0; i < num; i++) {
      Serial.printf("%02x ", buffer[i]);
    }
  }
  Serial.println();
}
