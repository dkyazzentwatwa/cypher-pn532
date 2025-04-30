#include <Wire.h>
#include <Adafruit_PN532.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <SD.h>
#include <SPI.h>
//PN532
#define PN532_IRQ (2)
#define PN532_RESET (3)

//SSD1306 128x64 .96inch screen
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SSD1306_I2C_ADDRESS 0x3C

//SD CARD
#define SD_CS 10
#define SD_MOSI 6  // SD Card MOSI pin
#define SD_MISO 5  // SD Card MISO pin
#define SD_SCK 4   // SD Card SCK pin

//Buttons
#define BUTTON_UP 3
#define BUTTON_DOWN 1
#define BUTTON_SELECT 2


// Variables for NFC data handling
const int MAX_NFC_DATA_SIZE = 16;  // Standard MIFARE Classic block size
char currentFileName[13];          // 8.3 format filename buffer

//Display
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
U8G2_FOR_ADAFRUIT_GFX u8g2_for_adafruit_gfx;
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);

const char* menuItems[] = { "Read", "Read NDEF", "Read/Write", "Erase", "SD Card" };

// Global state variables
bool inMenu = true;
bool inSDMenu = false;
int currentMenuItem = 0;
int currentSDMenuItem = 0;
const int totalMenuItems = 5;
int menuIndex = 0;    // Tracks the current menu option
int SDmenuIndex = 0;  // Tracks the current menu option

// Variables for file navigation
String fileList[20];       // Array to store filenames
int fileCount = 0;         // Number of files found
int currentFileIndex = 0;  // Currently selected file

void initDisplay() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, SSD1306_I2C_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ;
  }
  Serial.println("Display Starting...");
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
  display.setCursor(4, 4);
  display.println(title);
  display.drawLine(0, 14, SCREEN_WIDTH, 14, SSD1306_WHITE);
  display.setCursor(4, 18);
  display.println(info1);
  display.setCursor(4, 28);
  display.println(info2);
  display.setCursor(4, 38);
  display.println(info3);
  display.display();
}

void displayMenu() {
  display.clearDisplay();
  drawBorder();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(4, 4);
  display.println("Menu");
  display.drawLine(0, 14, SCREEN_WIDTH, 14, SSD1306_WHITE);

  // Calculate which items to show based on current selection
  int startIdx = 0;
  if (currentMenuItem > 2) {
    startIdx = currentMenuItem - 2;  // Keep selected item in middle when possible
  }
  if (startIdx + 4 > totalMenuItems) {
    startIdx = totalMenuItems - 4;  // Adjust if near end of list
  }
  if (startIdx < 0) startIdx = 0;  // Safety check

  // Display up to 4 menu items
  for (int i = 0; i < 4 && (startIdx + i) < totalMenuItems; i++) {
    display.setCursor(4, 18 + i * 10);
    if (startIdx + i == currentMenuItem) {
      display.print("> ");
    } else {
      display.print("  ");
    }
    display.println(menuItems[startIdx + i]);
  }

  // Optional: Add scroll indicators if there are more items
  if (startIdx > 0) {
    display.setCursor(120, 18);
    display.print("^");  // Up arrow
  }
  if (startIdx + 4 < totalMenuItems) {
    display.setCursor(120, 48);
    display.print("v");  // Down arrow
  }

  display.display();
}
// Modified executeMenuItem() function
void executeMenuItem() {
  switch (currentMenuItem) {
    case 0:  // Read
      readCard();
      break;
    case 1:  // Read
      readNDEF();
      break;
    case 2:  // Read/Write
      readWriteCard();
      break;
    case 3:  // Erase
      eraseCard();
      break;
    case 4:  // SD Card Menu
      inSDMenu = true;
      inMenu = false;
      currentSDMenuItem = 0;
      displaySDMenuOptions();
      break;
  }
}


void initSDCard() {
  displayInfo("SD Card", "Initializing...", "");
  Serial.println("Beginning SD Card initialization...");

  // Disable any existing SPI devices
  digitalWrite(SD_CS, HIGH);
  delay(100);

  // Force SPI to known state
  SPI.end();
  delay(100);

  // Initialize SPI with explicit settings
  SPISettings spiSettings(4000000, MSBFIRST, SPI_MODE0);

  // Begin SPI with forced pins
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  delay(100);

  // Configure CS pin
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  delay(100);

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  delay(3000);
  // Try to initialize SD card
  if (!SD.begin(SD_CS)) {
    Serial.println("SD Card initialization failed!");
    displayInfo("SD Error", "Init failed!", "Check connection");
    delay(2000);
    return;
  }

  // Get SD card info
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached!");
    displayInfo("SD Error", "No card found!", "Check card");
    delay(2000);
    return;
  }

  // Print card type
  String cardTypeStr = "Unknown";
  switch (cardType) {
    case CARD_MMC: cardTypeStr = "MMC"; break;
    case CARD_SD: cardTypeStr = "SDSC"; break;
    case CARD_SDHC: cardTypeStr = "SDHC"; break;
    default: cardTypeStr = "Unknown"; break;
  }

  // Get card size
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);

  // Display success and card info
  String sizeStr = String(cardSize) + "MB";
  displayInfo("SD Card OK", cardTypeStr, sizeStr);
  delay(2000);

  // Try to open root directory
  File root = SD.open("/");
  if (!root) {
    Serial.println("Failed to open root directory");
    displayInfo("SD Error", "Can't open root", "Format FAT32");
    delay(2000);
    return;
  }
  if (!root.isDirectory()) {
    Serial.println("Root is not a directory");
    displayInfo("SD Error", "Root invalid", "Format FAT32");
    delay(2000);
    return;
  }

  // Count files in root directory
  int fileCount = 0;
  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;
    fileCount++;
    Serial.print("Found file: ");
    Serial.println(entry.name());
    entry.close();
  }
  root.close();

  // Show file count
  Serial.println("SD Ready");
  Serial.print(String(fileCount));
  Serial.print(" files found");
  displayInfo("SD Ready", String(fileCount) + " files", "found");
  delay(2000);
}


