// ============================================================
// CYPHER NFC — v2.0 (2026)
// ESP32-C3 Super Mini + PN532 (I2C) + SSD1306 128x64 + SD Card
//
// HARDWARE WIRING NOTE:
//   The original board had BUTTON_UP on GPIO 3 which conflicts
//   with PN532_RESET, and BUTTON_SELECT on GPIO 2 which conflicts
//   with PN532_IRQ. Rewire buttons before flashing:
//     BUTTON_UP     -> GPIO 7   (free, safe)
//     BUTTON_DOWN   -> GPIO 1   (unchanged)
//     BUTTON_SELECT -> GPIO 0   (strapping pin, safe after boot)
//
// NDEF PRESETS (optional SD card files):
//   /NDEF_URL.TXT  — URL suffix for "Write NDEF URL" (e.g. "example.com")
//   /NDEF_TXT.TXT  — Text string for "Write NDEF Text"
// ============================================================

#include <Wire.h>
#include <Adafruit_PN532.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <SD.h>
#include <SPI.h>

// --- Build profile label ---
#ifndef APP_DISPLAY_NAME
#define APP_DISPLAY_NAME "CYPHER NFC"
#endif

// --- PN532 (I2C) ---
#ifndef PN532_IRQ
#define PN532_IRQ    (2)
#endif
#ifndef PN532_RESET
#define PN532_RESET  (3)
#endif

// --- I2C Bus ---
#ifndef I2C_SDA_PIN
#define I2C_SDA_PIN 8
#endif
#ifndef I2C_SCL_PIN
#define I2C_SCL_PIN 9
#endif
#ifndef I2C_CLOCK_HZ
#define I2C_CLOCK_HZ 100000
#endif

// --- SSD1306 OLED ---
#ifndef SCREEN_WIDTH
#define SCREEN_WIDTH     128
#endif
#ifndef SCREEN_HEIGHT
#define SCREEN_HEIGHT     64
#endif
#ifndef OLED_RESET
#define OLED_RESET        -1
#endif
#ifndef SSD1306_I2C_ADDR
#define SSD1306_I2C_ADDR 0x3C
#endif

// --- SD Card (SPI) ---
#ifndef SD_CS
#define SD_CS    10
#endif
#ifndef SD_MOSI
#define SD_MOSI   6
#endif
#ifndef SD_MISO
#define SD_MISO   5
#endif
#ifndef SD_SCK
#define SD_SCK    4
#endif

// --- Buttons (Active LOW with INPUT_PULLUP) ---
#ifndef BUTTON_UP
#define BUTTON_UP      7   // rewired from GPIO 3 (was conflicting with PN532_RESET)
#endif
#ifndef BUTTON_DOWN
#define BUTTON_DOWN    1   // unchanged
#endif
#ifndef BUTTON_SELECT
#define BUTTON_SELECT  0   // rewired from GPIO 2 (was conflicting with PN532_IRQ)
#endif
#ifndef BUTTON_NONE
#define BUTTON_NONE   -1
#endif

// --- Global Objects ---
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
U8G2_FOR_ADAFRUIT_GFX u8g2_for_adafruit_gfx;
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);

// ============================================================
// ENUMS
// ============================================================

enum AppState {
  STATE_MAIN_MENU,
  STATE_READ_SUBMENU,
  STATE_ATTACK_SUBMENU,
  STATE_CLONE_SUBMENU,
  STATE_WRITE_SUBMENU,
  STATE_SD_SUBMENU,
  STATE_DEMO_SUBMENU,
  STATE_EMULATE_SUBMENU
};

enum CardType {
  CARD_NFC_UNKNOWN,
  CARD_MIFARE_CLASSIC_1K,
  CARD_MIFARE_CLASSIC_4K,
  CARD_MIFARE_ULTRALIGHT,
  CARD_NTAG_213,
  CARD_NTAG_215,
  CARD_NTAG_216,
  CARD_ISO14443_4
};

// ============================================================
// MENU DATA
// ============================================================

const char* mainMenuItems[]   = { "Scan & Info", "Demo Mode", "Read Card", "Key Attack", "Clone Card", "Write Card", "SD Card", "Emulate Tag" };
const int   mainMenuCount     = 8;

const char* demoMenuItems[]   = { "Tag Studio", "Dump + Web", "Badge Writer", "Puzzle Hunt", "Back" };
const int   demoMenuCount     = 5;

const char* emulateMenuItems[] = { "NDEF from SD", "NTAG Dump", "UID Only", "Back" };
const int   emulateMenuCount   = 4;

const char* readMenuItems[]   = { "UID Only", "Read NDEF", "Dump MIFARE", "Dump NTAG", "Back" };
const int   readMenuCount     = 5;

const char* attackMenuItems[] = { "Dictionary Attack", "Back" };
const int   attackMenuCount   = 2;

const char* cloneMenuItems[]  = { "Dump to SD", "SD to Magic Card", "Verify Clone", "Back" };
const int   cloneMenuCount    = 4;

const char* writeMenuItems[]  = { "Write NDEF URL", "Write NDEF Text", "Write from SD", "Back" };
const int   writeMenuCount    = 4;

const char* sdMenuItems[]     = { "Browse Files", "Hex View File", "Delete File", "Back" };
const int   sdMenuCount       = 4;

// ============================================================
// STATE & NAVIGATION
// ============================================================

AppState appState       = STATE_MAIN_MENU;
int currentMenuItem     = 0;
int currentSubMenuItem  = 0;

// SD file browsing
String fileList[20];
int fileCount       = 0;
int currentFileIndex= 0;
String lastSavedFilename = "";
String lastSavedCompanion = "";

// ============================================================
// DATA STRUCTURES
// ============================================================

struct CardInfo {
  uint8_t  uid[7];
  uint8_t  uidLength;
  CardType type;
  uint16_t totalBlocks;
  uint16_t totalPages;
  char     typeName[24];
};

#define MAX_DUMP_BLOCKS 256
#define BLOCK_SIZE       16

struct MifareDump {
  uint8_t data[MAX_DUMP_BLOCKS][BLOCK_SIZE]; // 4 KB
  bool    blockRead[MAX_DUMP_BLOCKS];
  uint8_t keyUsed[MAX_DUMP_BLOCKS][6];       // key that unlocked each block
  int     totalBlocks;
  uint8_t uid[7];
  uint8_t uidLength;
};

struct NTAGDump {
  uint8_t pages[231][4]; // max NTAG216 = 231 pages
  bool    pageRead[231];
  int     totalPages;
  uint8_t uid[7];
  uint8_t uidLength;
  char    typeName[24];
};

struct SectorKeyMap {
  uint8_t keyA[40][6];
  uint8_t keyB[40][6];
  bool    keyAKnown[40];
  bool    keyBKnown[40];
  int     crackedCount;
  int     numSectors;
};

struct NDEFDecoded {
  bool ok;
  char recordType;
  String label;
  String value;
};

struct NDEFPreset {
  char recordType;
  uint8_t prefix;
  String payload;
  String label;
};

// Global NFC data buffers (kept as globals to avoid large stack allocations)
MifareDump   mifDump;
NTAGDump     ntagDump;
SectorKeyMap keyMap;

// ============================================================
// DEFAULT KEY TABLE — 50 common MIFARE Classic keys
// ============================================================

const uint8_t defaultKeys[][6] = {
  {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}, // Factory default (most common)
  {0x00,0x00,0x00,0x00,0x00,0x00}, // All zeros
  {0xA0,0xA1,0xA2,0xA3,0xA4,0xA5}, // MAD sector key A
  {0xB0,0xB1,0xB2,0xB3,0xB4,0xB5}, // Common transport key B
  {0xD3,0xF7,0xD3,0xF7,0xD3,0xF7}, // NDEF public read key
  {0x4B,0x52,0x59,0x50,0x54,0x4F}, // "KRYPTO"
  {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF}, // Sequential vendor key
  {0x71,0x4C,0x5C,0x88,0x6E,0x97}, // Gallagher access control
  {0x58,0x7E,0xE5,0xF9,0x35,0x0F}, // Known default
  {0x10,0x18,0x1F,0x28,0x6F,0x3D}, // Access control vendor
  {0x18,0x00,0x42,0x0C,0xDB,0xE3}, // HID iClass migration
  {0xFF,0x07,0x80,0x69,0xFF,0x07}, // NXP application note
  {0xA9,0xF9,0x53,0xDE,0x52,0x5A}, // Transit system
  {0x63,0x0A,0xCA,0x3B,0xEF,0xF0}, // Vendor default
  {0xC8,0xC1,0x70,0x58,0x60,0x5F}, // MIFARE Plus migration
  {0x09,0x13,0x84,0x6C,0x3E,0xCC}, // Access control vendor
  {0x3C,0x65,0xC4,0xF6,0xDE,0x65}, // Known default
  {0x84,0xCB,0xB5,0x55,0x28,0x2F}, // Known default
  {0xFC,0x00,0x01,0x82,0x04,0x00}, // Parking systems
  {0xE6,0xD7,0x50,0x24,0x23,0xC0}, // Transit system
  {0x48,0x5E,0x16,0x5A,0x32,0x31}, // Access control
  {0xAA,0x07,0x20,0x28,0x09,0x8F}, // Known default
  {0xEC,0x0A,0x9B,0x45,0x42,0x2F}, // Known default
  {0xBB,0xB9,0x0D,0xBA,0x55,0x07}, // Known default
  {0x66,0xFF,0x24,0xD2,0xA5,0xCA}, // STid RFID vendor
  {0x7B,0xD7,0x7E,0xB5,0x1E,0x59}, // Known default
  {0x16,0xF3,0xD5,0xAB,0x11,0x41}, // Known default
  {0xAB,0xCE,0x19,0x13,0x36,0xF3}, // Known default
  {0x53,0xFB,0xC8,0x51,0xFB,0x4C}, // Known default
  {0x4A,0x4C,0x2E,0x8C,0xBC,0xEB}, // Known default
  {0xCC,0xCC,0xCC,0xCC,0xCC,0xCC}, // Repeated pattern
  {0xAA,0xAA,0xAA,0xAA,0xAA,0xAA}, // Repeated pattern
  {0xBB,0xBB,0xBB,0xBB,0xBB,0xBB}, // Repeated pattern
  {0x11,0x11,0x11,0x11,0x11,0x11}, // Repeated pattern
  {0x22,0x22,0x22,0x22,0x22,0x22}, // Repeated pattern
  {0x33,0x33,0x33,0x33,0x33,0x33}, // Repeated pattern
  {0x44,0x44,0x44,0x44,0x44,0x44}, // Repeated pattern
  {0x55,0x55,0x55,0x55,0x55,0x55}, // Repeated pattern
  {0x01,0x02,0x03,0x04,0x05,0x06}, // Sequential
  {0x12,0x34,0x56,0x78,0x9A,0xBC}, // Common test key
  {0xDE,0xAD,0xBE,0xEF,0xCA,0xFE}, // "DEADBEEFCAFE"
  {0xCA,0xFE,0xBA,0xBE,0xDE,0xAD}, // Reversed
  {0x39,0x54,0xE7,0xB4,0x56,0x32}, // Vendor default
  {0xF0,0xE1,0xD2,0xC3,0xB4,0xA5}, // Descending pattern
  {0x05,0x01,0x02,0x03,0x04,0x00}, // Parking / transit
  {0x19,0x91,0x19,0x91,0x19,0x91}, // Year-based pattern
  {0x20,0x20,0x20,0x20,0x20,0x20}, // Year 2020
  {0x26,0x35,0x5E,0x89,0xB7,0x14}, // Vendor key
  {0xAB,0xCD,0xEF,0x12,0x34,0x56}, // Common hex pattern
  {0x2A,0x2B,0x2C,0x2D,0x2E,0x2F}, // ASCII sequential
};
const int NUM_DEFAULT_KEYS = sizeof(defaultKeys) / sizeof(defaultKeys[0]);

