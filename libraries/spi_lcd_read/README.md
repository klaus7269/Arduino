![](images/displays.png)

# spi_lcd_read

**Tiny simple library to read data from registers on this class of cheap SPI LCD controllers (ILI9341, ST7789, etc). Can be used for identifying hardware before the actual display driver is initialized. No dependencies other than Arduino's `SPI.h`.**

## Introduction

I was writing a library to use the LVGL on a family of very cheap boards with an ESP32 and a small LCD touch screen. Since I wanted user code wrtten using this library to 'just work', it had to detect what type of touch screen, but also what display controller was on there. (Different display controllers are used on different versions of the boards.)

The example with this library reads some common registers and displays the contents.

```
RDDID (0x04): seq: 00 00 00 - idx: 00 00 00 
RDDST (0x09): seq: 00 61 00 00 - idx: 00 00 00 00 
0x0A (0x0a): 08
0x0B (0x0b): 00
0x0C (0x0c): 06
0x0D (0x0d): 00
0x0E (0x0e): 00
0x0F (0x0f): 00
ID4 (0xd3): seq: 00 00 00 00 - idx: 41 00 93 41 
RDID1 (0xda): 00
RDID2 (0xdb): 00
RDID3 (0xdc): 00
```

As you can see above, the multi-byte registers are read using two different methods, producing different results. But let me write down some things that you need to know in an order that makes sense.

* This libary works **only for displays connected using SPI**. The same controllers can usually also be used in parallel mode, this library doesn't do that.

* **You need the MISO wire connected.** Many board designers figured they only ever need to send pixels to the display and they did not hook up the wire for the display to talk back to the processor.

* **It needs to be wired for "4-Wire 8-bit protocol".** You need the mode where there's a clock (SCK), data to the processor (MISO), data from the processor (MOSI) and a $\textsf{D}$/$\overline{\textsf{C}}$ line that indicates whether the current byte is a command or data. And a Chip Select (CS) to tell the display that we're talking to it. This is usally how it's set up, just be aware that there's a 9-bit mode with no $\textsf{D}$/$\overline{\textsf{C}}$ line and a way to do MISO and MOSI on same line, this code doesn't work with these modes.

* Single byte reads are really simple: pull low CS and DC, send the byte for the register you want to read and then pull up DC and read a byte.

* Multi-byte reads are either done "**sequentially**" by simply reading more bytes after the command (but after one dummy bit) or "**indexed**" by sending the command`0xD9` and an 'index' byte indicating which byte of the next register you want to read. Then on the next command, read the register as above to get the indexed byte.

  * As you can see in the sample output above, the `RDDST` register on the ILI9341 needs to be read sequentially, while `ID4` needs to be read indexed.

* Some display libraries, e.g. TFT_eSPI, implement reading multi-byte registers from the controller, but in TFT_eSPI's case only using indexed reading.

## Usage

Include this library, it will include `SPI.h` for you. Create an instance of Arduino SPI, possibly indicating which hardware device in the processor to use. Then create an instance of `SPI_LCD_READ`. On the ESP32 this would be:

```cpp
#include <spi_lcd_read.h>

SPIClass spi = SPIClass(VSPI);
SPI_LCD_READ slr;
```

In your `setup()`, initialize the SPI port as follows:

```cpp
slr.begin(&spi, LCD_SCK, LCD_MISO, LCD_MOSI, LCD_CS, LCD_DC);
```

(Assuming these pin names are defined as GPIO numbers using `#define`)

Now simply read bytes or buffers using any of the member functions of your `slr` instance.

```cpp
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
```

## Demo

`read_registers.ino` in the examples directory does exactly this and then prints to the serial port a number of registers these controllers seem to share. You might need to change the GPIO pins and how you create your SPI instance depending on your hardware. As you can see it reads the multi-byte registers using both the indexed and sequential method. 