void displayCardData() {
  displayInfo("Read Data", "Place card", "near reader");

  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
  uint8_t uidLength;

  // Assuming we write to Block 4
  uint8_t blockNumber = 4;
  uint8_t blockData[16];

  if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength)) {
    if (nfc.mifareclassic_ReadDataBlock(blockNumber, blockData)) {
      String cardData = "";
      for (int i = 0; i < 16; i++) {
        cardData += (char)blockData[i];  // Convert each byte to ASCII
      }
      displayInfo("Card Data:", cardData);
    } else {
      displayInfo("Read Failed!", "Try again.");
    }
  } else {
    displayInfo("No Card", "Found", "Try Again");
  }

  delay(3000);
  displayMenu();
}

void displayTitleScreen() {
  display.clearDisplay();
  u8g2_for_adafruit_gfx.setFont(u8g2_font_adventurer_tr);
  u8g2_for_adafruit_gfx.setCursor(20, 40);
  u8g2_for_adafruit_gfx.print("CYPHER NFC");
  display.display();
}

void displayInfoScreen() {
  display.clearDisplay();
  u8g2_for_adafruit_gfx.setFont(u8g2_font_baby_tf);
  u8g2_for_adafruit_gfx.setCursor(0, 30);
  u8g2_for_adafruit_gfx.print("Welcome to CYPHER NFC!");
  display.display();
}


// Main menu handling
void handleButtonPress() {
  if (digitalRead(BUTTON_UP) == LOW) {
    if (inSDMenu) {
      currentSDMenuItem = (currentSDMenuItem - 1 + totalMenuItems) % totalMenuItems;
      displaySDMenuOptions();
    } else {
      currentMenuItem = (currentMenuItem - 1 + totalMenuItems) % totalMenuItems;
      displayMenu();
    }
    delay(200);
  }

  if (digitalRead(BUTTON_DOWN) == LOW) {
    if (inSDMenu) {
      currentSDMenuItem = (currentSDMenuItem + 1) % totalMenuItems;
      displaySDMenuOptions();
    } else {
      currentMenuItem = (currentMenuItem + 1) % totalMenuItems;
      displayMenu();
    }
    delay(200);
  }

  if (digitalRead(BUTTON_SELECT) == LOW) {
    if (inSDMenu) {
      executeSDMenuAction(currentSDMenuItem);
    } else {
      executeMenuItem();
    }
    delay(200);
  }
}


void readCard() {
  while (true) {  // Keep scanning in a loop
    displayInfo("Read Card", "Place card", "near reader");
    uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
    uint8_t uidLength;
    bool cardRead = false;
    bool saveToSD = false;  // Added missing declaration
    uint8_t blockData[16];  // Added missing declaration
    String uidLen = String(uidLength);

    while (!cardRead) {                               // Keep scanning until a card is read
      uint16_t cardType = nfc.inListPassiveTarget();  // Get card type

      if (cardType) {
        if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength)) {
          String cardTypeStr = (cardType == 0x44) ? "MIFARE Classic" : "ISO 14443-4";
          String uidStr = "";

          for (uint8_t i = 0; i < uidLength; i++) {
            uidStr += String(uid[i], HEX) + " ";
          }

          displayInfo("TYPE / UID / LEN", cardTypeStr, uidStr, uidLen);
          Serial.println("TYPE / UID / LEN");
          Serial.println(cardTypeStr);
          Serial.println(uidStr);
          Serial.println(uidLen);

          delay(3000);
          cardRead = true;  // Set flag to indicate successful read

          // Read block data before saving
          uint8_t keya[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
          if (nfc.mifareclassic_AuthenticateBlock(uid, uidLength, 4, 0, keya)) {
            if (nfc.mifareclassic_ReadDataBlock(4, blockData)) {
              displayInfo("Card Read", "UP: Save to SD", "DOWN: Skip Save");

              // Wait for save decision
              while (true) {
                if (digitalRead(BUTTON_UP) == LOW) {
                  saveToSD = true;
                  break;
                }
                if (digitalRead(BUTTON_DOWN) == LOW) {
                  saveToSD = false;
                  break;
                }
              }

              if (saveToSD) {
                Serial.print("Block Data: ");
                for (int i = 0; i < 16; i++) {
                  Serial.print(blockData[i], HEX);
                  Serial.print(" ");
                }
                Serial.println();
                Serial.print("size of Block Data");
                Serial.print(sizeof(blockData));
                delay(3000);
                if (saveNFCDataToSD(blockData, sizeof(blockData))) {
                  displayInfo("Save Success", currentFileName, "Data saved");
                } else {
                  displayInfo("Save Failed", "SD Card Error", "Check card");
                }
                delay(2000);
              }
            }
          }

          break;  // Exit inner loop after successful read
        }
      }
      delay(100);  // Small delay to prevent too frequent scanning
    }

    if (!cardRead) {
      displayInfo("No Card", "Found", "Try Again");
      delay(3000);
    }

    if (cardRead) {
      delay(3000);    // Wait before returning to the main menu
      displayMenu();  // Call main menu function
      break;          // Exit the outer loop after successful read
    }
  }
}