// ============================================================
// DISPLAY HELPERS
// ============================================================

void drawBorder() {
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
}

void displayInfo(const String& title, const String& line1 = "", const String& line2 = "", const String& line3 = "") {
  display.clearDisplay();
  drawBorder();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(4, 4);
  display.println(title);
  display.drawLine(0, 14, SCREEN_WIDTH, 14, SSD1306_WHITE);
  if (line1.length()) { display.setCursor(4, 18); display.println(line1); }
  if (line2.length()) { display.setCursor(4, 28); display.println(line2); }
  if (line3.length()) { display.setCursor(4, 38); display.println(line3); }
  display.display();
}

void displayMenuScreen(const char* title, const char* items[], int count, int selected) {
  display.clearDisplay();
  drawBorder();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(4, 4);
  display.println(title);
  display.drawLine(0, 14, SCREEN_WIDTH, 14, SSD1306_WHITE);

  int startIdx = 0;
  if (selected > 2)         startIdx = selected - 2;
  if (startIdx + 4 > count) startIdx = max(0, count - 4);

  for (int i = 0; i < 4 && (startIdx + i) < count; i++) {
    display.setCursor(4, 18 + i * 10);
    display.print((startIdx + i == selected) ? "> " : "  ");
    display.println(items[startIdx + i]);
  }
  if (startIdx > 0)         { display.setCursor(120, 18); display.print("^"); }
  if (startIdx + 4 < count) { display.setCursor(120, 48); display.print("v"); }
  display.display();
}

void displayProgress(const char* title, int current, int total, const char* status) {
  display.clearDisplay();
  drawBorder();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(4, 4);
  display.println(title);
  display.drawLine(0, 14, SCREEN_WIDTH, 14, SSD1306_WHITE);

  int barWidth = (total > 0) ? (int)((long)120 * current / total) : 0;
  display.drawRect(4, 20, 120, 8, SSD1306_WHITE);
  if (barWidth > 0) display.fillRect(4, 20, barWidth, 8, SSD1306_WHITE);

  String pct = String(total > 0 ? (int)(100L * current / total) : 0)
               + "% (" + String(current) + "/" + String(total) + ")";
  display.setCursor(4, 32);
  display.println(pct);
  display.setCursor(4, 44);
  display.println(status);
  display.display();
}

void displayTitleScreen() {
  display.clearDisplay();
  u8g2_for_adafruit_gfx.setFont(u8g2_font_adventurer_tr);
  u8g2_for_adafruit_gfx.setCursor(10, 40);
  u8g2_for_adafruit_gfx.print(APP_DISPLAY_NAME);
  display.display();
}

void redisplayCurrentMenu() {
  switch (appState) {
    case STATE_MAIN_MENU:
      displayMenuScreen(APP_DISPLAY_NAME, mainMenuItems, mainMenuCount, currentMenuItem);
      break;
    case STATE_READ_SUBMENU:
      displayMenuScreen("Read Card", readMenuItems, readMenuCount, currentSubMenuItem);
      break;
    case STATE_DEMO_SUBMENU:
      displayMenuScreen("Demo Mode", demoMenuItems, demoMenuCount, currentSubMenuItem);
      break;
    case STATE_ATTACK_SUBMENU:
      displayMenuScreen("Key Attack", attackMenuItems, attackMenuCount, currentSubMenuItem);
      break;
    case STATE_CLONE_SUBMENU:
      displayMenuScreen("Clone Card", cloneMenuItems, cloneMenuCount, currentSubMenuItem);
      break;
    case STATE_WRITE_SUBMENU:
      displayMenuScreen("Write Card", writeMenuItems, writeMenuCount, currentSubMenuItem);
      break;
    case STATE_SD_SUBMENU:
      displayMenuScreen("SD Card", sdMenuItems, sdMenuCount, currentSubMenuItem);
      break;
    case STATE_EMULATE_SUBMENU:
      displayMenuScreen("Emulate Tag", emulateMenuItems, emulateMenuCount, currentSubMenuItem);
      break;
  }
}

// ============================================================
// BUTTON HANDLING
// ============================================================

int getButtonInput() {
  if (digitalRead(BUTTON_UP) == LOW) {
    delay(200);
    if (digitalRead(BUTTON_UP) == LOW) return BUTTON_UP;
  }
  if (digitalRead(BUTTON_DOWN) == LOW) {
    delay(200);
    if (digitalRead(BUTTON_DOWN) == LOW) return BUTTON_DOWN;
  }
  if (digitalRead(BUTTON_SELECT) == LOW) {
    delay(200);
    if (digitalRead(BUTTON_SELECT) == LOW) return BUTTON_SELECT;
  }
  return BUTTON_NONE;
}

// Wait for a card, return false if SELECT pressed to cancel
bool waitForCard(uint8_t* uid, uint8_t* uidLength,
                 const char* prompt1 = "Place card",
                 const char* prompt2 = "near reader") {
  displayInfo("Waiting...", prompt1, prompt2, "SELECT:Cancel");
  while (true) {
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, uidLength)) {
      return true;
    }
    if (digitalRead(BUTTON_SELECT) == LOW) {
      delay(200);
      return false;
    }
    delay(100);
  }
}

// ============================================================
// SD CARD
// ============================================================

void initSDCard() {
  displayInfo("SD Card", "Initializing...");
  digitalWrite(SD_CS, HIGH);
  delay(50);
  SPI.end();
  delay(50);
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  delay(100);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  delay(2000);

  if (!SD.begin(SD_CS)) {
    displayInfo("SD Error", "Init failed!", "Check SD card");
    Serial.println("SD init failed");
    delay(2000);
    return;
  }

  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    displayInfo("SD Error", "No card found");
    Serial.println("SD card type: none");
    delay(2000);
    return;
  }

  String typeStr;
  switch (cardType) {
    case CARD_MMC:  typeStr = "MMC";  break;
    case CARD_SD:   typeStr = "SDSC"; break;
    case CARD_SDHC: typeStr = "SDHC"; break;
    default:        typeStr = "Unk";  break;
  }
  String sizeStr = String((uint32_t)(SD.cardSize() / (1024 * 1024))) + " MB";
  Serial.printf("SD ready type=%s size=%s\n", typeStr.c_str(), sizeStr.c_str());
  displayInfo("SD Ready", typeStr, sizeStr);
  delay(1500);
}

uint16_t readFileCounter() {
  if (!SD.exists("/COUNTER.TXT")) return 0;
  File f = SD.open("/COUNTER.TXT", FILE_READ);
  if (!f) return 0;
  uint16_t n = 0;
  while (f.available()) {
    char c = f.read();
    if (c >= '0' && c <= '9') n = n * 10 + (c - '0');
  }
  f.close();
  return n;
}

void writeFileCounter(uint16_t n) {
  SD.remove("/COUNTER.TXT");
  File f = SD.open("/COUNTER.TXT", FILE_WRITE);
  if (f) { f.print(n); f.close(); }
}

// Generates sequential filenames: prefix + 3-digit counter + ".ext"
String generateUniqueFilename(const char* prefix, const char* ext) {
  uint16_t n = readFileCounter() + 1;
  writeFileCounter(n);
  char buf[13];
  snprintf(buf, sizeof(buf), "%s%03u.%s", prefix, n, ext);
  return String(buf);
}

// Browse SD files (filtered by extension, "" = show all).
// Populates fileList[], fileCount, currentFileIndex.
// Returns true if user selected a file, false if empty/cancelled.
bool browseFiles(const char* extension) {
  fileCount = 0;
  currentFileIndex = 0;

  File root = SD.open("/");
  if (!root) {
    displayInfo("SD Error", "Can't open root");
    delay(2000);
    return false;
  }
  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;
    if (!entry.isDirectory()) {
      String name = String(entry.name());
      if (strlen(extension) == 0 || name.endsWith(extension)) {
        if (fileCount < 20) fileList[fileCount++] = name;
      }
    }
    entry.close();
  }
  root.close();

  if (fileCount == 0) {
    String msg = (strlen(extension) > 0)
                 ? "No *" + String(extension) + " files"
                 : "SD card empty";
    displayInfo("No Files", msg);
    delay(2000);
    return false;
  }

  while (true) {
    display.clearDisplay();
    drawBorder();
    display.setCursor(4, 4);
    display.println("Select File");
    display.drawLine(0, 14, SCREEN_WIDTH, 14, SSD1306_WHITE);

    int startIdx = max(0, currentFileIndex - 1);
    for (int i = 0; i < 3 && (startIdx + i) < fileCount; i++) {
      display.setCursor(4, 18 + i * 10);
      display.print((startIdx + i == currentFileIndex) ? "> " : "  ");
      String fname = fileList[startIdx + i];
      if (fname.length() > 16) fname = fname.substring(0, 13) + "...";
      display.println(fname);
    }
    display.setCursor(4, 54);
    display.println("U/D:Nav S:Select");
    display.display();

    int btn = getButtonInput();
    if (btn == BUTTON_UP)
      currentFileIndex = (currentFileIndex > 0) ? currentFileIndex - 1 : fileCount - 1;
    if (btn == BUTTON_DOWN)
      currentFileIndex = (currentFileIndex < fileCount - 1) ? currentFileIndex + 1 : 0;
    if (btn == BUTTON_SELECT) return true;
  }
}

void viewFiles() {
  if (!browseFiles("")) return;
  displayInfo("Selected", fileList[currentFileIndex], "Size: N/A");
  delay(1500);
}

void deleteFile() {
  if (!browseFiles("")) return;
  displayInfo("Delete?", fileList[currentFileIndex], "SELECT:Yes UP:No");
  while (true) {
    if (digitalRead(BUTTON_SELECT) == LOW) {
      delay(200);
      bool ok = SD.remove("/" + fileList[currentFileIndex]);
      displayInfo(ok ? "Deleted!" : "Failed!", fileList[currentFileIndex]);
      delay(2000);
      return;
    }
    if (digitalRead(BUTTON_UP) == LOW) {
      delay(200);
      displayInfo("Cancelled");
      delay(1500);
      return;
    }
    delay(30);
  }
}

void hexViewFile() {
  if (!browseFiles("")) return;
  String shortName = fileList[currentFileIndex];

  File f = SD.open("/" + shortName, FILE_READ);
  if (!f) {
    displayInfo("Error", "Cannot open", shortName);
    delay(2000);
    return;
  }
  long fileSize = f.size();
  int offset = 0;

  while (true) {
    display.clearDisplay();
    drawBorder();
    display.setCursor(4, 4);
    display.println(shortName.substring(0, 14));
    display.drawLine(0, 14, SCREEN_WIDTH, 14, SSD1306_WHITE);

    f.seek(offset);
    for (int row = 0; row < 4 && f.available(); row++) {
      uint8_t buf[4] = {0, 0, 0, 0};
      int n = f.read(buf, 4);
      int y = 18 + row * 10;
      // Hex columns (left side)
      display.setCursor(4, y);
      for (int i = 0; i < n; i++) {
        if (buf[i] < 0x10) display.print("0");
        display.print(buf[i], HEX);
        display.print(" ");
      }
      // ASCII columns (right side)
      display.setCursor(88, y);
      for (int i = 0; i < n; i++) {
        char c = (char)buf[i];
        display.print((c >= 0x20 && c < 0x7F) ? c : '.');
      }
    }
    display.setCursor(4, 56);
    display.print(String(offset) + "/" + String(fileSize));
    display.display();

    while (true) {
      int btn = getButtonInput();
      if (btn == BUTTON_DOWN && (offset + 16) < fileSize) { offset += 16; break; }
      if (btn == BUTTON_UP   && offset > 0)               { offset -= 16; break; }
      if (btn == BUTTON_SELECT) { f.close(); return; }
      delay(30);
    }
  }
}

