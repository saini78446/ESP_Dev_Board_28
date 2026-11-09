//C:\Users\<USERNAME>\Documents\Arduino\libraries\TFT_eSPI\User_Setup.h

#define ST7789_DRIVER
#define TFT_INVERSION_ON

#define TFT_WIDTH  240
#define TFT_HEIGHT 320

#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC   2
#define TFT_RST  -1

#define TFT_BL   21
#define TFT_BACKLIGHT_ON HIGH

#define TFT_RGB_ORDER TFT_BGR
#define TFT_INVERSION_OFF

#define SPI_FREQUENCY 40000000

#define LOAD_GLCD    // font 1
#define LOAD_FONT2   // small
#define LOAD_FONT4   // medium
#define LOAD_FONT6   // large, digits only
#define LOAD_FONT7   // 7-segment, digits only (the big UK time)
#define LOAD_FONT8   // very large, digits only
#define SMOOTH_FONT