void readNDEF() {
  displayInfo("Read NDEF", "Place card", "near reader");
  uint8_t success;
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };  // Buffer to store the returned UID
  uint8_t uidLength;                        // Length of the UID (4 or 7 bytes depending on ISO14443A card type)
  bool cardRead = false;                    // Flag to indicate a successful read

  // Wait for an NTAG203 card. When one is found, 'uid' will be populated with
  // the UID, and uidLength will indicate the size of the UUID (normally 7 bytes)
  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength);

  if (success) {
    cardRead = true;  // Set flag to indicate a successful read
    // Display card detection message
    String uidLenStr = String(uidLength) + " bytes";
    Serial.println("Found an ISO14443A card");
    Serial.print("  UID Length: ");
    Serial.print(uidLength, DEC);
    Serial.println(" bytes");
    displayInfo("Card Found", "UID Length:", uidLenStr);
    delay(2000);
    // Display UID value
    Serial.print("  UID Value: ");
    nfc.PrintHex(uid, uidLength);
    Serial.println("");
    String uidStr = "";
    for (uint8_t i = 0; i < uidLength; i++) {
      String byteStr = String(uid[i], HEX);
      if (byteStr.length() < 2) {
        byteStr = "0" + byteStr;
      }
      uidStr += byteStr + " ";
    }
    displayInfo("UID Value", uidStr);
    delay(2000);

    if (uidLength == 7) {
      uint8_t data[32];
      Serial.println("Seems to be an NTAG2xx tag (7 byte UID)");
      displayInfo("Tag Type", "NTAG2xx detected");

      // NTAG2x3 cards have 39*4 bytes of user pages (156 user bytes),
      // starting at page 4 ... larger cards just add pages to the end of
      // this range:

      // See: http://www.nxp.com/documents/short_data_sheet/NTAG203_SDS.pdf

      // TAG Type       PAGES   USER START    USER STOP
      // --------       -----   ----------    ---------
      // NTAG 203       42      4             39
      // NTAG 213       45      4             39
      // NTAG 215       135     4             129
      // NTAG 216       231     4             225

      for (uint8_t i = 0; i < 42; i++) {
        success = nfc.ntag2xx_ReadPage(i, data);

        // Display the current page number
        String pageStr = "PAGE ";
        if (i < 10) {
          pageStr += "0";
        }
        pageStr += String(i);
        Serial.print(pageStr + ": ");
        displayInfo("Reading Page", pageStr);

        // Display the results, depending on 'success'
        if (success) {
          // Dump the page data
          nfc.PrintHexChar(data, 4);
          String pageDataStr = "";
          for (int j = 0; j < 4; j++) {
            String byteStr = String(data[j], HEX);
            if (byteStr.length() < 2) {
              byteStr = "0" + byteStr;
            }
            pageDataStr += byteStr + " ";
          }
          displayInfo(pageStr, pageDataStr);
          delay(1000);  // Increased delay to see each page
        } else {
          Serial.println("Unable to read the requested page!");
          displayInfo("Error", "Unable to read", "page " + String(i));
          delay(2000);
        }
      }
    } else {
      Serial.println("This doesn't seem to be an NTAG203 tag (UUID length != 7 bytes)!");
      displayInfo("Error", "Not NTAG203", "UID != 7 bytes");
      delay(3000);
    }

    // Wait a bit before trying again
    Serial.println("\n\nSend a character to scan another tag!");
    Serial.flush();
    while (!Serial.available())
      ;
    while (Serial.available()) {
      Serial.read();
    }
    Serial.flush();
    if (cardRead) {
      displayInfo("Returning", "Main Menu", "Loading...");
      delay(2000);    // Wait briefly before returning to main menu
      displayMenu();  // Call main menu function
    }
  } else if (digitalRead(BUTTON_DOWN) == LOW) {
    displayInfo("Returning", "Main Menu", "Loading...");
    Serial.println("Returning to menu");
    delay(2000);    // Wait briefly before returning to main menu
    displayMenu();  // Call main menu function
  } else {
    displayInfo("No Card", "Detected");
    delay(1000);  // Give some time for the message to be displayed
  }
}


