#pragma once
// ============================================================================
//  config.h  -  كل ما قد تحتاج تغييره في العتاد (Pins & options)
// ============================================================================

// ---- TFT ST7735S 1.8" 128x160 (SPI) -----------------------------------------
//  SCK  -> GPIO18 (VSPI SCK)    SDA/MOSI -> GPIO23 (VSPI MOSI)   [fixed by the hardware SPI]
#define TFT_CS    5
#define TFT_DC    21      // pin "A0" / "DC" on the module
#define TFT_RST   22
#define BL_PIN    -1      // -1 = backlight (LED pin) wired directly to 3.3V. Otherwise a GPIO driving it via transistor
#define BL_INVERT 0       // 1 if the backlight transistor is PNP (LOW = on)

#define TAB_TYPE    INITR_BLACKTAB   // try INITR_GREENTAB or INITR_REDTAB if the picture is shifted / has a colored edge
#define TFT_INVERT  0                // 1 if colors look inverted (black<->white)
#define SWAP_RB     0                // 1 if red and blue are swapped (BGR panel)
#define ROTATION    1                // 1 or 3 (landscape, flip 180 degrees)
#define SPI_HZ      16000000

// ---- Touch sensor TTP223 -----------------------------------------------------
#define TOUCH_PIN        27
#define TOUCH_ACTIVE_HIGH 1          // TTP223 default: HIGH when touched

// ---- Buzzer (active or passive) through an NPN transistor (S8050 + 1k) -------
#define BUZZ_PIN   25

// ---- Wi-Fi access point of the device (used by the QR code / phone setup) ----
#define AP_SSID        "AdhanClock"
#define AP_PASS_DEFAULT "adhan1234"   // min 8 chars. Can be changed from the settings page
#define MDNS_NAME      "adhan"        // http://adhan.local/

// ---- Behaviour ---------------------------------------------------------------
#define SPLASH_MS      4500
#define LONG_PRESS_MS  800
#define VLONG_PRESS_MS 2000
#define XLONG_PRESS_MS 6000           // (on the QR screen) factory reset
#define DUA_SLIDE_MS   20000          // auto-advance of the adhkar screen
