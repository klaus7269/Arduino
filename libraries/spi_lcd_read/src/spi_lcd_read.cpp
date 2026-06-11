#include <spi_lcd_read.h>
#include <Arduino.h>

// specify display wiring and frequency on supplied Arduino SPI instance
void SPI_LCD_READ::begin(SPIClass * spi_, int sck, int miso, int mosi, int cs_, int dc_, long freq_) {
  spi = spi_;
  cs = cs_;
  dc = dc_;
  freq = freq_;
  spi->begin(sck, miso, mosi, cs);
  pinMode(dc, OUTPUT);
  pinMode(cs, OUTPUT);
}

// read a single byte return from a register
uint8_t SPI_LCD_READ::readByte(uint8_t reg) {
  uint8_t ret;
  readBufferSeq(reg, &ret, 1);
  return ret;
}

// read a series of bytes sequentially
// (some multi-byte registers on some displays need to be read using indexed reading)
void SPI_LCD_READ::readBufferSeq(uint8_t reg, uint8_t * buf, uint8_t len) {
  spi->beginTransaction(SPISettings(freq, MSBFIRST, SPI_MODE0));
  digitalWrite(cs, LOW);
  digitalWrite(dc, LOW);
  spi->transfer(reg);
  digitalWrite(dc, HIGH);
  if (len == 1) {
    // single byte is read directly, no shifting needed
    buf[0] = spi->transfer(0x00);
  } else {
    // Only when reading multiple bytes, one bit is skipped
    // so we make a temporary buffer and read one extra byte
    uint8_t tmpbuf[len + 1];
    for (int n = 0; n < len + 1; n++) {
      tmpbuf[n] = spi->transfer(0x00);
    }
    // and then shift everything left one bit
    for (int n = 0; n < len; n++) {
      buf[n] = (tmpbuf[n] << 1) + (tmpbuf[n + 1] & 0x80 ? 1 : 0);
    }
  }
  digitalWrite(cs, HIGH);
  spi->endTransaction();
}

// read a single byte at the specified register and index
// (only some registers on some displays support this)
uint8_t SPI_LCD_READ::readByteIndex(uint8_t reg, uint8_t index) {
  spi->beginTransaction(SPISettings(freq, MSBFIRST, SPI_MODE0));
  // first the 0xD9 command with the index
  digitalWrite(cs, LOW);
  digitalWrite(dc, LOW);
  spi->transfer(0xD9);
  digitalWrite(dc, HIGH);
  spi->transfer(0x10 + (index & 0x0f));
  digitalWrite(cs, HIGH);
  // then read from register
  digitalWrite(cs, LOW);
  digitalWrite(dc, LOW);
  spi->transfer(reg);
  digitalWrite(dc, HIGH);
  uint8_t ret = spi->transfer(0x00);
  digitalWrite(cs, HIGH);
  spi->endTransaction();
  return ret;
}

// read a series of bytes, one at a time using indexed reading
void SPI_LCD_READ::readBufferIndex(uint8_t reg, uint8_t * buf, uint8_t len) {
  for (int n = 0; n < len; n++) {
    buf[n] = readByteIndex(reg, n);
  }
}

// free resources used by SPI
void SPI_LCD_READ::end() {
  spi->end();
}