// Test code to send APDU Commands
/*
void readNDEF3() {
  uint8_t success;
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
  uint8_t uidLength;

  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength);
  delay(100);

  if (success) {
    Serial.println("\nFound a card!");
    Serial.print("UID Length: ");
    Serial.print(uidLength, DEC);
    Serial.println(" bytes");
    Serial.print("UID Value: ");
    printHexArray(uid, uidLength);

    delay(200);

    if (!selectCard()) {
      Serial.println("Failed to select card");
      return;
    }

    // First select MF (Master File)
    if (!selectFile(0x3F, 0x00)) {
      Serial.println("Failed to select MF");
      return;
    }

    // Try to select common AIDs (Application Identifiers)
    // These are examples - you might need to use different AIDs depending on your card
    const uint8_t commonAIDs[][2] = {
      { 0x10, 0x01 },
      { 0x10, 0x00 },
      { 0xA0, 0x00 },
      { 0x3F, 0x01 }
    };

    bool foundValidAID = false;

    for (int i = 0; i < sizeof(commonAIDs) / sizeof(commonAIDs[0]); i++) {
      Serial.print("Trying AID: ");
      Serial.print(commonAIDs[i][0], HEX);
      Serial.println(commonAIDs[i][1], HEX);

      if (selectFile(commonAIDs[i][0], commonAIDs[i][1])) {
        foundValidAID = true;
        Serial.println("Successfully selected AID");

        // Try to read the directory
        readDirectory();
        break;
      }
    }

    if (!foundValidAID) {
      Serial.println("Could not find a valid AID");
    }
  }
}

bool selectFile(uint8_t fid1, uint8_t fid2) {
  uint8_t selectCmd[] = {
    0x00,       // CLA
    0xA4,       // INS (SELECT)
    0x00,       // P1
    0x00,       // P2
    0x02,       // Lc
    fid1, fid2  // File ID
  };

  uint8_t response[255];
  uint8_t responseLength = 255;

  if (sendApduWithRetry(selectCmd, sizeof(selectCmd), response, &responseLength)) {
    if (responseLength >= 2) {
      uint8_t sw1 = response[responseLength - 2];
      uint8_t sw2 = response[responseLength - 1];

      Serial.print("Select response SW1SW2: ");
      if (sw1 < 0x10) Serial.print("0");
      Serial.print(sw1, HEX);
      if (sw2 < 0x10) Serial.print("0");
      Serial.println(sw2, HEX);

      return (sw1 == 0x90 && sw2 == 0x00);
    }
  }
  return false;
}

void readDirectory() {
  // First try to read directory information
  uint8_t readBinary[] = {
    0x00,  // CLA
    0xB0,  // INS (READ BINARY)
    0x00,  // P1
    0x00,  // P2
    0x00   // Le (length to read)
  };

  uint8_t response[255];
  uint8_t responseLength = 255;

  if (sendApduWithRetry(readBinary, sizeof(readBinary), response, &responseLength)) {
    Serial.println("Directory content:");
    printHexArray(response, responseLength - 2);

    // Now try to read records
    readRecords();
  } else {
    Serial.println("Failed to read directory. Trying records directly...");
    readRecords();
  }
}

void readRecords() {
  uint8_t recordNumber = 1;
  bool continueReading = true;

  while (continueReading && recordNumber <= 255) {
    uint8_t readRecord[] = {
      0x00,          // CLA
      0xB2,          // INS (READ RECORD)
      recordNumber,  // P1: Record Number
      0x04,          // P2: Reading method (0x04 = read record P1)
      0x00           // Le: Expected length (0x00 = all available bytes)
    };

    uint8_t response[255];
    uint8_t responseLength = 255;

    Serial.print("\nReading record #");
    Serial.println(recordNumber);

    if (sendApduWithRetry(readRecord, sizeof(readRecord), response, &responseLength)) {
      if (responseLength >= 2) {
        uint8_t sw1 = response[responseLength - 2];
        uint8_t sw2 = response[responseLength - 1];

        Serial.print("SW1SW2: ");
        if (sw1 < 0x10) Serial.print("0");
        Serial.print(sw1, HEX);
        if (sw2 < 0x10) Serial.print("0");
        Serial.println(sw2, HEX);

        if (sw1 == 0x90 && sw2 == 0x00) {
          Serial.print("Record content: ");
          printHexArray(response, responseLength - 2);

          Serial.print("ASCII: ");
          for (int i = 0; i < responseLength - 2; i++) {
            if (isPrintable(response[i])) {
              Serial.print((char)response[i]);
            } else {
              Serial.print('.');
            }
          }
          Serial.println();
        } else if (sw1 == 0x6A && sw2 == 0x83) {
          Serial.println("No more records found");
          continueReading = false;
        } else {
          Serial.print("Error reading record. SW1SW2: ");
          if (sw1 < 0x10) Serial.print("0");
          Serial.print(sw1, HEX);
          if (sw2 < 0x10) Serial.print("0");
          Serial.println(sw2, HEX);
          continueReading = false;
        }
      }
    } else {
      Serial.println("Failed to read record");
      continueReading = false;
    }

    recordNumber++;
    delay(200);
  }
}

bool selectCard() {
  int retries = 3;
  while (retries > 0) {
    if (nfc.inListPassiveTarget()) {
      Serial.println("Card selected successfully");
      delay(200);
      return true;
    }
    retries--;
    delay(500);
  }
  return false;
}

bool sendApduWithRetry(uint8_t* apdu, uint8_t apduLength, uint8_t* response, uint8_t* responseLength) {
  int retries = 3;
  bool success = false;

  while (retries > 0 && !success) {
    Serial.print("\nSending APDU (tries remaining: ");
    Serial.print(retries);
    Serial.println(")");

    // Print APDU being sent
    Serial.print("APDU: ");
    for (int i = 0; i < apduLength; i++) {
      if (apdu[i] < 0x10) Serial.print("0");
      Serial.print(apdu[i], HEX);
      Serial.print(" ");
    }
    Serial.println();

    delay(100);  // Delay before sending
    success = nfc.inDataExchange(apdu, apduLength, response, responseLength);
    delay(200);  // Delay after sending

    if (success) {
      Serial.println("APDU sent successfully");
      if (*responseLength >= 2) {
        uint8_t sw1 = response[*responseLength - 2];
        uint8_t sw2 = response[*responseLength - 1];
        Serial.print("Status Words: ");
        if (sw1 < 0x10) Serial.print("0");
        Serial.print(sw1, HEX);
        if (sw2 < 0x10) Serial.print("0");
        Serial.println(sw2, HEX);

        // Check if we need to retry based on status words
        if (sw1 == 0x6F || sw1 == 0x6A) {
          success = false;
          Serial.println("Received error status, will retry");
        }
      }
    } else {
      Serial.println("Failed to send APDU");
    }

    if (!success) {
      retries--;
      if (retries > 0) {
        delay(500);  // Longer delay between retries
      }
    }
  }

  return success;
}

void printHexArray(uint8_t* data, uint8_t length) {
  for (uint8_t i = 0; i < length; i++) {
    if (data[i] < 0x10) Serial.print("0");
    Serial.print(data[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
}
/*
void readRecords() {
  // Read Record APDU template
  uint8_t readRecord[] = {
    0x00, // CLA
    0xB2, // INS (READ RECORD)
    0x01, // P1 (Record Number)
    0x04, // P2 (SFI)
    0x00  // Le (Expected length)
  };
  
  uint8_t response[255];
  uint8_t responseLength;
  
  // Try reading first few records
  for (uint8_t record = 1; record <= 3; record++) {
    readRecord[2] = record; // Set record number
    
    Serial.print("\nReading record "); 
    Serial.println(record);
    
    if (sendApdu(readRecord, sizeof(readRecord), response, &responseLength)) {
      Serial.print("Record "); 
      Serial.print(record);
      Serial.println(" contents:");
      printHexArray(response, responseLength);
      
      // Parse and print readable content
      parseRecord(response, responseLength);
    } else {
      break; // Stop if reading fails
    }
  }
}

void parseRecord(uint8_t *data, uint8_t length) {
  Serial.println("Parsed data:");
  
  // Check for text data
  bool isText = true;
  for (int i = 0; i < length - 2; i++) { // -2 to skip status words
    if (data[i] < 32 || data[i] > 126) {
      isText = false;
      break;
    }
  }
  
  if (isText) {
    Serial.print("Text: ");
    for (int i = 0; i < length - 2; i++) {
      Serial.print((char)data[i]);
    }
    Serial.println();
  } else {
    // Try to identify record type based on first few bytes
    if (length > 2) {
      switch (data[0]) {
        case 0x6F:
          Serial.println("FCI Template");
          break;
        case 0x77:
          Serial.println("Proprietary Template");
          break;
        case 0x70:
          Serial.println("Record Template");
          break;
        default:
          Serial.println("Unknown record type");
      }
    }
  }
  
  // Print status words
  if (length >= 2) {
    Serial.print("Status Words: ");
    if (data[length-2] < 0x10) Serial.print("0");
    Serial.print(data[length-2], HEX);
    if (data[length-1] < 0x10) Serial.print("0");
    Serial.println(data[length-1], HEX);
  }
}
*/


