#ifndef _SPI_LCD_READ_h_
#define _SPI_LCD_READ_h_

#include <SPI.h>

class SPI_LCD_READ {
  public:
  
    // specify display wiring and frequency on supplied Arduino SPI instance
    void begin(SPIClass * spi_, int sck, int miso, int mosi, int cs_, int dc_, long freq_ = 10000000);

    // read a single byte return from a register
    uint8_t readByte(uint8_t reg);

    // read a series of bytes sequentially
    void readBufferSeq(uint8_t reg, uint8_t * buf, uint8_t len);

    // read a single byte at the specified register and index
    uint8_t readByteIndex(uint8_t reg, uint8_t index);

    // read a series of bytes at the specified register using indexed reading
    void readBufferIndex(uint8_t reg, uint8_t * buf, uint8_t len);

    // free resources used by SPI
    void end();

  private:
    int dc, cs;
    SPIClass * spi;
    long freq;
};

#endif
