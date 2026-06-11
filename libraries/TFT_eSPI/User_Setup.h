#define USER_SETUP_INFO "ESP32_CYD_2432S028R"

// Driver
#define ILI9341_2_DRIVER
//#define ILI9341_DRIVER

// Pins for the CYD Display
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1  // Set to -1 if connected to ESP32 reset line
#define TFT_BL   21  // Backlight control pin

#define TFT_BACKLIGHT_ON HIGH


// Fonts to enable (useful for debugging text overlays)
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF

#define SPI_FREQUENCY  55000000 // Stable speed for the CYD display


#define SPI_READ_FREQUENCY  20000000

//#define TOUCH_CS 33
//#define SPI_TOUCH_FREQUENCY 2500000