void readWriteCard() {
  while (true) {
    displayInfo("Read/Write", "Place card", "near reader");
    uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
    uint8_t uidLength;
    bool cardRead = false;
    uint8_t blockNumber = 4;  // Define the block number we are working with


    // First phase: Read card and authenticate
    while (!cardRead) {
      if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength)) {
        String uidStr = "";
        for (uint8_t i = 0; i < uidLength; i++) {
          String byteStr = String(uid[i], HEX);
          if (byteStr.length() < 2) {
            byteStr = "0" + byteStr;
          }
          uidStr += byteStr + " ";
        }
        displayInfo("Card Found!", "Authenticating", "...");
        Serial.print("UID: ");
        Serial.println(uidStr);
        delay(1000);

        // Authenticate with default key for block 4 (sector 1)
        uint8_t keya[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
        uint8_t sector = 1;  // Sector containing block 4
        uint8_t blockNumber = 4;
        if (!nfc.mifareclassic_AuthenticateBlock(uid, uidLength, blockNumber, 0, keya)) {
          displayInfo("Auth Failed!", "Check card", "and key");
          Serial.println("Authentication failed for block " + String(blockNumber));
          delay(2000);
          break;  // Exit inner loop to try reading again
        } else {
          Serial.println("Authentication successful for block " + String(blockNumber));
        }

        // Read current data from block 4
        uint8_t currentData[16];
        if (nfc.mifareclassic_ReadDataBlock(blockNumber, currentData)) {
          Serial.print("Current data on block " + String(blockNumber) + ": ");
          for (int i = 0; i < 16; i++) {
            Serial.print((char)currentData[i]);
          }
          Serial.println();
          displayInfo("Current Data", "Read Success", "Preparing write");
          delay(2000);
          cardRead = true;
        } else {
          displayInfo("Read Failed", "Please try", "again");
          Serial.println("Failed to read block " + String(blockNumber));
          delay(2000);
          break;  // Exit inner loop to try reading again
        }
      } else {
        displayInfo("Waiting...", "Place card", "near reader");
        delay(250);
      }
    }

    if (!cardRead) {
      continue;  // Go back to the beginning of the outer loop if card wasn't read
    }

    // Second phase: Write new data to block 4
    displayInfo("Writing", "New Data", "...");
    uint8_t newData[16] = { 'O', 'P', 'E', 'N', 'A', 'I', '-', 'N', 'F', 'C', '!', '!', '!', '!', '!', 0 };
    Serial.print("Writing data to block " + String(blockNumber) + ": ");
    for (int i = 0; i < 16; i++) {
      Serial.print((char)newData[i]);
    }
    Serial.println();

    if (!nfc.mifareclassic_WriteDataBlock(blockNumber, newData)) {
      displayInfo("Write Failed!", "Try again", "");
      Serial.println("Failed to write to block " + String(blockNumber));
      delay(2000);
      continue;  // Go back to the beginning of the outer loop
    } else {
      Serial.println("Write successful to block " + String(blockNumber));
      displayInfo("Write Success!", "Verifying...", "");
      delay(1000);
    }

    // Third phase: Verify the write by reading block 4 again
    uint8_t verifyData[16];
    if (!nfc.mifareclassic_ReadDataBlock(blockNumber, verifyData)) {
      displayInfo("Verify Failed!", "Cannot read", "Try again");
      Serial.println("Failed to read block " + String(blockNumber) + " for verification");
      delay(2000);
      continue;  // Go back to the beginning of the outer loop
    } else {
      Serial.print("Data read for verification from block " + String(blockNumber) + ": ");
      for (int i = 0; i < 16; i++) {
        Serial.print((char)verifyData[i]);
      }
      Serial.println();
    }

    // Compare written data
    bool writeVerified = true;
    for (int i = 0; i < 16; i++) {
      if (verifyData[i] != newData[i]) {
        writeVerified = false;
        break;
      }
    }

    if (writeVerified) {
      displayInfo("Success!", "Write", "Verified");
      Serial.println("Write verified successfully!");
      delay(3000);
      break;  // Exit the main loop on success
    } else {
      displayInfo("Verify Failed!", "Data mismatch", "Try again");
      Serial.println("Verification failed. Data mismatch!");
      delay(2000);
      continue;  // Go back to the beginning of the outer loop
    }
  }

  displayMenu();  // Return to menu after successful write
}