// ============================================================
// LOGGING & TEXT HELPERS
// ============================================================

String uidToString(const uint8_t* uid, uint8_t uidLength) {
  String uidStr = "";
  for (int i = 0; i < uidLength; i++) {
    if (uid[i] < 0x10) uidStr += "0";
    uidStr += String(uid[i], HEX);
    if (i < uidLength - 1) uidStr += ":";
  }
  uidStr.toUpperCase();
  return uidStr;
}

String csvEscape(const String& value) {
  bool needsQuotes = value.indexOf(',') >= 0 || value.indexOf('"') >= 0 ||
                     value.indexOf('\n') >= 0 || value.indexOf('\r') >= 0;
  if (!needsQuotes) return value;
  String out = "\"";
  for (size_t i = 0; i < value.length(); i++) {
    if (value[i] == '"') out += "\"\"";
    else out += value[i];
  }
  out += "\"";
  return out;
}

void appendScanLog(const String& uid, const String& type,
                   const String& action, const String& filename = "-") {
  const char* logPath = "/SCANLOG.CSV";
  bool needsHeader = !SD.exists(logPath);
  File f = SD.open(logPath, FILE_APPEND);
  if (!f) return;
  if (needsHeader) f.println("uptime_ms,uid,type,action,filename");
  f.print(millis()); f.print(",");
  f.print(csvEscape(uid)); f.print(",");
  f.print(csvEscape(type)); f.print(",");
  f.print(csvEscape(action)); f.print(",");
  f.println(csvEscape(filename.length() ? filename : "-"));
  f.close();
}

String ndefUriPrefix(uint8_t code) {
  switch (code) {
    case 0x01: return "http://www.";
    case 0x02: return "https://www.";
    case 0x03: return "http://";
    case 0x04: return "https://";
    default: return "";
  }
}

NDEFPreset parseNDEFPreset(String content) {
  content.trim();
  NDEFPreset preset;
  preset.recordType = 'T';
  preset.prefix = 0;
  preset.payload = content;
  preset.label = "Text";

  String lower = content;
  lower.toLowerCase();
  if (lower.startsWith("text:")) {
    preset.payload = content.substring(5);
    preset.payload.trim();
    return preset;
  }

  if (lower.startsWith("url:")) {
    preset.recordType = 'U';
    preset.label = "URL";
    preset.payload = content.substring(4);
    preset.payload.trim();
  }

  if (preset.recordType == 'U') {
    String urlLower = preset.payload;
    urlLower.toLowerCase();
    if (urlLower.startsWith("https://www.")) {
      preset.prefix = 0x02;
      preset.payload = preset.payload.substring(12);
    } else if (urlLower.startsWith("http://www.")) {
      preset.prefix = 0x01;
      preset.payload = preset.payload.substring(11);
    } else if (urlLower.startsWith("https://")) {
      preset.prefix = 0x04;
      preset.payload = preset.payload.substring(8);
    } else if (urlLower.startsWith("http://")) {
      preset.prefix = 0x03;
      preset.payload = preset.payload.substring(7);
    } else {
      preset.prefix = 0x04;
    }
  }
  return preset;
}

// ============================================================
// CARD TYPE DETECTION
// ============================================================

