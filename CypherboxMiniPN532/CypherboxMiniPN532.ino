// ============================================================
// CYPHER PN532 — Cypherbox Mini profile (2026)
// ESP32-C3 XIAO/SuperMini + PN532 I2C + SSD1306 128x64 + SPI SD
//
// Hardware profile:
//   I2C SDA=GPIO8, SCL=GPIO9, 100 kHz
//   PN532 IRQ/RESET are not connected; set module DIP switches to I2C mode.
//   SD SPI SCK=GPIO4, MISO=GPIO5, MOSI=GPIO6, CS=GPIO10
//   Buttons are active-low with pullups: UP=GPIO1, DOWN=GPIO2, SELECT=GPIO3
// ============================================================

#define APP_DISPLAY_NAME "CYPHERBOX NFC"

#define PN532_IRQ    (-1)
#define PN532_RESET  (-1)

#define I2C_SDA_PIN  8
#define I2C_SCL_PIN  9
#define I2C_CLOCK_HZ 100000

#define SD_CS    10
#define SD_MOSI   6
#define SD_MISO   5
#define SD_SCK    4

#define BUTTON_UP      1
#define BUTTON_DOWN    2
#define BUTTON_SELECT  3
#define BUTTON_NONE   -1

#include <Wire.h>
#include <Adafruit_PN532.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <SD.h>
#include <SPI.h>

#include "../cypher_pn532/cypher_pn532.ino"