void eraseCard() {
  displayInfo("Erase Card", "Place card", "near reader");

  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
  uint8_t uidLength;
  if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength)) {
    uint8_t emptyBlock[16] = { 0 };
    if (nfc.mifareclassic_WriteDataBlock(4, emptyBlock)) {
      displayInfo("Erase Success!", "Block Erased.");
      delay(3000);
    } else {
      displayInfo("Erase Failed!", "Try again.");
    }
  } else {
    displayInfo("No Card", "Found", "Try Again");
  }

  delay(3000);
  displayMenu();
}


// SD CARD

// Function to generate unique filename for NFC data
void generateFileName(String& fileName) {
  static int fileCounter = 0;  // Static variable to keep track of file count
  fileName = "nfc.txt";        // Create a unique file name
}

bool saveNFCDataToSD(uint8_t* data, uint8_t dataLength) {
  String currentFileName;             // Declare a variable to hold the file name
  generateFileName(currentFileName);  // Generate a new file name

  Serial.print("Opening: ");
  Serial.println(currentFileName);

  File dataFile = SD.open(currentFileName, FILE_WRITE);
  if (dataFile) {
    Serial.println("Writing data to SD...");

    for (uint8_t i = 0; i < dataLength; i++) {
      dataFile.print(String(data[i], HEX));  // Convert each byte to hex and write
      dataFile.print(" ");                   // Optional: Add space between bytes for readability
    }
    dataFile.close();
    Serial.println("Data written successfully");

    return true;
  }

  /*if (!dataFile) {
        Serial.println("Failed to open file for writing");
        return false;  // Return false if the file cannot be opened
    }*/
  /*
  Serial.println("Writing data to SD...");
  dataFile.write(data, dataLength);
  dataFile.close();
  Serial.println("Data written successfully");*/
  return false;
}
// Function to read NFC data from SD card
bool readNFCDataFromSD(const char* filename, uint8_t* data, uint8_t* dataLength) {
  File dataFile = SD.open(filename);
  if (!dataFile) {
    return false;
  }

  *dataLength = 0;
  while (dataFile.available() && *dataLength < MAX_NFC_DATA_SIZE) {
    data[*dataLength] = dataFile.read();
    (*dataLength)++;
  }

  dataFile.close();
  return true;
}