CardType detectCardType(uint8_t* uid, uint8_t uidLength, CardInfo* info) {
  memcpy(info->uid, uid, uidLength);
  info->uidLength  = uidLength;
  info->totalBlocks = 0;
  info->totalPages  = 0;

  if (uidLength == 4) {
    // MIFARE Classic 1K (64 blocks) or 4K (256 blocks)
    uint8_t keya[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (nfc.mifareclassic_AuthenticateBlock(uid, uidLength, 0, 0, keya)) {
      // Probe block 128: only exists on 4K cards
      if (nfc.mifareclassic_AuthenticateBlock(uid, uidLength, 128, 0, keya)) {
        info->type = CARD_MIFARE_CLASSIC_4K;
        info->totalBlocks = 256;
        strncpy(info->typeName, "MIFARE Classic 4K", sizeof(info->typeName));
      } else {
        info->type = CARD_MIFARE_CLASSIC_1K;
        info->totalBlocks = 64;
        strncpy(info->typeName, "MIFARE Classic 1K", sizeof(info->typeName));
      }
    } else {
      // Auth failed — still MIFARE Classic but locked with non-default key
      info->type = CARD_MIFARE_CLASSIC_1K;
      info->totalBlocks = 64;
      strncpy(info->typeName, "MIFARE Classic ?K", sizeof(info->typeName));
    }
    return info->type;
  }

  if (uidLength == 7) {
    // NTAG or Ultralight — read Capability Container at page 3
    uint8_t page3[4];
    if (nfc.ntag2xx_ReadPage(3, page3)) {
      uint8_t cc2 = page3[2]; // memory size indicator
      if (cc2 == 0x12) {
        info->type = CARD_NTAG_213; info->totalPages = 45;
        strncpy(info->typeName, "NTAG213", sizeof(info->typeName));
      } else if (cc2 == 0x3E) {
        info->type = CARD_NTAG_215; info->totalPages = 135;
        strncpy(info->typeName, "NTAG215", sizeof(info->typeName));
      } else if (cc2 == 0x6D) {
        info->type = CARD_NTAG_216; info->totalPages = 231;
        strncpy(info->typeName, "NTAG216", sizeof(info->typeName));
      } else {
        info->type = CARD_MIFARE_ULTRALIGHT; info->totalPages = 42;
        strncpy(info->typeName, "Ultralight", sizeof(info->typeName));
      }
    } else {
      info->type = CARD_MIFARE_ULTRALIGHT; info->totalPages = 42;
      strncpy(info->typeName, "Ultralight", sizeof(info->typeName));
    }
    return info->type;
  }

  info->type = CARD_NFC_UNKNOWN;
  strncpy(info->typeName, "Unknown", sizeof(info->typeName));
  return CARD_NFC_UNKNOWN;
}

void scanAndInfo() {
  uint8_t uid[7] = {0};
  uint8_t uidLength = 0;
  if (!waitForCard(uid, &uidLength)) { redisplayCurrentMenu(); return; }

  CardInfo info;
  detectCardType(uid, uidLength, &info);

  String uidStr = uidToString(uid, uidLength);
  appendScanLog(uidStr, info.typeName, "scan_info");

  displayInfo(info.typeName,
              "UID: " + uidStr.substring(0, 17),
              "Len: " + String(uidLength) + " bytes");
  delay(3000);

  if (info.totalBlocks > 0) {
    displayInfo(info.typeName,
                String(info.totalBlocks) + " blocks",
                String((int)info.totalBlocks * BLOCK_SIZE) + " bytes total");
  } else if (info.totalPages > 0) {
    displayInfo(info.typeName,
                String(info.totalPages) + " pages",
                String((int)info.totalPages * 4) + " bytes total");
  }
  delay(3000);
  redisplayCurrentMenu();
}

// ============================================================
// MIFARE CLASSIC HELPERS
// ============================================================

int blockToSector(int block, bool is4K) {
  if (!is4K || block < 128) return block / 4;
  return 32 + (block - 128) / 16;
}

int sectorTrailerBlock(int sector, bool is4K) {
  if (!is4K || sector < 32) return sector * 4 + 3;
  return 128 + (sector - 32) * 16 + 15;
}

int totalSectors(bool is4K) { return is4K ? 40 : 16; }

// ============================================================
// MIFARE CLASSIC DUMP
// ============================================================

void dumpMifareClassic(uint8_t* uid, uint8_t uidLength, bool is4K) {
  int numBlocks  = is4K ? 256 : 64;
  int numSectors = totalSectors(is4K);

  mifDump.totalBlocks = numBlocks;
  mifDump.uidLength   = uidLength;
  memcpy(mifDump.uid, uid, uidLength);
  memset(mifDump.blockRead, false, sizeof(mifDump.blockRead));

  // Populate keyMap while discovering keys
  keyMap.numSectors    = numSectors;
  keyMap.crackedCount  = 0;
  memset(keyMap.keyAKnown, false, sizeof(keyMap.keyAKnown));
  memset(keyMap.keyBKnown, false, sizeof(keyMap.keyBKnown));

  // Phase 1 — find working Key A for each sector
  for (int s = 0; s < numSectors; s++) {
    int trailer = sectorTrailerBlock(s, is4K);
    displayProgress("Finding Keys", s, numSectors,
                    ("Sector " + String(s)).c_str());

    for (int k = 0; k < NUM_DEFAULT_KEYS; k++) {
      if (nfc.mifareclassic_AuthenticateBlock(uid, uidLength, trailer, 0,
          (uint8_t*)defaultKeys[k])) {
        memcpy(keyMap.keyA[s], defaultKeys[k], 6);
        keyMap.keyAKnown[s] = true;
        keyMap.crackedCount++;
        break;
      }
      if (k % 5 == 0) delay(1); // feed watchdog
    }
  }

  // Phase 2 — read every block using its sector's cached Key A
  for (int b = 0; b < numBlocks; b++) {
    int s = blockToSector(b, is4K);
    displayProgress("Reading", b, numBlocks, ("Block " + String(b)).c_str());

    if (keyMap.keyAKnown[s]) {
      nfc.mifareclassic_AuthenticateBlock(uid, uidLength, b, 0, keyMap.keyA[s]);
      if (nfc.mifareclassic_ReadDataBlock(b, mifDump.data[b])) {
        mifDump.blockRead[b] = true;
        memcpy(mifDump.keyUsed[b], keyMap.keyA[s], 6);
      }
    }
    if (b % 4 == 0) delay(1); // feed watchdog
  }

  int readCount = 0;
  for (int b = 0; b < numBlocks; b++) if (mifDump.blockRead[b]) readCount++;
  displayInfo("Dump Done",
              String(readCount) + "/" + String(numBlocks) + " blocks",
              String(keyMap.crackedCount) + "/" + String(numSectors) + " sectors");
  delay(2000);
}

bool saveMifareDumpToSD(bool is4K) {
  lastSavedFilename = "";
  lastSavedCompanion = "";
  String mfdName = generateUniqueFilename("dmp", "mfd");
  File mfd = SD.open("/" + mfdName, FILE_WRITE);
  if (!mfd) {
    displayInfo("SD Error", "Cannot create", mfdName);
    delay(2000);
    return false;
  }

  displayInfo("Saving", mfdName, "Writing binary...");
  static const uint8_t blankBlock[BLOCK_SIZE] = {0};
  for (int b = 0; b < mifDump.totalBlocks; b++) {
    mfd.write(mifDump.blockRead[b] ? mifDump.data[b] : blankBlock, BLOCK_SIZE);
  }
  mfd.close();

  // Human-readable companion file
  String txtName = mfdName.substring(0, mfdName.lastIndexOf('.')) + ".txt";
  File txt = SD.open("/" + txtName, FILE_WRITE);
  if (txt) {
    txt.println("CYPHER NFC MIFARE Dump");
    txt.print("UID: ");
    for (int i = 0; i < mifDump.uidLength; i++) {
      if (mifDump.uid[i] < 0x10) txt.print("0");
      txt.print(mifDump.uid[i], HEX);
      if (i < mifDump.uidLength - 1) txt.print(":");
    }
    txt.println();
    txt.println(is4K ? "Type: MIFARE Classic 4K" : "Type: MIFARE Classic 1K");
    txt.println();

    for (int b = 0; b < mifDump.totalBlocks; b++) {
      int s = blockToSector(b, is4K);
      int firstOfSector = (s < 32) ? s * 4 : 128 + (s - 32) * 16;
      if (b == firstOfSector) {
        txt.print("--- Sector "); txt.print(s); txt.println(" ---");
      }
      txt.print("Blk ");
      if (b < 10)  txt.print(" ");
      if (b < 100) txt.print(" ");
      txt.print(b); txt.print(": ");
      for (int i = 0; i < BLOCK_SIZE; i++) {
        if (mifDump.data[b][i] < 0x10) txt.print("0");
        txt.print(mifDump.data[b][i], HEX); txt.print(" ");
      }
      txt.print("| ");
      for (int i = 0; i < BLOCK_SIZE; i++) {
        char c = (char)mifDump.data[b][i];
        txt.print((c >= 0x20 && c < 0x7F) ? c : '.');
      }
      if (!mifDump.blockRead[b]) txt.print(" [UNREAD]");
      txt.println();
    }
    txt.close();
  }

  lastSavedFilename = mfdName;
  lastSavedCompanion = txtName;
  displayInfo("Saved!", mfdName, txtName);
  delay(2500);
  return true;
}

bool loadMifareDumpFromSD() {
  if (!browseFiles(".mfd")) return false;

  String filename = "/" + fileList[currentFileIndex];
  File f = SD.open(filename, FILE_READ);
  if (!f) {
    displayInfo("Error", "Cannot open", fileList[currentFileIndex]);
    delay(2000);
    return false;
  }

  long fileSize = f.size();
  bool is4K = (fileSize >= 4096);
  mifDump.totalBlocks = is4K ? 256 : 64;
  mifDump.uidLength   = 0; // UID unknown until written to card

  displayInfo("Loading", fileList[currentFileIndex], is4K ? "4K (256 blk)" : "1K (64 blk)");
  for (int b = 0; b < mifDump.totalBlocks; b++) {
    if (f.available() >= BLOCK_SIZE) {
      f.read(mifDump.data[b], BLOCK_SIZE);
      mifDump.blockRead[b] = true;
    } else {
      memset(mifDump.data[b], 0, BLOCK_SIZE);
      mifDump.blockRead[b] = false;
    }
    memset(mifDump.keyUsed[b], 0xFF, 6); // assume default key for clone
  }
  f.close();

  displayInfo("Dump Loaded", fileList[currentFileIndex],
              String(mifDump.totalBlocks) + " blocks ready");
  delay(2000);
  return true;
}

void verifyClone() {
  if (mifDump.totalBlocks == 0) {
    displayInfo("No Dump Loaded", "Use Clone>Dump to SD", "or load .mfd first");
    delay(3000);
    redisplayCurrentMenu();
    return;
  }

  uint8_t uid[7]; uint8_t uidLength;
  if (!waitForCard(uid, &uidLength, "Place CLONE", "card now")) {
    redisplayCurrentMenu(); return;
  }

  bool is4K = (mifDump.totalBlocks == 256);
  int mismatch = 0, skipped = 0;
  uint8_t defKey[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

  for (int b = 0; b < mifDump.totalBlocks; b++) {
    if (!mifDump.blockRead[b]) { skipped++; continue; }
    displayProgress("Verifying", b, mifDump.totalBlocks, ("Blk " + String(b)).c_str());

    bool authed = nfc.mifareclassic_AuthenticateBlock(uid, uidLength, b, 0, defKey);
    if (!authed)
      authed = nfc.mifareclassic_AuthenticateBlock(uid, uidLength, b, 0, mifDump.keyUsed[b]);
    if (!authed) { skipped++; continue; }

    uint8_t readBack[16];
    if (!nfc.mifareclassic_ReadDataBlock(b, readBack)) { skipped++; continue; }
    if (memcmp(readBack, mifDump.data[b], 16) != 0) mismatch++;
    if (b % 4 == 0) delay(1);
  }

  if (mismatch == 0) {
    displayInfo("Verify OK!", "All blocks match",
                String(skipped) + " blocks skipped");
  } else {
    displayInfo("Verify FAIL", String(mismatch) + " mismatches",
                String(skipped) + " skipped");
  }
  delay(3000);
  redisplayCurrentMenu();
}

// ============================================================
// DICTIONARY ATTACK
// ============================================================

void showKeyMap(int numSectors) {
  display.clearDisplay();
  drawBorder();
  display.setCursor(4, 4);
  display.println("Key Map [A/B]");
  display.drawLine(0, 14, SCREEN_WIDTH, 14, SSD1306_WHITE);

  // Show up to 16 sectors in a compact grid (2 rows of 8)
  int show = min(numSectors, 16);
  for (int s = 0; s < show; s++) {
    int col = (s % 8) * 15 + 4;
    int row = 18 + (s / 8) * 10;
    display.setCursor(col, row);
    String sym = "";
    sym += (keyMap.keyAKnown[s] ? "A" : ".");
    sym += (keyMap.keyBKnown[s] ? "B" : ".");
    display.print(sym);
  }

  display.setCursor(4, 44);
  display.print(String(keyMap.crackedCount) + "/" + String(numSectors) + " cracked");
  display.setCursor(4, 54);
  display.println("SELECT: Save+Exit");
  display.display();

  while (digitalRead(BUTTON_SELECT) != LOW) delay(50);
  delay(200);
}

void saveKeyMapToSD(int numSectors) {
  String fname = generateUniqueFilename("key", "txt");
  File f = SD.open("/" + fname, FILE_WRITE);
  if (!f) return;

  f.println("CYPHER NFC Key Map");
  f.print("Sectors: "); f.println(numSectors);
  f.print("Cracked: "); f.println(keyMap.crackedCount);
  f.println();

  for (int s = 0; s < numSectors; s++) {
    f.print("S"); if (s < 10) f.print("0"); f.print(s);
    f.print(" A:");
    if (keyMap.keyAKnown[s]) {
      for (int i = 0; i < 6; i++) {
        if (keyMap.keyA[s][i] < 0x10) f.print("0");
        f.print(keyMap.keyA[s][i], HEX);
      }
    } else { f.print("??????"); }
    f.print(" B:");
    if (keyMap.keyBKnown[s]) {
      for (int i = 0; i < 6; i++) {
        if (keyMap.keyB[s][i] < 0x10) f.print("0");
        f.print(keyMap.keyB[s][i], HEX);
      }
    } else { f.print("??????"); }
    f.println();
  }
  f.close();
  displayInfo("Keys Saved", fname);
  delay(1500);
}

void dictionaryAttack() {
  uint8_t uid[7]; uint8_t uidLength;
  if (!waitForCard(uid, &uidLength, "Place MIFARE", "card now")) {
    redisplayCurrentMenu(); return;
  }

  CardInfo info;
  detectCardType(uid, uidLength, &info);
  if (info.type != CARD_MIFARE_CLASSIC_1K && info.type != CARD_MIFARE_CLASSIC_4K) {
    displayInfo("Not MIFARE!", info.typeName, "Need Classic card");
    delay(3000);
    redisplayCurrentMenu();
    return;
  }

  bool is4K = (info.type == CARD_MIFARE_CLASSIC_4K);
  int numSectors = totalSectors(is4K);
  keyMap.numSectors   = numSectors;
  keyMap.crackedCount = 0;
  memset(keyMap.keyAKnown, false, sizeof(keyMap.keyAKnown));
  memset(keyMap.keyBKnown, false, sizeof(keyMap.keyBKnown));

  for (int s = 0; s < numSectors; s++) {
    int trailer = sectorTrailerBlock(s, is4K);

    // Try Key A
    displayProgress("Dict Attack", s * 2, numSectors * 2,
                    ("S" + String(s) + " Key A").c_str());
    for (int k = 0; k < NUM_DEFAULT_KEYS && !keyMap.keyAKnown[s]; k++) {
      if (nfc.mifareclassic_AuthenticateBlock(uid, uidLength, trailer, 0,
          (uint8_t*)defaultKeys[k])) {
        memcpy(keyMap.keyA[s], defaultKeys[k], 6);
        keyMap.keyAKnown[s] = true;
        keyMap.crackedCount++;
      }
      if (k % 3 == 0) delay(1);
    }

    // Try Key B
    displayProgress("Dict Attack", s * 2 + 1, numSectors * 2,
                    ("S" + String(s) + " Key B").c_str());
    for (int k = 0; k < NUM_DEFAULT_KEYS && !keyMap.keyBKnown[s]; k++) {
      if (nfc.mifareclassic_AuthenticateBlock(uid, uidLength, trailer, 1,
          (uint8_t*)defaultKeys[k])) {
        memcpy(keyMap.keyB[s], defaultKeys[k], 6);
        keyMap.keyBKnown[s] = true;
      }
      if (k % 3 == 0) delay(1);
    }
  }

  showKeyMap(numSectors);
  saveKeyMapToSD(numSectors);
  redisplayCurrentMenu();
}

// ============================================================
// NTAG OPS
// ============================================================

// Raw NTAG page read using InDataExchange — works for all page numbers
bool readNTAGPageRaw(uint8_t page, uint8_t* buf) {
  uint8_t cmd[] = {0x30, page}; // MIFARE_CMD_READ
  uint8_t resp[18];
  uint8_t respLen = sizeof(resp);
  if (!nfc.inDataExchange(cmd, 2, resp, &respLen)) return false;
  if (respLen < 4) return false;
  memcpy(buf, resp, 4); // first 4 bytes = requested page data
  return true;
}

bool readNDEFPages(CardInfo* info, int* maxPage, int* readCount, int* failCount) {
  *maxPage = min((int)info->totalPages, 42);
  *readCount = 0;
  *failCount = 0;
  memset(ntagDump.pageRead, false, sizeof(ntagDump.pageRead));
  memset(ntagDump.pages, 0, sizeof(ntagDump.pages));

  for (int p = 0; p < *maxPage; p++) {
    displayProgress("Read NDEF", p + 1, *maxPage, ("Page " + String(p)).c_str());
    bool ok = readNTAGPageRaw(p, ntagDump.pages[p]);
    ntagDump.pageRead[p] = ok;
    if (ok) (*readCount)++;
    else (*failCount)++;
    delay(5);
  }
  ntagDump.totalPages = *maxPage;
  strncpy(ntagDump.typeName, info->typeName, sizeof(ntagDump.typeName));
  return *readCount > 0;
}

String bytesToString(const uint8_t* data, int len) {
  String out = "";
  for (int i = 0; i < len; i++) {
    char c = (char)data[i];
    if (c >= 0x20 && c < 0x7F) out += c;
  }
  return out;
}

bool decodeCachedNDEF(int maxPage, NDEFDecoded* decoded) {
  decoded->ok = false;
  decoded->recordType = 0;
  decoded->label = "Raw NDEF";
  decoded->value = "";

  uint8_t buf[160];
  int len = 0;
  for (int p = 4; p < maxPage && len + 4 <= (int)sizeof(buf); p++) {
    for (int j = 0; j < 4; j++) buf[len++] = ntagDump.pageRead[p] ? ntagDump.pages[p][j] : 0;
  }

  for (int i = 0; i < len;) {
    uint8_t tlv = buf[i++];
    if (tlv == 0x00) continue;
    if (tlv == 0xFE) break;
    if (tlv != 0x03 || i >= len) continue;

    int tlvLen = buf[i++];
    if (tlvLen == 0xFF) {
      if (i + 1 >= len) return false;
      tlvLen = ((int)buf[i] << 8) | buf[i + 1];
      i += 2;
    }
    if (tlvLen <= 0 || i + tlvLen > len) return false;

    int r = i;
    uint8_t flags = buf[r++];
    if ((flags & 0x10) == 0) return false; // short records only
    uint8_t typeLen = buf[r++];
    uint8_t payloadLen = buf[r++];
    uint8_t idLen = 0;
    if (flags & 0x08) idLen = buf[r++];
    if (typeLen != 1 || r + typeLen + idLen + payloadLen > i + tlvLen) return false;
    char type = (char)buf[r++];
    r += idLen;

    if (type == 'U' && payloadLen >= 1) {
      decoded->ok = true;
      decoded->recordType = 'U';
      decoded->label = "NDEF URL";
      decoded->value = ndefUriPrefix(buf[r]) + bytesToString(&buf[r + 1], payloadLen - 1);
      return true;
    }
    if (type == 'T' && payloadLen >= 1) {
      uint8_t status = buf[r++];
      int langLen = status & 0x3F;
      if (payloadLen < 1 + langLen) return false;
      decoded->ok = true;
      decoded->recordType = 'T';
      decoded->label = "NDEF Text";
      decoded->value = bytesToString(&buf[r + langLen], payloadLen - 1 - langLen);
      return true;
    }
    return false;
  }
  return false;
}

void showRawNDEFPages(const String& title, int maxPage) {
  int viewPage = 0;
  while (true) {
    display.clearDisplay();
    drawBorder();
    display.setCursor(4, 4);
    display.println(title);
    display.drawLine(0, 14, SCREEN_WIDTH, 14, SSD1306_WHITE);

    for (int row = 0; row < 4 && (viewPage + row) < maxPage; row++) {
      int p = viewPage + row;
      display.setCursor(4, 18 + row * 10);
      display.print(p < 10 ? "P0" : "P"); display.print(p); display.print(":");
      if (ntagDump.pageRead[p]) {
        for (int j = 0; j < 4; j++) {
          if (ntagDump.pages[p][j] < 0x10) display.print("0");
          display.print(ntagDump.pages[p][j], HEX); display.print(" ");
        }
      } else {
        display.print("-- -- -- --");
      }
    }
    display.setCursor(4, 56);
    display.println("U/D:Scroll S:Exit");
    display.display();

    int btn = getButtonInput();
    if (btn == BUTTON_DOWN && viewPage + 4 < maxPage) viewPage += 4;
    if (btn == BUTTON_UP   && viewPage > 0)           viewPage -= 4;
    if (btn == BUTTON_SELECT) break;
    delay(50);
  }
}

void showDecodedNDEFOrRaw(CardInfo* info, int maxPage, NDEFDecoded* decoded) {
  if (decoded->ok) {
    displayInfo(decoded->label,
                decoded->value.substring(0, 20),
                decoded->value.substring(20, 40),
                "Select:Raw");
    while (getButtonInput() != BUTTON_SELECT) delay(40);
  }
  showRawNDEFPages(String(info->typeName) + " NDEF", maxPage);
}

void dumpNTAG(uint8_t* uid, uint8_t uidLength, CardInfo* info) {
  memcpy(ntagDump.uid, uid, uidLength);
  ntagDump.uidLength  = uidLength;
  ntagDump.totalPages = info->totalPages;
  strncpy(ntagDump.typeName, info->typeName, sizeof(ntagDump.typeName));
  memset(ntagDump.pageRead, false, sizeof(ntagDump.pageRead));

  for (int p = 0; p < info->totalPages; p++) {
    displayProgress("NTAG Dump", p, info->totalPages, ("Page " + String(p)).c_str());
    ntagDump.pageRead[p] = readNTAGPageRaw(p, ntagDump.pages[p]);
    delay(2);
  }

  int readCount = 0;
  for (int p = 0; p < info->totalPages; p++) if (ntagDump.pageRead[p]) readCount++;
  displayInfo("NTAG Dump Done",
              String(readCount) + "/" + String(info->totalPages) + " pages",
              info->typeName);
  delay(2000);
}

void saveNTAGDumpToSD() {
  lastSavedFilename = "";
  lastSavedCompanion = "";
  String binName = generateUniqueFilename("ntg", "bin");
  File f = SD.open("/" + binName, FILE_WRITE);
  if (!f) {
    displayInfo("SD Error", "Cannot save NTAG");
    delay(2000);
    return;
  }

  displayInfo("Saving", binName);
  static const uint8_t blank4[4] = {0, 0, 0, 0};
  for (int p = 0; p < ntagDump.totalPages; p++) {
    f.write(ntagDump.pageRead[p] ? ntagDump.pages[p] : blank4, 4);
  }
  f.close();

  String txtName = binName.substring(0, binName.lastIndexOf('.')) + ".txt";
  File t = SD.open("/" + txtName, FILE_WRITE);
  if (t) {
    t.println("CYPHER NFC NTAG Dump");
    t.println(ntagDump.typeName);
    t.print("UID: ");
    for (int i = 0; i < ntagDump.uidLength; i++) {
      if (ntagDump.uid[i] < 0x10) t.print("0");
      t.print(ntagDump.uid[i], HEX);
    }
    t.println();
    t.println();
    for (int p = 0; p < ntagDump.totalPages; p++) {
      t.print("Pg ");
      if (p < 10)  t.print(" ");
      if (p < 100) t.print(" ");
      t.print(p); t.print(": ");
      for (int j = 0; j < 4; j++) {
        if (ntagDump.pages[p][j] < 0x10) t.print("0");
        t.print(ntagDump.pages[p][j], HEX); t.print(" ");
      }
      t.print("| ");
      for (int j = 0; j < 4; j++) {
        char c = (char)ntagDump.pages[p][j];
        t.print((c >= 0x20 && c < 0x7F) ? c : '.');
      }
      if (!ntagDump.pageRead[p]) t.print(" [UNREAD]");
      t.println();
    }
    t.close();
  }

  lastSavedFilename = binName;
  lastSavedCompanion = txtName;
  displayInfo("Saved!", binName, txtName);
  delay(2500);
}

void readNDEF() {
  uint8_t uid[7]; uint8_t uidLength;
  if (!waitForCard(uid, &uidLength)) { redisplayCurrentMenu(); return; }

  CardInfo info;
  detectCardType(uid, uidLength, &info);

  if (info.totalPages == 0) {
    displayInfo("Not NTAG/UL", info.typeName, "Need 7-byte UID card");
    delay(3000);
    redisplayCurrentMenu();
    return;
  }

  int maxPage = 0;
  int readCount = 0;
  int failCount = 0;
  readNDEFPages(&info, &maxPage, &readCount, &failCount);
  ntagDump.uidLength  = uidLength;
  memcpy(ntagDump.uid, uid, uidLength);

  NDEFDecoded decoded;
  decodeCachedNDEF(maxPage, &decoded);
  appendScanLog(uidToString(uid, uidLength), info.typeName,
                decoded.ok ? "read_ndef_decoded" : "read_ndef_raw");
  showDecodedNDEFOrRaw(&info, maxPage, &decoded);
  delay(200);
  redisplayCurrentMenu();
}

// ============================================================
// MAGIC CARD / CLONE OPS
// ============================================================

// Test if card accepts writes to block 0 (Gen1a magic, CUID/Gen2).
// Real MIFARE Classic rejects block 0 writes at hardware level.
// Magic/CUID cards allow it after standard auth.
bool detectMagicCard(uint8_t* uid, uint8_t uidLength) {
  uint8_t defKey[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  if (!nfc.mifareclassic_AuthenticateBlock(uid, uidLength, 0, 0, defKey)) return false;

  uint8_t block0[16];
  if (!nfc.mifareclassic_ReadDataBlock(0, block0)) return false;

  // Non-destructive test: write block 0 back with same data
  return nfc.mifareclassic_WriteDataBlock(0, block0);
}

bool writeDumpToMagicCard() {
  if (mifDump.totalBlocks == 0) {
    displayInfo("No Dump Loaded", "Load .mfd file", "from Clone menu");
    delay(3000);
    return false;
  }

  uint8_t uid[7]; uint8_t uidLength;
  if (!waitForCard(uid, &uidLength, "Place MAGIC", "card now")) return false;

  displayInfo("Checking Card", "Testing block 0", "writability...");
  if (!detectMagicCard(uid, uidLength)) {
    displayInfo("Not Magic Card!", "Block 0 locked", "Use magic/CUID card");
    delay(3000);
    return false;
  }
  displayInfo("Magic Card OK!", "Starting clone...", "Keep card still");
  delay(1500);

  bool is4K = (mifDump.totalBlocks == 256);
  uint8_t defKey[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

  // Write block 0 (UID block) — magic cards allow this
  nfc.mifareclassic_AuthenticateBlock(uid, uidLength, 0, 0, defKey);
  if (!nfc.mifareclassic_WriteDataBlock(0, mifDump.data[0])) {
    displayInfo("Block 0 Failed", "Continuing...");
    delay(1000);
  }

  // Write data blocks (skip trailers, write those last)
  for (int b = 1; b < mifDump.totalBlocks; b++) {
    if (!mifDump.blockRead[b]) continue;
    int s       = blockToSector(b, is4K);
    int trailer = sectorTrailerBlock(s, is4K);
    if (b == trailer) continue; // skip sector trailers for now

    displayProgress("Cloning", b, mifDump.totalBlocks, ("Blk " + String(b)).c_str());

    if (!nfc.mifareclassic_AuthenticateBlock(uid, uidLength, b, 0, mifDump.keyUsed[b]))
      nfc.mifareclassic_AuthenticateBlock(uid, uidLength, b, 0, defKey);

    nfc.mifareclassic_WriteDataBlock(b, mifDump.data[b]);
    if (b % 4 == 0) delay(1);
  }

  // Write sector trailers last (to avoid locking ourselves out)
  for (int s = 1; s < totalSectors(is4K); s++) {
    int trailer = sectorTrailerBlock(s, is4K);
    if (!mifDump.blockRead[trailer]) continue;
    nfc.mifareclassic_AuthenticateBlock(uid, uidLength, trailer, 0, defKey);
    nfc.mifareclassic_WriteDataBlock(trailer, mifDump.data[trailer]);
    delay(5);
  }

  displayInfo("Clone Done!", "Run Verify Clone", "to confirm");
  delay(3000);
  return true;
}

// ============================================================
// NDEF WRITE
// ============================================================

String loadNDEFPreset(const char* filename, const String& defaultVal) {
  if (!SD.exists(filename)) return defaultVal;
  File f = SD.open(filename, FILE_READ);
  if (!f) return defaultVal;
  String s = "";
  while (f.available() && s.length() < 60) s += (char)f.read();
  f.close();
  s.trim();
  return (s.length() > 0) ? s : defaultVal;
}

// Build and write an NDEF TLV message to an NTAG/Ultralight card.
// recordType: 'U' (URI) or 'T' (Text)
// For 'U': prefix = URI identifier code (0x04 = https://)
// For 'T': prefix is ignored; lang code "en" is used
// Build a TLV-wrapped NDEF message into msg[] (must hold >=128 bytes).
// recordType: 'U' (URI) or 'T' (Text). Returns total byte count.
// Shared by buildAndWriteNDEF() (writing a card) and the tag emulator.
int buildNDEFMessage(char recordType, uint8_t prefix,
                     const char* payload, uint8_t* msg) {
  // Build the record type-specific payload bytes
  uint8_t pld[64];
  int pldLen = 0;

  if (recordType == 'U') {
    pld[pldLen++] = prefix;
    int urlLen = min((int)strlen(payload), 60);
    memcpy(&pld[pldLen], payload, urlLen);
    pldLen += urlLen;
  } else { // 'T' — Text record
    pld[pldLen++] = 0x02; // language code length = 2
    pld[pldLen++] = 'e';
    pld[pldLen++] = 'n';
    int txtLen = min((int)strlen(payload), 58);
    memcpy(&pld[pldLen], payload, txtLen);
    pldLen += txtLen;
  }

  // NDEF TLV wrapper:
  // [0x03][TLV_len][0xD1][0x01][pldLen][type_char][pld...][0xFE]
  // TLV_len = 4 (record header) + pldLen
  int idx = 0;
  msg[idx++] = 0x03;                    // NDEF Message TLV tag
  msg[idx++] = (uint8_t)(4 + pldLen);  // TLV length
  msg[idx++] = 0xD1;                    // MB=1 ME=1 SR=1 TNF=0x01 (Well Known)
  msg[idx++] = 0x01;                    // Type Length = 1
  msg[idx++] = (uint8_t)pldLen;         // Payload Length (short record)
  msg[idx++] = (uint8_t)recordType;     // Record type: 'U' or 'T'
  memcpy(&msg[idx], pld, pldLen);
  idx += pldLen;
  msg[idx++] = 0xFE;                    // Terminator TLV
  return idx;
}

bool buildAndWriteNDEF(uint8_t* uid, uint8_t uidLength,
                        char recordType, uint8_t prefix,
                        const char* payload) {
  uint8_t msg[128] = {0};
  int totalBytes = buildNDEFMessage(recordType, prefix, payload, msg);

  if (totalBytes > 120) {
    displayInfo("Too Long!", "Max 120 bytes", "Shorten content");
    delay(3000);
    return false;
  }

  // Write pages starting at page 4 (user data area)
  for (int i = 0, page = 4; i < totalBytes; i += 4, page++) {
    uint8_t pageData[4] = {0, 0, 0, 0};
    memcpy(pageData, &msg[i], min(4, totalBytes - i));
    if (!nfc.ntag2xx_WritePage(page, pageData)) {
      displayInfo("Write Failed", "Page " + String(page), "Check card/remove");
      delay(2000);
      return false;
    }
    delay(10);
  }
  return true;
}

void writeNDEFUrl() {
  String url = loadNDEFPreset("/NDEF_URL.TXT",
                              "github.com/dkyazzentwatwa/cypher-pn532");
  NDEFPreset preset = parseNDEFPreset(String("url:") + url);
  String visible = ndefUriPrefix(preset.prefix) + preset.payload;
  displayInfo("NDEF URL", visible.substring(0, 20), "Place NTAG card");
  delay(2000);

  uint8_t uid[7]; uint8_t uidLength;
  if (!waitForCard(uid, &uidLength, "Place NTAG", "card now")) {
    redisplayCurrentMenu(); return;
  }

  displayInfo("Writing URL...");
  bool ok = buildAndWriteNDEF(uid, uidLength, 'U', preset.prefix, preset.payload.c_str());
  appendScanLog(uidToString(uid, uidLength), "NTAG", ok ? "write_url" : "write_url_failed", "NDEF_URL.TXT");
  displayInfo(ok ? "URL Written!" : "Write Failed", visible.substring(0, 20),
              ok ? "Scan with phone!" : "");
  delay(2500);
  redisplayCurrentMenu();
}

void writeNDEFText() {
  String text = loadNDEFPreset("/NDEF_TXT.TXT", "CYPHER NFC 2026");
  displayInfo("NDEF Text", text.substring(0, 20), "Place NTAG card");
  delay(2000);

  uint8_t uid[7]; uint8_t uidLength;
  if (!waitForCard(uid, &uidLength, "Place NTAG", "card now")) {
    redisplayCurrentMenu(); return;
  }

  displayInfo("Writing Text...");
  bool ok = buildAndWriteNDEF(uid, uidLength, 'T', 0, text.c_str());
  appendScanLog(uidToString(uid, uidLength), "NTAG", ok ? "write_text" : "write_text_failed", "NDEF_TXT.TXT");
  displayInfo(ok ? "Text Written!" : "Write Failed", text.substring(0, 20));
  delay(2500);
  redisplayCurrentMenu();
}

void writeNDEFFromSD() {
  displayInfo("NDEF from SD", "Select .txt file", "with your content");
  delay(1500);
  if (!browseFiles(".txt")) { redisplayCurrentMenu(); return; }

  File f = SD.open("/" + fileList[currentFileIndex], FILE_READ);
  if (!f) {
    displayInfo("Error", "Cannot open file");
    delay(2000);
    redisplayCurrentMenu();
    return;
  }
  String content = "";
  while (f.available() && content.length() < 90) content += (char)f.read();
  f.close();
  content.trim();
  NDEFPreset preset = parseNDEFPreset(content);
  String visible = (preset.recordType == 'U') ? ndefUriPrefix(preset.prefix) + preset.payload : preset.payload;

  displayInfo(preset.recordType == 'U' ? "Writing as URL" : "Writing as Text",
              visible.substring(0, 20), "Place NTAG card");
  delay(2000);

  uint8_t uid[7]; uint8_t uidLength;
  if (!waitForCard(uid, &uidLength, "Place NTAG", "card now")) {
    redisplayCurrentMenu(); return;
  }

  bool ok = buildAndWriteNDEF(uid, uidLength, preset.recordType, preset.prefix, preset.payload.c_str());
  appendScanLog(uidToString(uid, uidLength), "NTAG",
                ok ? (preset.recordType == 'U' ? "write_sd_url" : "write_sd_text") : "write_sd_failed",
                fileList[currentFileIndex]);
  displayInfo(ok ? "Written!" : "Failed", visible.substring(0, 20));
  delay(2500);
  redisplayCurrentMenu();
}

// ============================================================
// READ OPS
// ============================================================

void readUIDOnly() {
  uint8_t uid[7]; uint8_t uidLength;
  if (!waitForCard(uid, &uidLength)) { redisplayCurrentMenu(); return; }

  CardInfo info;
  detectCardType(uid, uidLength, &info);

  String uidStr = uidToString(uid, uidLength);
  appendScanLog(uidStr, info.typeName, "uid_only");

  Serial.print("TYPE: "); Serial.println(info.typeName);
  Serial.print("UID:  "); Serial.println(uidStr);

  displayInfo(info.typeName, uidStr.substring(0, 20),
              "Len: " + String(uidLength) + "B", "SELECT:Exit");

  while (digitalRead(BUTTON_SELECT) != LOW) delay(50);
  delay(200);
  redisplayCurrentMenu();
}

void dumpMifareFlow() {
  uint8_t uid[7]; uint8_t uidLength;
  if (!waitForCard(uid, &uidLength, "Place MIFARE", "card now")) {
    redisplayCurrentMenu(); return;
  }

  CardInfo info;
  detectCardType(uid, uidLength, &info);
  if (info.type != CARD_MIFARE_CLASSIC_1K && info.type != CARD_MIFARE_CLASSIC_4K) {
    displayInfo("Not MIFARE!", info.typeName, "Need Classic card");
    delay(3000);
    redisplayCurrentMenu();
    return;
  }

  bool is4K = (info.type == CARD_MIFARE_CLASSIC_4K);
  dumpMifareClassic(uid, uidLength, is4K);
  if (saveMifareDumpToSD(is4K)) {
    appendScanLog(uidToString(uid, uidLength), info.typeName, "dump_mifare", lastSavedFilename);
  }
  redisplayCurrentMenu();
}

void dumpNTAGFlow() {
  uint8_t uid[7]; uint8_t uidLength;
  if (!waitForCard(uid, &uidLength, "Place NTAG", "card now")) {
    redisplayCurrentMenu(); return;
  }

  CardInfo info;
  detectCardType(uid, uidLength, &info);
  if (info.totalPages == 0) {
    displayInfo("Not NTAG!", info.typeName, "Need NTAG card");
    delay(3000);
    redisplayCurrentMenu();
    return;
  }

  dumpNTAG(uid, uidLength, &info);
  saveNTAGDumpToSD();
  appendScanLog(uidToString(uid, uidLength), info.typeName, "dump_ntag", lastSavedFilename);
  redisplayCurrentMenu();
}

// Auto-detect card and dump it (used by Clone > Dump to SD)
void dumpToSDFlow() {
  uint8_t uid[7]; uint8_t uidLength;
  if (!waitForCard(uid, &uidLength)) { redisplayCurrentMenu(); return; }

  CardInfo info;
  detectCardType(uid, uidLength, &info);
  displayInfo(info.typeName, "Starting dump...");
  delay(1000);

  if (info.type == CARD_MIFARE_CLASSIC_1K || info.type == CARD_MIFARE_CLASSIC_4K) {
    bool is4K = (info.type == CARD_MIFARE_CLASSIC_4K);
    dumpMifareClassic(uid, uidLength, is4K);
    if (saveMifareDumpToSD(is4K)) {
      appendScanLog(uidToString(uid, uidLength), info.typeName, "dump_auto", lastSavedFilename);
    }
  } else if (info.totalPages > 0) {
    dumpNTAG(uid, uidLength, &info);
    saveNTAGDumpToSD();
    appendScanLog(uidToString(uid, uidLength), info.typeName, "dump_auto", lastSavedFilename);
  } else {
    displayInfo("Unknown Card", "Cannot dump", info.typeName);
    delay(3000);
  }
  redisplayCurrentMenu();
}

// ============================================================
// DEMO MODE
// ============================================================

void waitForSelectExit() {
  while (getButtonInput() != BUTTON_SELECT) delay(40);
}

void tagStudioFlow(bool puzzleMode) {
  uint8_t uid[7]; uint8_t uidLength;
  if (!waitForCard(uid, &uidLength)) { redisplayCurrentMenu(); return; }

  CardInfo info;
  detectCardType(uid, uidLength, &info);
  String uidStr = uidToString(uid, uidLength);
  String action = puzzleMode ? "puzzle_hunt" : "tag_studio";

  if (info.totalPages > 0) {
    int maxPage = 0, readCount = 0, failCount = 0;
    readNDEFPages(&info, &maxPage, &readCount, &failCount);
    ntagDump.uidLength = uidLength;
    memcpy(ntagDump.uid, uid, uidLength);
    NDEFDecoded decoded;
    decodeCachedNDEF(maxPage, &decoded);
    appendScanLog(uidStr, info.typeName, action, decoded.ok ? decoded.label : "raw");

    if (puzzleMode && decoded.ok) {
      String clue = decoded.value;
      String lower = clue;
      lower.toLowerCase();
      if (lower.startsWith("clue:")) clue = clue.substring(5);
      else if (lower.startsWith("puzzle:")) clue = clue.substring(7);
      clue.trim();
      displayInfo("Puzzle Hunt", clue.substring(0, 20), clue.substring(20, 40), "Select:Exit");
      waitForSelectExit();
    } else if (decoded.ok) {
      displayInfo("Tag Studio", decoded.label,
                  decoded.value.substring(0, 20),
                  decoded.value.substring(20, 40));
      delay(3500);
    } else {
      displayInfo("Tag Studio", info.typeName, "No decoded NDEF", uidStr.substring(0, 20));
      delay(3000);
    }
  } else {
    appendScanLog(uidStr, info.typeName, action);
    displayInfo(puzzleMode ? "Puzzle Hunt" : "Tag Studio",
                info.typeName,
                "UID: " + uidStr.substring(0, 17),
                info.totalBlocks ? String(info.totalBlocks) + " blocks" : "No NDEF");
    delay(3500);
  }
  redisplayCurrentMenu();
}

void demoDumpWebFlow() {
  uint8_t uid[7]; uint8_t uidLength;
  if (!waitForCard(uid, &uidLength)) { redisplayCurrentMenu(); return; }

  CardInfo info;
  detectCardType(uid, uidLength, &info);
  displayInfo("Dump + Web", info.typeName, "Saving to SD...");
  delay(1000);

  if (info.type == CARD_MIFARE_CLASSIC_1K || info.type == CARD_MIFARE_CLASSIC_4K) {
    bool is4K = (info.type == CARD_MIFARE_CLASSIC_4K);
    dumpMifareClassic(uid, uidLength, is4K);
    if (saveMifareDumpToSD(is4K)) {
      appendScanLog(uidToString(uid, uidLength), info.typeName, "demo_dump_web", lastSavedFilename);
    }
  } else if (info.totalPages > 0) {
    dumpNTAG(uid, uidLength, &info);
    saveNTAGDumpToSD();
    appendScanLog(uidToString(uid, uidLength), info.typeName, "demo_dump_web", lastSavedFilename);
  } else {
    displayInfo("Unknown Card", "Cannot dump", info.typeName);
    delay(3000);
    redisplayCurrentMenu();
    return;
  }

  displayInfo("Dump Saved", lastSavedFilename, "View on SD card", "Select:Exit");
  waitForSelectExit();
  redisplayCurrentMenu();
}

// ============================================================
// CARD EMULATION (ISO14443A target mode — NTAG / Type 2 tag)
//
// The PN532 can present itself as a tag via TgInitAsTarget (0x8C) +
// TgGetData (0x86) / TgSetData (0x8E). Adafruit_PN532 doesn't expose the
// target-mode response read (readdata is private), so we drive the chip with
// our own raw-I2C frame helpers below and keep Adafruit_PN532 for everything
// else on the same bus.
//
// LIMITATION: MIFARE Classic emulation is NOT possible — the PN532 firmware
// can't run Crypto1 auth as a target. This emulates an NTAG-style Type 2 tag
// that a phone reads as NDEF. The RF UID is partly chip-generated (only 3
// NFCID1 bytes are controllable), so "UID Only" is a best-effort spoof.
// ============================================================

#define PN532_I2C_ADDRESS 0x24   // 7-bit address (0x48 >> 1)

#define EMU_MAX_PAGES 64                       // covers NTAG213 (45 pp) + NDEF
static uint8_t emuPageImage[EMU_MAX_PAGES * 4];
static int     emuPageCount = 0;

// Send a command (TFI 0xD4 is prepended here) and verify the 6-byte ACK.
bool pn532_sendCmd(const uint8_t* cmd, uint8_t len) {
  uint8_t lenField = len + 1;                  // TFI + cmd bytes
  Wire.beginTransmission(PN532_I2C_ADDRESS);
  Wire.write((uint8_t)0x00);                   // preamble
  Wire.write((uint8_t)0x00);                   // start 1
  Wire.write((uint8_t)0xFF);                   // start 2
  Wire.write(lenField);
  Wire.write((uint8_t)(~lenField + 1));        // length checksum
  Wire.write((uint8_t)0xD4);                   // TFI: host -> PN532
  uint8_t sum = 0xD4;
  for (uint8_t i = 0; i < len; i++) { Wire.write(cmd[i]); sum += cmd[i]; }
  Wire.write((uint8_t)(~sum + 1));             // data checksum
  Wire.write((uint8_t)0x00);                   // postamble
  if (Wire.endTransmission() != 0) return false;

  // Read the ACK frame (00 00 FF 00 FF 00), preceded by an I2C ready byte.
  uint32_t start = millis();
  while (millis() - start < 100) {
    Wire.requestFrom((uint8_t)PN532_I2C_ADDRESS, (uint8_t)7);
    if (Wire.available() >= 7) {
      uint8_t rdy = Wire.read();
      uint8_t a[6];
      for (int i = 0; i < 6; i++) a[i] = Wire.read();
      if ((rdy & 0x01) && a[0] == 0x00 && a[1] == 0x00 && a[2] == 0xFF &&
          a[3] == 0x00 && a[4] == 0xFF) return true;
      return false;
    }
    delay(2);
  }
  return false;
}

// Read a response frame; returns payload length (cmd-response bytes, excluding
// TFI 0xD5 and the response-code byte) into buf, or -1 on timeout/error.
int pn532_readResp(uint8_t* buf, uint8_t maxLen, uint16_t timeoutMs) {
  uint32_t start = millis();
  // Poll the 1-byte ready status.
  while (millis() - start < timeoutMs) {
    Wire.requestFrom((uint8_t)PN532_I2C_ADDRESS, (uint8_t)1);
    if (Wire.available() && (Wire.read() & 0x01)) break;
    if (millis() - start >= timeoutMs) return -1;
    delay(3);
  }
  // Read the full frame: ready + 00 00 FF LEN LCS D5 RC <data..> DCS 00
  uint8_t want = maxLen + 10;
  Wire.requestFrom((uint8_t)PN532_I2C_ADDRESS, want);
  if (Wire.available() < 8) { while (Wire.available()) Wire.read(); return -1; }
  Wire.read();                                  // ready byte
  uint8_t p0 = Wire.read(), p1 = Wire.read(), p2 = Wire.read();
  if (!(p0 == 0x00 && p1 == 0x00 && p2 == 0xFF)) {
    while (Wire.available()) Wire.read(); return -1;
  }
  uint8_t len = Wire.read();
  uint8_t lcs = Wire.read();
  if (((len + lcs) & 0xFF) != 0 || len < 2) {
    while (Wire.available()) Wire.read(); return -1;
  }
  Wire.read();                                  // TFI 0xD5
  Wire.read();                                  // response code (cmd+1)
  int dataLen = len - 2;                        // exclude TFI + response code
  int n = 0;
  for (int i = 0; i < dataLen && Wire.available(); i++) {
    uint8_t b = Wire.read();
    if (n < maxLen) buf[n++] = b;
  }
  while (Wire.available()) Wire.read();          // drain DCS + postamble
  return n;
}

// Configure the PN532 as a passive ISO14443A (Type 2) target.
// Returns true once a reader has activated it.
bool emuInitAsTarget(const uint8_t* uid3, uint16_t timeoutMs) {
  uint8_t cmd[] = {
    0x8C,                                  // TgInitAsTarget
    0x05,                                  // Mode: PICC-only + passive-only
    0x04, 0x00,                            // SENS_RES (ATQA)
    uid3[0], uid3[1], uid3[2],             // NFCID1t (3 controllable UID bytes)
    0x00,                                  // SEL_RES (SAK) 0x00 = Type 2 / NTAG
    // FeliCa params (NFCID2 8 + PAD 8 + system code 2) — unused but required
    0x01, 0xFE, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
    0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
    0xFF, 0xFF,
    // NFCID3t (10 bytes)
    0xAA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00,                                  // length of general bytes
    0x00                                   // length of historical bytes
  };
  if (!pn532_sendCmd(cmd, sizeof(cmd))) return false;
  uint8_t resp[64];
  // Response arrives only once a reader selects us (Mode byte + initiator cmd).
  return pn532_readResp(resp, sizeof(resp), timeoutMs) >= 0;
}

// One TgGetData (0x86): read the reader's command into buf. Returns length.
int emuGetData(uint8_t* buf, uint8_t maxLen, uint16_t timeoutMs) {
  uint8_t cmd = 0x86;
  if (!pn532_sendCmd(&cmd, 1)) return -1;
  int n = pn532_readResp(buf, maxLen, timeoutMs);
  if (n < 1) return -1;
  // buf[0] is the TgGetData status byte; shift it off, leaving reader payload.
  uint8_t status = buf[0];
  for (int i = 1; i < n; i++) buf[i - 1] = buf[i];
  n -= 1;
  if (status & 0x3F) return -1;                 // RF error / field lost
  return n;
}

// One TgSetData (0x8E): send our response payload back to the reader.
bool emuSetData(const uint8_t* data, uint8_t len) {
  uint8_t cmd[1 + 32];
  cmd[0] = 0x8E;
  for (uint8_t i = 0; i < len && i < 32; i++) cmd[1 + i] = data[i];
  if (!pn532_sendCmd(cmd, 1 + len)) return false;
  uint8_t resp[8];
  return pn532_readResp(resp, sizeof(resp), 200) >= 0;
}

// Serve the prebuilt emuPageImage as a Type 2 tag until SELECT is pressed.
void runType2Emulation(const String& sourceLabel, const uint8_t* uid3) {
  displayInfo("Emulating Tag", sourceLabel, "Tap phone now", "SELECT: exit");
  appendScanLog(uidToString(uid3, 3), "NTAG", "emulate", sourceLabel);

  int bytes = emuPageCount * 4;
  while (digitalRead(BUTTON_SELECT) != LOW) {
    // (Re)enter target mode; short timeout so we can poll the exit button.
    if (!emuInitAsTarget(uid3, 1500)) { delay(20); continue; }

    // Serve reader commands until the field drops or SELECT is pressed.
    while (digitalRead(BUTTON_SELECT) != LOW) {
      uint8_t in[64];
      int n = emuGetData(in, sizeof(in), 600);
      if (n < 1) break;                          // field lost -> re-init
      if (in[0] == 0x30 && n >= 2) {             // Type 2 READ <page>
        uint8_t page = in[1];
        uint8_t out[16];
        for (int i = 0; i < 16; i++)
          out[i] = emuPageImage[((page * 4) + i) % bytes];
        emuSetData(out, 16);
      } else if (in[0] == 0x50) {                // HALT
        break;
      } else {
        emuSetData(NULL, 0);                     // NAK / ignore others
      }
    }
  }
  delay(250);                                    // debounce the exit press
  displayInfo("Emulation", "Stopped");
  delay(1200);
  redisplayCurrentMenu();
}

// Reset emuPageImage and lay down the standard NTAG header (UID + CC).
// uid3 receives the 3 controllable UID bytes used for the RF anticollision.
static void emuPrepareHeader(const uint8_t* uid, uint8_t uidLen, uint8_t* uid3) {
  memset(emuPageImage, 0, sizeof(emuPageImage));
  uid3[0] = uidLen > 0 ? uid[0] : 0x04;
  uid3[1] = uidLen > 1 ? uid[1] : 0x12;
  uid3[2] = uidLen > 2 ? uid[2] : 0x34;
  // Pages 0-2: UID + BCC + internal (cosmetic; phones read NDEF, not UID here)
  emuPageImage[0] = uid3[0];
  emuPageImage[1] = uid3[1];
  emuPageImage[2] = uid3[2];
  emuPageImage[3] = uid3[0] ^ uid3[1] ^ uid3[2] ^ 0x88;
  // Page 3: Capability Container — NTAG213, 144-byte user area, read/write
  emuPageImage[12] = 0xE1;
  emuPageImage[13] = 0x10;
  emuPageImage[14] = 0x12;
  emuPageImage[15] = 0x00;
}

// Source 1: NDEF built from the SD presets (/NDEF_URL.TXT or /NDEF_TXT.TXT).
void emulateNdefFromSD() {
  String url  = loadNDEFPreset("/NDEF_URL.TXT", "");
  String text = loadNDEFPreset("/NDEF_TXT.TXT", "");
  NDEFPreset preset = (url.length() > 0)
      ? parseNDEFPreset(String("url:") + url)
      : parseNDEFPreset(String("text:") + (text.length() ? text : String("CYPHER NFC 2026")));

  uint8_t msg[128] = {0};
  int totalBytes = buildNDEFMessage(preset.recordType, preset.prefix,
                                    preset.payload.c_str(), msg);
  // Lay NDEF into the user area from page 4 onward.
  if (totalBytes > (EMU_MAX_PAGES - 4) * 4) totalBytes = (EMU_MAX_PAGES - 4) * 4;

  uint8_t uid3[3];
  uint8_t noUid[1] = {0};
  emuPrepareHeader(noUid, 0, uid3);             // synthetic UID
  memcpy(&emuPageImage[16], msg, totalBytes);   // page 4 = byte offset 16
  emuPageCount = 4 + ((totalBytes + 3) / 4) + 1;
  if (emuPageCount > EMU_MAX_PAGES) emuPageCount = EMU_MAX_PAGES;

  String label = (preset.recordType == 'U') ? "NDEF URL" : "NDEF Text";
  runType2Emulation(label, uid3);
}

// Source 2: replay a saved NTAG dump (.bin = sequential 4-byte pages).
void emulateNtagDumpFromSD() {
  if (!browseFiles(".bin")) { redisplayCurrentMenu(); return; }
  String shortName = fileList[currentFileIndex];
  File f = SD.open("/" + shortName, FILE_READ);
  if (!f) {
    displayInfo("Error", "Cannot open", shortName);
    delay(2000); redisplayCurrentMenu(); return;
  }
  memset(emuPageImage, 0, sizeof(emuPageImage));
  int n = f.read(emuPageImage, sizeof(emuPageImage));
  f.close();
  if (n < 16) {
    displayInfo("Error", "Dump too small", shortName);
    delay(2000); redisplayCurrentMenu(); return;
  }
  emuPageCount = n / 4;
  uint8_t uid3[3] = { emuPageImage[0], emuPageImage[1], emuPageImage[2] };
  runType2Emulation(shortName.substring(0, 14), uid3);
}

// Source 3: spoof a UID from a live scan; empty NDEF body.
void emulateUidOnly() {
  uint8_t uid[7]; uint8_t uidLength;
  if (!waitForCard(uid, &uidLength, "Scan UID to", "emulate")) {
    redisplayCurrentMenu(); return;
  }
  uint8_t uid3[3];
  emuPrepareHeader(uid, uidLength, uid3);
  // Empty NDEF message TLV so a reader sees a valid (blank) tag.
  emuPageImage[16] = 0x03;                       // NDEF TLV, length 0
  emuPageImage[17] = 0x00;
  emuPageImage[18] = 0xFE;                        // terminator
  emuPageCount = 6;
  runType2Emulation("UID: " + uidToString(uid, uidLength).substring(0, 11), uid3);
}

void executeEmulateMenuItem(int idx) {
  switch (idx) {
    case 0: emulateNdefFromSD();      break;
    case 1: emulateNtagDumpFromSD();  break;
    case 2: emulateUidOnly();         break;
    case 3: appState = STATE_MAIN_MENU; redisplayCurrentMenu(); break;
  }
}

void executeDemoMenuItem(int idx) {
  switch (idx) {
    case 0: tagStudioFlow(false); break;
    case 1: demoDumpWebFlow(); break;
    case 2: writeNDEFFromSD(); break;
    case 3: tagStudioFlow(true); break;
    case 4: appState = STATE_MAIN_MENU; redisplayCurrentMenu(); break;
  }
}

// ============================================================
// MENU EXECUTION DISPATCHERS
// ============================================================

void executeReadMenuItem(int idx) {
  switch (idx) {
    case 0: readUIDOnly();   break;
    case 1: readNDEF();      break;
    case 2: dumpMifareFlow(); break;
    case 3: dumpNTAGFlow();  break;
    case 4: appState = STATE_MAIN_MENU; redisplayCurrentMenu(); break;
  }
}

void executeAttackMenuItem(int idx) {
  switch (idx) {
    case 0: dictionaryAttack(); break;
    case 1: appState = STATE_MAIN_MENU; redisplayCurrentMenu(); break;
  }
}

void executeCloneMenuItem(int idx) {
  switch (idx) {
    case 0: dumpToSDFlow(); break;
    case 1:
      if (mifDump.totalBlocks == 0) {
        if (!loadMifareDumpFromSD()) { redisplayCurrentMenu(); break; }
      }
      writeDumpToMagicCard();
      redisplayCurrentMenu();
      break;
    case 2: verifyClone(); break;
    case 3: appState = STATE_MAIN_MENU; redisplayCurrentMenu(); break;
  }
}

void executeWriteMenuItem(int idx) {
  switch (idx) {
    case 0: writeNDEFUrl();    break;
    case 1: writeNDEFText();   break;
    case 2: writeNDEFFromSD(); break;
    case 3: appState = STATE_MAIN_MENU; redisplayCurrentMenu(); break;
  }
}

void executeSDMenuItem(int idx) {
  switch (idx) {
    case 0: viewFiles();  redisplayCurrentMenu(); break;
    case 1: hexViewFile(); redisplayCurrentMenu(); break;
    case 2: deleteFile(); redisplayCurrentMenu(); break;
    case 3: appState = STATE_MAIN_MENU; redisplayCurrentMenu(); break;
  }
}

void executeMainMenuItem(int idx) {
  switch (idx) {
    case 0:
      scanAndInfo();
      break;
    case 1:
      appState = STATE_DEMO_SUBMENU; currentSubMenuItem = 0;
      displayMenuScreen("Demo Mode", demoMenuItems, demoMenuCount, 0);
      break;
    case 2:
      appState = STATE_READ_SUBMENU; currentSubMenuItem = 0;
      displayMenuScreen("Read Card", readMenuItems, readMenuCount, 0);
      break;
    case 3:
      appState = STATE_ATTACK_SUBMENU; currentSubMenuItem = 0;
      displayMenuScreen("Key Attack", attackMenuItems, attackMenuCount, 0);
      break;
    case 4:
      appState = STATE_CLONE_SUBMENU; currentSubMenuItem = 0;
      displayMenuScreen("Clone Card", cloneMenuItems, cloneMenuCount, 0);
      break;
    case 5:
      appState = STATE_WRITE_SUBMENU; currentSubMenuItem = 0;
      displayMenuScreen("Write Card", writeMenuItems, writeMenuCount, 0);
      break;
    case 6:
      appState = STATE_SD_SUBMENU; currentSubMenuItem = 0;
      displayMenuScreen("SD Card", sdMenuItems, sdMenuCount, 0);
      break;
    case 7:
      appState = STATE_EMULATE_SUBMENU; currentSubMenuItem = 0;
      displayMenuScreen("Emulate Tag", emulateMenuItems, emulateMenuCount, 0);
      break;
  }
}

// ============================================================
// STATE MACHINE
// ============================================================

void handleButtonPress() {
  int btn = getButtonInput();
  if (btn == BUTTON_NONE) return;

  // Resolve active menu context
  const char** items  = nullptr;
  int count           = 0;
  const char* title   = "";
  int* selPtr         = &currentMenuItem;

  switch (appState) {
    case STATE_MAIN_MENU:
      items = (const char**)mainMenuItems; count = mainMenuCount;
      title = APP_DISPLAY_NAME; selPtr = &currentMenuItem;
      break;
    case STATE_READ_SUBMENU:
      items = (const char**)readMenuItems; count = readMenuCount;
      title = "Read Card"; selPtr = &currentSubMenuItem;
      break;
    case STATE_DEMO_SUBMENU:
      items = (const char**)demoMenuItems; count = demoMenuCount;
      title = "Demo Mode"; selPtr = &currentSubMenuItem;
      break;
    case STATE_ATTACK_SUBMENU:
      items = (const char**)attackMenuItems; count = attackMenuCount;
      title = "Key Attack"; selPtr = &currentSubMenuItem;
      break;
    case STATE_CLONE_SUBMENU:
      items = (const char**)cloneMenuItems; count = cloneMenuCount;
      title = "Clone Card"; selPtr = &currentSubMenuItem;
      break;
    case STATE_WRITE_SUBMENU:
      items = (const char**)writeMenuItems; count = writeMenuCount;
      title = "Write Card"; selPtr = &currentSubMenuItem;
      break;
    case STATE_SD_SUBMENU:
      items = (const char**)sdMenuItems; count = sdMenuCount;
      title = "SD Card"; selPtr = &currentSubMenuItem;
      break;
    case STATE_EMULATE_SUBMENU:
      items = (const char**)emulateMenuItems; count = emulateMenuCount;
      title = "Emulate Tag"; selPtr = &currentSubMenuItem;
      break;
    default: return;
  }

  if (btn == BUTTON_UP) {
    *selPtr = (*selPtr - 1 + count) % count;
    displayMenuScreen(title, items, count, *selPtr);
  } else if (btn == BUTTON_DOWN) {
    *selPtr = (*selPtr + 1) % count;
    displayMenuScreen(title, items, count, *selPtr);
  } else if (btn == BUTTON_SELECT) {
    switch (appState) {
      case STATE_MAIN_MENU:     executeMainMenuItem(currentMenuItem);      break;
      case STATE_READ_SUBMENU:  executeReadMenuItem(currentSubMenuItem);   break;
      case STATE_DEMO_SUBMENU:  executeDemoMenuItem(currentSubMenuItem);   break;
      case STATE_ATTACK_SUBMENU:executeAttackMenuItem(currentSubMenuItem); break;
      case STATE_CLONE_SUBMENU: executeCloneMenuItem(currentSubMenuItem);  break;
      case STATE_WRITE_SUBMENU: executeWriteMenuItem(currentSubMenuItem);  break;
      case STATE_SD_SUBMENU:    executeSDMenuItem(currentSubMenuItem);     break;
      case STATE_EMULATE_SUBMENU: executeEmulateMenuItem(currentSubMenuItem); break;
    }
  }
}

// ============================================================
// SETUP & LOOP
// ============================================================

void initDisplay() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, SSD1306_I2C_ADDR)) {
    Serial.println("SSD1306 init failed");
    for (;;);
  }
  Serial.printf("SSD1306 ready address=0x%02X size=%dx%d\n", SSD1306_I2C_ADDR, SCREEN_WIDTH, SCREEN_HEIGHT);
  display.display();
  delay(500);
  display.clearDisplay();
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.print(APP_DISPLAY_NAME);
  Serial.println(" boot");
  Serial.printf("I2C SDA=%d SCL=%d clock=%lu\n", I2C_SDA_PIN, I2C_SCL_PIN, (unsigned long)I2C_CLOCK_HZ);
  Serial.printf("SD SPI SCK=%d MISO=%d MOSI=%d CS=%d\n", SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  Serial.printf("Buttons UP=%d DOWN=%d SELECT=%d active_low=1\n", BUTTON_UP, BUTTON_DOWN, BUTTON_SELECT);
  Serial.printf("PN532 IRQ=%d RESET=%d\n", PN532_IRQ, PN532_RESET);
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(I2C_CLOCK_HZ);
  delay(2000);

  pinMode(BUTTON_UP,     INPUT_PULLUP);
  pinMode(BUTTON_DOWN,   INPUT_PULLUP);
  pinMode(BUTTON_SELECT, INPUT_PULLUP);

  initDisplay();
  u8g2_for_adafruit_gfx.begin(display);

  displayTitleScreen();
  delay(2500);

  initSDCard();

  displayInfo("PN532 NFC", "Initializing...");
  nfc.begin();
  delay(1000);

  uint32_t ver = nfc.getFirmwareVersion();
  if (!ver) {
    displayInfo("Error", "PN532 not found", "Check wiring");
    while (1);
  }

  String fw   = "FW: " + String((ver >> 16) & 0xFF) + "." + String((ver >> 8) & 0xFF);
  String chip = "Chip: PN5" + String((ver >> 24) & 0xFF, HEX);
  Serial.printf("PN532 ready chip=PN5%02lX fw=%lu.%lu\n",
                (unsigned long)((ver >> 24) & 0xFF),
                (unsigned long)((ver >> 16) & 0xFF),
                (unsigned long)((ver >> 8) & 0xFF));
  displayInfo("PN532 Ready", chip, fw);
  delay(1500);

  nfc.SAMConfig();

  // Init global state — no dump loaded yet
  mifDump.totalBlocks = 0;
  ntagDump.totalPages = 0;
  keyMap.crackedCount = 0;
  keyMap.numSectors   = 0;

  displayMenuScreen(APP_DISPLAY_NAME, mainMenuItems, mainMenuCount, 0);
}

void loop() {
  handleButtonPress();
}
