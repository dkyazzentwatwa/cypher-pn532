#include <Wire.h>
#include <Adafruit_PN532.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define PN532_IRQ   (2)
#define PN532_RESET (3)

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SSD1306_I2C_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);

void initDisplay() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, SSD1306_I2C_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }
  display.display();
  delay(2000);
  display.clearDisplay();
}

void drawBorder() {
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
}

void displayInfo(String title, String info1 = "", String info2 = "", String info3 = "") {
  display.clearDisplay();
  drawBorder();
  
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  // Title
  display.setCursor(4, 4);
  display.println(title);
  display.drawLine(0, 14, SCREEN_WIDTH, 14, SSD1306_WHITE);
  
  // Info lines
  display.setCursor(4, 18);
  display.println(info1);
  display.setCursor(4, 28);
  display.println(info2);
  display.setCursor(4, 38);
  display.println(info3);
  
  display.display();
}

void setup() {
  Serial.begin(115200);
  delay(5000);
  Serial.println("PN532 NFC Reader");

  initDisplay();
  displayInfo("PN532 NFC Reader", "Initializing...");

  Wire.begin();
  nfc.begin();

  uint32_t versiondata = nfc.getFirmwareVersion();
  delay(5000);
  if (!versiondata) {
    Serial.println("Didn't find PN53x board");
    displayInfo("Error", "PN53x board", "not found");
    while (1);
  }
  
  String fwVersion = "FW: " + String((versiondata>>16) & 0xFF) + "." + String((versiondata>>8) & 0xFF);
  String chipInfo = "Chip: PN5" + String((versiondata>>24) & 0xFF, HEX);
  
  displayInfo("PN532 Info", chipInfo, fwVersion);
  delay(2000);
  
  nfc.SAMConfig();
  
  displayInfo("Ready", "Waiting for card...", "Place card near", "the reader");
}

void loop() {
  uint8_t success;
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
  uint8_t uidLength;
  
  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength);
  
  if (success) {
    String uidString = "";
    for (uint8_t i = 0; i < uidLength; i++) {
      uidString += (uid[i] < 0x10 ? "0" : "") + String(uid[i], HEX) + " ";
    }
    uidString.trim();
    
    String typeString;
    if (uidLength == 4) {
      typeString = "Mifare Classic";
    } else if (uidLength == 7) {
      typeString = "Mifare Ultralight";
    } else {
      typeString = "Unknown type";
    }
    
    displayInfo("Card Detected", "UID: " + uidString, "Type: " + typeString, "Length: " + String(uidLength) + " bytes");
    
    Serial.println("Found card:");
    Serial.println("  UID: " + uidString);
    Serial.println("  Type: " + typeString);
    
    delay(3000);
    displayInfo("Ready", "Waiting for card...", "Place card near", "the reader");
  }
  else
  {
    // No card found, update display every 5 seconds
    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate > 5000) {
      displayInfo("Ready", "Waiting for card...", "Place card near", "the reader");
      lastUpdate = millis();
    }
  }
}