void sdCardMenu() {
  SDmenuIndex = 0;  // Reset the menu index when entering the SD menu
  // Call this function periodically from the main loop
  displaySDMenuOptions();  // Show current menu

  int button = getButtonInput();  // Get button input

  if (button == BUTTON_UP) {
    menuIndex = (menuIndex == 0) ? totalMenuItems - 1 : menuIndex - 1;
    delay(100);  // Short debounce delay
  } else if (button == BUTTON_DOWN) {
    menuIndex = (menuIndex == totalMenuItems - 1) ? 0 : menuIndex + 1;
    delay(100);  // Short debounce delay
  } else if (button == BUTTON_SELECT) {
    executeSDMenuAction(menuIndex);  // Execute the selected action
    delay(100);                      // Short debounce delay
  }

  // Call this function continuously in your main loop, without blocking
}
const char* sdMenuOptions[] = {
  "View Files",
  "Delete Files",
  "Load Data",
  "Back to Main Menu"
};

/*
void displayMenuOptions() {
  displayInfo("SD Card Menu", sdMenuOptions[menuIndex]);
}*/

void displaySDMenuOptions() {
  display.clearDisplay();
  drawBorder();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(4, 4);
  display.println("SD Card");
  display.drawLine(0, 14, SCREEN_WIDTH, 14, SSD1306_WHITE);

  // Calculate which items to show based on current selection
  int startIdx = 0;
  if (currentSDMenuItem > 2) {
    startIdx = currentSDMenuItem - 2;
  }
  if (startIdx + 4 > totalMenuItems) {
    startIdx = totalMenuItems - 4;
  }
  if (startIdx < 0) startIdx = 0;

  // Display up to 4 menu items
  for (int i = 0; i < 4 && (startIdx + i) < totalMenuItems; i++) {
    display.setCursor(4, 18 + i * 10);
    if (startIdx + i == currentSDMenuItem) {
      display.print("> ");
    } else {
      display.print("  ");
    }
    display.println(sdMenuOptions[startIdx + i]);
  }

  // Optional: Add scroll indicators if there are more items
  if (startIdx > 0) {
    display.setCursor(120, 18);
    display.print("^");
  }
  if (startIdx + 4 < totalMenuItems) {
    display.setCursor(120, 48);
    display.print("v");
  }

  display.display();
}

int getButtonInput() {
  if (digitalRead(BUTTON_UP) == LOW) {
    delay(200);                                           // Debounce delay
    if (digitalRead(BUTTON_UP) == LOW) return BUTTON_UP;  // Ensure it's still pressed
  }

  if (digitalRead(BUTTON_DOWN) == LOW) {
    delay(200);  // Debounce delay
    if (digitalRead(BUTTON_DOWN) == LOW) return BUTTON_DOWN;
  }

  if (digitalRead(BUTTON_SELECT) == LOW) {
    delay(200);  // Debounce delay
    if (digitalRead(BUTTON_SELECT) == LOW) return BUTTON_SELECT;
  }

  return 0;  // No button pressed
}


void executeSDMenuAction(int option) {
  switch (option) {
    case 0:
      viewFiles();
      break;
    case 1:
      deleteFile();
      break;
    case 2:
      loadData();
      break;
    case 3:  // Back to main menu
      inMenu = true;
      inSDMenu = false;
      displayMenu();
      break;
  }
}

void viewFiles() {
  fileCount = 0;
  currentFileIndex = 0;

  File root = SD.open("/");
  if (!root) {
    Serial.println("Failed to open root directory");
    displayInfo("SD Error", "Can't open root", "Check format");
    delay(2000);
    return;
  }

  if (!root.isDirectory()) {
    root.close();
    Serial.println("Root is not a directory");
    displayInfo("SD Error", "Invalid root", "Check format");
    delay(2000);
    return;
  }

  Serial.println("Reading directory contents:");

  // Populate fileList with file names on the SD card
  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;

    if (!entry.isDirectory() && fileCount < 50) {
      fileList[fileCount] = String(entry.name());
      fileCount++;
    }
    entry.close();
  }
  root.close();

  if (fileCount == 0) {
    displayInfo("No Files", "SD card is", "empty");
    delay(2000);
    return;
  }

  // Enter file list view
  bool inFileView = true;
  while (inFileView) {
    displayFileList();

    int button = getButtonInput();

    if (button == BUTTON_UP) {
      currentFileIndex = (currentFileIndex == 0) ? fileCount - 1 : currentFileIndex - 1;
      delay(150);  // Debounce delay
    } else if (button == BUTTON_DOWN) {
      currentFileIndex = (currentFileIndex == fileCount - 1) ? 0 : currentFileIndex + 1;
      delay(150);  // Debounce delay
    } else if (button == BUTTON_SELECT) {
      inFileView = false;  // Exit file list view
    }
  }
}


void displayFileList() {
  display.clearDisplay();
  drawBorder();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Display title
  display.setCursor(4, 4);
  display.println("Files on SD Card");
  display.drawLine(0, 14, SCREEN_WIDTH, 14, SSD1306_WHITE);

  // Display files (3 at a time)
  int startIndex = max(0, currentFileIndex - 1);
  for (int i = 0; i < 3 && (startIndex + i) < fileCount; i++) {
    display.setCursor(4, 18 + (i * 10));
    if (startIndex + i == currentFileIndex) {
      display.print("> ");
    } else {
      display.print("  ");
    }
    // Truncate filename if too long
    String filename = fileList[startIndex + i];
    if (filename.length() > 18) {
      filename = filename.substring(0, 15) + "...";
    }
    display.println(filename);
  }

  // Display navigation hints
  display.setCursor(4, 54);
  display.println("UP/DOWN:Nav SELECT:Exit");

  display.display();
}

void deleteFile() {
  // First view and select file
  viewFiles();
  if (fileCount == 0) return;

  // Confirm deletion
  displayInfo("Delete File?", fileList[currentFileIndex], "SELECT:Yes UP:No");

  while (true) {
    if (digitalRead(BUTTON_SELECT) == LOW) {
      delay(200);
      if (SD.remove("/" + fileList[currentFileIndex])) {
        displayInfo("Success", "File deleted", fileList[currentFileIndex]);
      } else {
        displayInfo("Error", "Could not", "delete file");
      }
      delay(2000);
      break;
    }
    if (digitalRead(BUTTON_UP) == LOW) {
      delay(200);
      displayInfo("Cancelled", "File not", "deleted");
      delay(2000);
      break;
    }
  }
}

void loadData() {
  // First view and select file
  viewFiles();
  if (fileCount == 0) return;

  String selectedFile = fileList[currentFileIndex];
  File dataFile = SD.open("/" + selectedFile);

  if (dataFile) {
    displayInfo("Loading", selectedFile, "Please wait...");

    // Read file contents
    String content = "";
    while (dataFile.available()) {
      char c = dataFile.read();
      if (content.length() < 100) {  // Prevent buffer overflow
        content += c;
      }
    }
    dataFile.close();

    // Display first part of file contents
    displayInfo("File Contents", content.substring(0, 40), "SELECT:Exit");

    while (digitalRead(BUTTON_SELECT) != LOW) {
      delay(50);  // Wait for button press
    }
    delay(200);  // Debounce
  } else {
    displayInfo("Error", "Could not", "open file");
    delay(2000);
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(8, 9);
  Wire.setClock(100000);
  delay(3000);
  /*
  if (!SD.begin(5)) {  // Change '5' to your actual SD card CS pin if different
    Serial.println("SD Card initialization failed!");
    Serial.println("Trying again...")
    delay(3000);
  }
  Serial.println("SD Card initialized.");
  */
  pinMode(BUTTON_UP, INPUT_PULLUP);
  pinMode(BUTTON_DOWN, INPUT_PULLUP);
  pinMode(BUTTON_SELECT, INPUT_PULLUP);

  initDisplay();
  u8g2_for_adafruit_gfx.begin(display);

  displayTitleScreen();
  delay(3000);
  displayInfoScreen();
  delay(5000);
  initSDCard();

  displayInfo("PN532 NFC", "Initializing...");

  nfc.begin();
  delay(2000);
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    displayInfo("Error", "PN532 not", "found");
    while (1)
      ;
  }

  String fwVersion = "FW: " + String((versiondata >> 16) & 0xFF) + "." + String((versiondata >> 8) & 0xFF);
  String chipInfo = "Chip: PN5" + String((versiondata >> 24) & 0xFF, HEX);
  displayInfo("PN532 Info", chipInfo, fwVersion);
  delay(2000);

  nfc.SAMConfig();
  displayMenu();
}
void loop() {
  handleButtonPress();  // Main menu logic
}




/* In progress

void dumpCard() {
  displayInfo("Dump Card", "Place card", "near reader");

  // MIFARE Classic key A (default key)
  uint8_t keyA[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
  uint8_t uidLength;

  if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength)) {
    String uidStr = "";
    for (uint8_t i = 0; i < uidLength; i++) {
      uidStr += String(uid[i], HEX) + " ";
    }
    Serial.println("Dumping card...");

    // Loop through each block (16 sectors * 4 blocks = 64 blocks)
    for (uint8_t block = 0; block < 64; block++) {
      // Authenticate each sector (every 4 blocks is a new sector)
      if (nfc.mifareclassic_AuthenticateBlock(uid, uidLength, block, 0, keyA)) {
        uint8_t blockData[16];
        // If authentication is successful, read the data block
        if (nfc.mifareclassic_ReadDataBlock(block, blockData)) {
          Serial.print("Block ");
          Serial.print(block);
          Serial.print(": ");
          for (int i = 0; i < 16; i++) {
            Serial.print(blockData[i], HEX);
            Serial.print(" ");
          }
          Serial.println();
        } else {
          Serial.print("Failed to read block ");
          Serial.println(block);
        }
      } else {
        Serial.print("Authentication failed for block ");
        Serial.println(block);
      }
    }

    displayInfo("Dump Success!", "UID:", uidStr);
    delay(3000);
  } else {
    displayInfo("No Card", "Found", "Try Again");
  }

  delay(3000);
  displayMenu();
}

*/
