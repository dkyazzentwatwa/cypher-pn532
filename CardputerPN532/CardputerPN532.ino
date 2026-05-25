// ============================================================
// CYPHER PN532 — Cardputer ADV port (2026)
// M5Stack Cardputer ADV + PN532 over EXT I2C + built-in SD
//
// HARDWARE WIRING NOTE:
//   PN532 VCC -> EXT pin 6  5VOUT
//   PN532 GND -> EXT pin 4  GND
//   PN532 SDA -> EXT pin 8  G8 / I2C_SDA
//   PN532 SCL -> EXT pin 10 G9 / I2C_SCL
//   Leave RESET / INT / BUSY unconnected for this build.
//   Do not use EXT pin 2 5VIN to power the PN532 module.
//
// NDEF PRESETS (optional SD card files):
//   /cypher-pn532/NDEF_URL.TXT or /NDEF_URL.TXT
//   /cypher-pn532/NDEF_TXT.TXT or /NDEF_TXT.TXT
// ============================================================

#include <Arduino.h>
#include <M5Cardputer.h>
#include <SD.h>
#include <SPI.h>
#include <WebServer.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#if __has_include(<CypherPuterReturn.h>)
#include <CypherPuterReturn.h>
#endif

// --- PN532 (I2C) ---
static constexpr uint8_t PN532_I2C_ADDRESS = 0x24;
static constexpr uint32_t PN532_I2C_FREQ = 100000;
static constexpr uint8_t PN532_PACKBUFFSIZ = 64;
static constexpr uint8_t PN532_PREAMBLE = 0x00;
static constexpr uint8_t PN532_STARTCODE1 = 0x00;
static constexpr uint8_t PN532_STARTCODE2 = 0xFF;
static constexpr uint8_t PN532_POSTAMBLE = 0x00;
static constexpr uint8_t PN532_HOSTTOPN532 = 0xD4;
static constexpr uint8_t PN532_PN532TOHOST = 0xD5;
static constexpr uint8_t PN532_COMMAND_GETFIRMWAREVERSION = 0x02;
static constexpr uint8_t PN532_COMMAND_SAMCONFIGURATION = 0x14;
static constexpr uint8_t PN532_COMMAND_RFCONFIGURATION = 0x32;
static constexpr uint8_t PN532_COMMAND_INLISTPASSIVETARGET = 0x4A;
static constexpr uint8_t PN532_COMMAND_INDATAEXCHANGE = 0x40;
static constexpr uint8_t PN532_RESPONSE_INLISTPASSIVETARGET = 0x4B;
static constexpr uint8_t PN532_RESPONSE_INDATAEXCHANGE = 0x41;
static constexpr uint8_t PN532_I2C_BUSY = 0x00;
static constexpr uint8_t PN532_I2C_READY = 0x01;
static constexpr uint8_t PN532_MIFARE_ISO14443A = 0x00;
static constexpr uint8_t MIFARE_CMD_AUTH_A = 0x60;
static constexpr uint8_t MIFARE_CMD_AUTH_B = 0x61;
static constexpr uint8_t MIFARE_CMD_READ = 0x30;
static constexpr uint8_t MIFARE_CMD_WRITE = 0xA0;
static constexpr uint8_t MIFARE_ULTRALIGHT_CMD_WRITE = 0xA2;

// --- Cardputer Display ---
#define SCREEN_WIDTH     240
#define SCREEN_HEIGHT    135
#define SSD1306_WHITE    TFT_WHITE
#define SSD1306_BLACK    TFT_BLACK

// --- SD Card (SPI) ---
#define SD_CS    12
#define SD_MOSI  14
#define SD_MISO  39
#define SD_SCK   40
#define SD_POWER_HOLD 5

static constexpr uint32_t SERIAL_BAUD = 115200;
static constexpr const char* APP_DIR = "/cypher-pn532";
static constexpr const char* WEB_AP_SSID = "CYPHER-PN532";
static constexpr const char* WEB_AP_PASS = "cypher532";
static const IPAddress WEB_AP_IP(192, 168, 4, 1);
static const IPAddress WEB_AP_GATEWAY(192, 168, 4, 1);
static const IPAddress WEB_AP_SUBNET(255, 255, 255, 0);
static constexpr uint8_t PN532_BOOT_ATTEMPTS = 3;
static constexpr uint8_t PN532_READ_FAIL_LIMIT = 4;

enum ButtonInput {
  BUTTON_NONE = 0,
  BUTTON_UP,
  BUTTON_DOWN,
  BUTTON_SELECT,
  BUTTON_BACK
};

class CardputerDisplayCompat {
 public:
  void clearDisplay() { M5Cardputer.Display.fillScreen(TFT_BLACK); }
  void display() {}
  void setTextSize(uint8_t size) { M5Cardputer.Display.setTextSize(size); }
  void setTextColor(uint16_t color) { M5Cardputer.Display.setTextColor(color); }
  void setTextColor(uint16_t color, uint16_t bg) { M5Cardputer.Display.setTextColor(color, bg); }
  void setCursor(int16_t x, int16_t y) { M5Cardputer.Display.setCursor(x, y); }
  void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    M5Cardputer.Display.drawRect(x, y, w, h, color);
  }
  void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
    M5Cardputer.Display.drawLine(x0, y0, x1, y1, color);
  }
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    M5Cardputer.Display.fillRect(x, y, w, h, color);
  }
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
    M5Cardputer.Display.drawFastHLine(x, y, w, color);
  }
  void fillScreen(uint16_t color) { M5Cardputer.Display.fillScreen(color); }

  template <typename T>
  size_t print(const T& value) { return M5Cardputer.Display.print(value); }
  size_t print(unsigned char value, int base) { return M5Cardputer.Display.print(value, base); }
  size_t print(int value, int base) { return M5Cardputer.Display.print(value, base); }
  size_t print(unsigned int value, int base) { return M5Cardputer.Display.print(value, base); }
  size_t print(long value, int base) { return M5Cardputer.Display.print(value, base); }
  size_t print(unsigned long value, int base) { return M5Cardputer.Display.print(value, base); }

  template <typename T>
  size_t println(const T& value) { return M5Cardputer.Display.println(value); }
  size_t println() { return M5Cardputer.Display.println(); }
  size_t println(unsigned char value, int base) { return M5Cardputer.Display.println(value, base); }
  size_t println(int value, int base) { return M5Cardputer.Display.println(value, base); }
  size_t println(unsigned int value, int base) { return M5Cardputer.Display.println(value, base); }
  size_t println(long value, int base) { return M5Cardputer.Display.println(value, base); }
  size_t println(unsigned long value, int base) { return M5Cardputer.Display.println(value, base); }
};

class M5Pn532 {
 public:
  bool begin() {
    delay(80);
    return SAMConfig();
  }

  bool recoverBus() {
    M5Cardputer.In_I2C.stop();
    M5Cardputer.In_I2C.release();
    delay(20);
    bool ok = M5Cardputer.In_I2C.begin();
    delay(20);
    ok = ok && M5Cardputer.In_I2C.scanID(PN532_I2C_ADDRESS, PN532_I2C_FREQ);
    transportFault_ = !ok;
    return ok;
  }

  bool probe() {
    bool ok = M5Cardputer.In_I2C.scanID(PN532_I2C_ADDRESS, PN532_I2C_FREQ);
    if (!ok) transportFault_ = true;
    return ok;
  }

  bool hadTransportFault() const {
    return transportFault_;
  }

  void clearFault() {
    transportFault_ = false;
  }

  bool SAMConfig() {
    clearFault();
    packet_[0] = PN532_COMMAND_SAMCONFIGURATION;
    packet_[1] = 0x01;
    packet_[2] = 0x14;
    packet_[3] = 0x00;  // IRQ disabled; this port polls the I2C ready byte.
    if (!sendCommandCheckAck(packet_, 4)) return false;
    if (!readdata(packet_, 9)) return false;
    if (packet_[6] != 0x15) {
      noteFault();
      return false;
    }
    return true;
  }

  uint32_t getFirmwareVersion() {
    clearFault();
    packet_[0] = PN532_COMMAND_GETFIRMWAREVERSION;
    if (!sendCommandCheckAck(packet_, 1)) return 0;
    if (!readdata(packet_, 13)) return 0;
    const uint8_t expected[] = {0x00, 0x00, 0xFF, 0x06, 0xFA, 0xD5};
    if (memcmp(packet_, expected, sizeof(expected)) != 0) {
      noteFault();
      return 0;
    }

    uint32_t response = packet_[7];
    response <<= 8;
    response |= packet_[8];
    response <<= 8;
    response |= packet_[9];
    response <<= 8;
    response |= packet_[10];
    return response;
  }

  bool setPassiveActivationRetries(uint8_t maxRetries) {
    clearFault();
    packet_[0] = PN532_COMMAND_RFCONFIGURATION;
    packet_[1] = 5;
    packet_[2] = 0xFF;
    packet_[3] = 0x01;
    packet_[4] = maxRetries;
    return sendCommandCheckAck(packet_, 5);
  }

  bool setParameters(uint8_t flags) {
    clearFault();
    packet_[0] = 0x12;
    packet_[1] = flags;
    if (!sendCommandCheckAck(packet_, 2)) return false;
    if (!readdata(packet_, 9)) return false;
    return packet_[6] == 0x13;
  }

  bool readPassiveTargetID(uint8_t cardbaudrate, uint8_t* uid, uint8_t* uidLength,
                           uint16_t timeout = 120) {
    clearFault();
    packet_[0] = PN532_COMMAND_INLISTPASSIVETARGET;
    packet_[1] = 1;
    packet_[2] = cardbaudrate;
    if (!sendCommandCheckAck(packet_, 3, timeout, false)) return false;

    if (!readdata(packet_, 20)) return false;
    if (packet_[0] != 0 || packet_[1] != 0 || packet_[2] != 0xFF) return false;
    if (packet_[5] != PN532_PN532TOHOST || packet_[6] != PN532_RESPONSE_INLISTPASSIVETARGET) return false;
    if (packet_[7] != 1) return false;
    inListedTag_ = packet_[8];
    *uidLength = packet_[12];
    if (*uidLength > 7) return false;
    memcpy(uid, packet_ + 13, *uidLength);
    return true;
  }

  bool inDataExchange(uint8_t* send, uint8_t sendLength, uint8_t* response,
                      uint8_t* responseLength) {
    clearFault();
    if (sendLength > PN532_PACKBUFFSIZ - 2) return false;
    packet_[0] = PN532_COMMAND_INDATAEXCHANGE;
    packet_[1] = inListedTag_;
    memcpy(packet_ + 2, send, sendLength);
    if (!sendCommandCheckAck(packet_, sendLength + 2, 1000)) return false;
    if (!waitready(1000, true)) return false;
    if (!readdata(packet_, sizeof(packet_))) return false;

    if (packet_[0] != 0 || packet_[1] != 0 || packet_[2] != 0xFF) {
      noteFault();
      return false;
    }
    uint8_t length = packet_[3];
    if (packet_[4] != (uint8_t)(~length + 1)) {
      noteFault();
      return false;
    }
    if (packet_[5] != PN532_PN532TOHOST || packet_[6] != PN532_RESPONSE_INDATAEXCHANGE) {
      noteFault();
      return false;
    }
    if ((packet_[7] & 0x3F) != 0) return false;

    length -= 3;
    if (length > *responseLength) length = *responseLength;
    memcpy(response, packet_ + 8, length);
    *responseLength = length;
    return true;
  }

  uint8_t mifareclassic_AuthenticateBlock(uint8_t* uid, uint8_t uidLen,
                                          uint32_t blockNumber, uint8_t keyNumber,
                                          uint8_t* keyData) {
    clearFault();
    packet_[0] = PN532_COMMAND_INDATAEXCHANGE;
    packet_[1] = 1;
    packet_[2] = keyNumber ? MIFARE_CMD_AUTH_B : MIFARE_CMD_AUTH_A;
    packet_[3] = blockNumber;
    memcpy(packet_ + 4, keyData, 6);
    memcpy(packet_ + 10, uid, uidLen);
    if (!sendCommandCheckAck(packet_, 10 + uidLen)) return 0;
    if (!readdata(packet_, 12)) return 0;
    return packet_[7] == 0x00;
  }

  uint8_t mifareclassic_ReadDataBlock(uint8_t blockNumber, uint8_t* data) {
    clearFault();
    packet_[0] = PN532_COMMAND_INDATAEXCHANGE;
    packet_[1] = 1;
    packet_[2] = MIFARE_CMD_READ;
    packet_[3] = blockNumber;
    if (!sendCommandCheckAck(packet_, 4)) return 0;
    if (!readdata(packet_, 26)) return 0;
    if (packet_[7] != 0x00) return 0;
    memcpy(data, packet_ + 8, 16);
    return 1;
  }

  uint8_t mifareclassic_WriteDataBlock(uint8_t blockNumber, uint8_t* data) {
    clearFault();
    packet_[0] = PN532_COMMAND_INDATAEXCHANGE;
    packet_[1] = 1;
    packet_[2] = MIFARE_CMD_WRITE;
    packet_[3] = blockNumber;
    memcpy(packet_ + 4, data, 16);
    if (!sendCommandCheckAck(packet_, 20)) return 0;
    delay(10);
    if (!readdata(packet_, 26)) return 0;
    return packet_[7] == 0x00;
  }

  uint8_t ntag2xx_ReadPage(uint8_t page, uint8_t* buffer) {
    clearFault();
    if (page >= 231) return 0;
    packet_[0] = PN532_COMMAND_INDATAEXCHANGE;
    packet_[1] = 1;
    packet_[2] = MIFARE_CMD_READ;
    packet_[3] = page;
    if (!sendCommandCheckAck(packet_, 4)) return 0;
    if (!readdata(packet_, 26)) return 0;
    if (packet_[7] != 0x00) return 0;
    memcpy(buffer, packet_ + 8, 4);
    return 1;
  }

  uint8_t ntag2xx_WritePage(uint8_t page, uint8_t* data) {
    clearFault();
    if (page < 4 || page > 225) return 0;
    packet_[0] = PN532_COMMAND_INDATAEXCHANGE;
    packet_[1] = 1;
    packet_[2] = MIFARE_ULTRALIGHT_CMD_WRITE;
    packet_[3] = page;
    memcpy(packet_ + 4, data, 4);
    if (!sendCommandCheckAck(packet_, 8)) return 0;
    delay(10);
    if (!readdata(packet_, 26)) return 0;
    return packet_[7] == 0x00;
  }

  // ---- Target mode (card emulation) ----
  // Present as a passive ISO14443A (Type 2 / NTAG) target. Returns true once a
  // reader activates us. MIFARE Classic emulation is NOT supported by the chip.
  bool tgInitAsTarget(const uint8_t* uid3, uint16_t timeout) {
    clearFault();
    uint8_t cmd[37];
    int i = 0;
    cmd[i++] = 0x8C;                       // TgInitAsTarget
    cmd[i++] = 0x05;                       // Mode: PICC-only + passive-only
    cmd[i++] = 0x04; cmd[i++] = 0x00;      // SENS_RES (ATQA)
    cmd[i++] = uid3[0]; cmd[i++] = uid3[1]; cmd[i++] = uid3[2];  // NFCID1t
    cmd[i++] = 0x00;                       // SEL_RES (SAK) 0x00 = Type 2
    static const uint8_t fel[18] = {
      0x01, 0xFE, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
      0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xFF, 0xFF };
    memcpy(cmd + i, fel, 18); i += 18;
    for (int k = 0; k < 10; k++) cmd[i++] = (k == 0) ? 0xAA : 0x00;  // NFCID3t
    cmd[i++] = 0x00;                       // length of general bytes
    cmd[i++] = 0x00;                       // length of historical bytes
    if (!sendCommandCheckAck(cmd, i, timeout, false)) return false;
    if (!waitready(timeout, false)) return false;
    return readdata(packet_, 32);          // mode byte + initiator command
  }

  bool tgInitAsType4Target(const uint8_t* uid3, uint16_t timeout) {
    clearFault();
    if (!setParameters(0x34)) return false;  // auto ATR, auto RATS, ISO14443-4 PICC
    uint8_t cmd[37];
    int i = 0;
    cmd[i++] = 0x8C;                       // TgInitAsTarget
    cmd[i++] = 0x05;                       // Mode: PICC-only + passive-only
    cmd[i++] = 0x08; cmd[i++] = 0x00;      // SENS_RES (ATQA) for Type 4A
    cmd[i++] = uid3[0]; cmd[i++] = uid3[1]; cmd[i++] = uid3[2];  // NFCID1t
    cmd[i++] = 0x20;                       // SEL_RES (SAK) ISO14443-4 compliant
    static const uint8_t fel[18] = {
      0x01, 0xFE, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
      0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xFF, 0xFF };
    memcpy(cmd + i, fel, 18); i += 18;
    for (int k = 0; k < 10; k++) cmd[i++] = (k == 0) ? 0xAA : 0x00;  // NFCID3t
    cmd[i++] = 0x00;                       // length of general bytes
    cmd[i++] = 0x00;                       // length of historical bytes
    if (!sendCommandCheckAck(cmd, i, timeout, false)) return false;
    if (!waitready(timeout, false)) return false;
    return readdata(packet_, 32);
  }

  // TgGetData (0x86): read the reader's command. Returns payload length or -1.
  int tgGetData(uint8_t* buf, uint8_t maxLen, uint16_t timeout) {
    clearFault();
    packet_[0] = 0x86;
    if (!sendCommandCheckAck(packet_, 1, timeout, false)) return -1;
    if (!waitready(timeout, false)) return -1;
    if (!readdata(packet_, sizeof(packet_))) return -1;
    if (packet_[0] != 0 || packet_[1] != 0 || packet_[2] != 0xFF) return -1;
    uint8_t len = packet_[3];
    if (packet_[5] != PN532_PN532TOHOST || packet_[6] != 0x87) return -1;
    if (packet_[7] & 0x3F) return -1;      // status: RF error / field lost
    int dataLen = (int)len - 3;            // exclude TFI + response code + status
    if (dataLen < 0) dataLen = 0;
    if (dataLen > maxLen) dataLen = maxLen;
    memcpy(buf, packet_ + 8, dataLen);
    return dataLen;
  }

  // TgSetData (0x8E): send our response payload back to the reader.
  bool tgSetData(const uint8_t* data, uint8_t len, uint16_t timeout) {
    clearFault();
    packet_[0] = 0x8E;
    if (len > PN532_PACKBUFFSIZ - 1) len = PN532_PACKBUFFSIZ - 1;
    memcpy(packet_ + 1, data, len);
    if (!sendCommandCheckAck(packet_, len + 1, timeout, false)) return false;
    if (!waitready(timeout, false)) return false;
    return readdata(packet_, 9);
  }

 private:
  void noteFault() {
    transportFault_ = true;
  }

  bool sendCommandCheckAck(uint8_t* cmd, uint8_t cmdlen, uint16_t timeout = 1000,
                           bool timeoutIsFault = true) {
    if (!writecommand(cmd, cmdlen)) return false;
    delay(1);
    if (!waitready(timeout, timeoutIsFault)) return false;
    if (!readack()) return false;
    delay(1);
    return waitready(timeout, timeoutIsFault);
  }

  bool isready() {
    uint8_t rdy = PN532_I2C_BUSY;
    if (!readBytes(&rdy, 1)) return false;
    return rdy == PN532_I2C_READY;
  }

  bool waitready(uint16_t timeout, bool timeoutIsFault = true) {
    uint32_t start = millis();
    while (!isready()) {
      if (timeout != 0 && millis() - start > timeout) {
        if (timeoutIsFault) noteFault();
        return false;
      }
      M5Cardputer.update();
      delay(10);
    }
    return true;
  }

  bool readack() {
    uint8_t ack[6];
    static const uint8_t expected[] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
    if (!readdata(ack, sizeof(ack))) return false;
    bool ok = memcmp(ack, expected, sizeof(expected)) == 0;
    if (!ok) noteFault();
    return ok;
  }

  bool readdata(uint8_t* buff, uint8_t n) {
    uint8_t raw[PN532_PACKBUFFSIZ + 1] = {0};
    if (n > PN532_PACKBUFFSIZ) n = PN532_PACKBUFFSIZ;
    if (!readBytes(raw, n + 1)) {
      memset(buff, 0, n);
      noteFault();
      return false;
    }
    if (raw[0] != PN532_I2C_READY) {
      memset(buff, 0, n);
      noteFault();
      return false;
    }
    memcpy(buff, raw + 1, n);
    return true;
  }

  bool writecommand(uint8_t* cmd, uint8_t cmdlen) {
    uint8_t packet[8 + PN532_PACKBUFFSIZ] = {0};
    if (cmdlen > PN532_PACKBUFFSIZ) cmdlen = PN532_PACKBUFFSIZ;
    const uint8_t len = cmdlen + 1;
    packet[0] = PN532_PREAMBLE;
    packet[1] = PN532_STARTCODE1;
    packet[2] = PN532_STARTCODE2;
    packet[3] = len;
    packet[4] = ~len + 1;
    packet[5] = PN532_HOSTTOPN532;
    uint8_t sum = PN532_HOSTTOPN532;
    for (uint8_t i = 0; i < cmdlen; ++i) {
      packet[6 + i] = cmd[i];
      sum += cmd[i];
    }
    packet[6 + cmdlen] = ~sum + 1;
    packet[7 + cmdlen] = PN532_POSTAMBLE;
    if (!writeBytes(packet, 8 + cmdlen)) {
      noteFault();
      return false;
    }
    return true;
  }

  bool readBytes(uint8_t* data, size_t len) {
    if (!M5Cardputer.In_I2C.start(PN532_I2C_ADDRESS, true, PN532_I2C_FREQ)) return false;
    bool ok = M5Cardputer.In_I2C.read(data, len, true);
    return M5Cardputer.In_I2C.stop() && ok;
  }

  bool writeBytes(const uint8_t* data, size_t len) {
    if (!M5Cardputer.In_I2C.start(PN532_I2C_ADDRESS, false, PN532_I2C_FREQ)) return false;
    bool ok = M5Cardputer.In_I2C.write(data, len);
    return M5Cardputer.In_I2C.stop() && ok;
  }

  uint8_t packet_[PN532_PACKBUFFSIZ] = {0};
  uint8_t inListedTag_ = 1;
  bool transportFault_ = false;
};

// --- Global Objects ---
CardputerDisplayCompat display;
M5Pn532 nfc;
WebServer webServer(80);
static constexpr const char* BLE_DEVICE_NAME = "CYPHER-PN532";
static constexpr const char* BLE_NUS_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static constexpr const char* BLE_NUS_RX_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
static constexpr const char* BLE_NUS_TX_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";
NimBLEServer* bleServer = nullptr;
NimBLECharacteristic* bleTxCharacteristic = nullptr;
bool pn532Ready = false;
bool sdReady = false;
uint32_t pn532FirmwareVersion = 0;
extern bool bleSerialActive;
extern bool bleClientConnected;
extern uint16_t bleConnHandle;

void queueBleSerialInput(const std::string& value);

class CypherBleServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
    bleClientConnected = true;
    bleConnHandle = connInfo.getConnHandle();
  }

  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    bleClientConnected = false;
    bleConnHandle = 0xFFFF;
    if (bleSerialActive) {
      NimBLEDevice::startAdvertising();
    }
  }
};

class CypherBleRxCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
    queueBleSerialInput(pCharacteristic->getValue());
  }
};

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
  STATE_EMULATE_SUBMENU,
  STATE_APDU_SUBMENU
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

enum Type4SelectedFile {
  TYPE4_FILE_NONE,
  TYPE4_FILE_CC,
  TYPE4_FILE_NDEF
};

enum CommandTransport {
  TRANSPORT_USB,
  TRANSPORT_BLE
};

// ============================================================
// MENU DATA
// ============================================================

const char* mainMenuItems[]   = {
  "Scan & Info", "Demo Mode", "Read Card", "Key Attack", "Clone Card",
  "Write Card", "SD Card", "Emulate Tag", "APDU Lab", "Web Control", "BLE Serial", "Return to Cypher OS"
};
const int   mainMenuCount     = 12;

const char* demoMenuItems[]   = { "Tag Studio", "Dump + Web", "Badge Writer", "Puzzle Hunt", "Back" };
const int   demoMenuCount     = 5;

const char* emulateMenuItems[] = { "NDEF from SD", "NTAG Dump", "UID Only", "Back" };
const int   emulateMenuCount   = 4;

const char* apduMenuItems[] = { "Type4 NDEF Probe", "Select NDEF AID", "Back" };
const int   apduMenuCount   = 3;

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
String filePathList[20];
int fileCount       = 0;
int currentFileIndex= 0;
String lastSavedFilename = "";
String lastSavedCompanion = "";
String lastSavedJson = "";
bool operationBusy = false;
String lastOperationJson = "{\"ok\":true,\"op\":\"boot\",\"message\":\"ready\"}";
String serialLineBuffer = "";
bool bleSerialActive = false;
bool bleClientConnected = false;
uint16_t bleConnHandle = 0xFFFF;
String bleLineBuffer = "";
String bleCommandQueue[4];
uint8_t bleCommandHead = 0;
uint8_t bleCommandTail = 0;
uint8_t bleCommandCount = 0;

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

struct OperationArgs {
  String key[12];
  String value[12];
  int count;
};

// Global NFC data buffers (kept as globals to avoid large stack allocations)
MifareDump   mifDump;
NTAGDump     ntagDump;
SectorKeyMap keyMap;
uint8_t sdKeys[64][6];
int sdKeyCount = 0;
bool sdKeysLoaded = false;

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
  display.setCursor(8, 8);
  display.println(title);
  display.drawLine(0, 24, SCREEN_WIDTH, 24, SSD1306_WHITE);
  if (line1.length()) { display.setCursor(8, 36); display.println(line1); }
  if (line2.length()) { display.setCursor(8, 52); display.println(line2); }
  if (line3.length()) { display.setCursor(8, 68); display.println(line3); }
  display.display();
}

void displayMenuScreen(const char* title, const char* items[], int count, int selected) {
  display.clearDisplay();
  drawBorder();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(8, 8);
  display.println(title);
  display.drawLine(0, 24, SCREEN_WIDTH, 24, SSD1306_WHITE);

  const int visibleRows = 7;
  int startIdx = 0;
  if (selected > 3) startIdx = selected - 3;
  if (startIdx + visibleRows > count) startIdx = max(0, count - visibleRows);

  for (int i = 0; i < visibleRows && (startIdx + i) < count; i++) {
    display.setCursor(10, 34 + i * 13);
    display.print((startIdx + i == selected) ? "> " : "  ");
    display.println(items[startIdx + i]);
  }
  if (startIdx > 0) { display.setCursor(226, 34); display.print("^"); }
  if (startIdx + visibleRows < count) { display.setCursor(226, 112); display.print("v"); }
  display.display();
}

void displayProgress(const char* title, int current, int total, const char* status) {
  display.clearDisplay();
  drawBorder();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(8, 8);
  display.println(title);
  display.drawLine(0, 24, SCREEN_WIDTH, 24, SSD1306_WHITE);

  int barWidth = (total > 0) ? (int)((long)220 * current / total) : 0;
  display.drawRect(10, 42, 220, 12, SSD1306_WHITE);
  if (barWidth > 0) display.fillRect(10, 42, barWidth, 12, SSD1306_WHITE);

  String pct = String(total > 0 ? (int)(100L * current / total) : 0)
               + "% (" + String(current) + "/" + String(total) + ")";
  display.setCursor(10, 64);
  display.println(pct);
  display.setCursor(10, 82);
  display.println(status);
  display.display();
}

void displayTitleScreen() {
  display.clearDisplay();
  M5Cardputer.Display.setTextDatum(middle_center);
  M5Cardputer.Display.setTextColor(TFT_CYAN);
  M5Cardputer.Display.setTextSize(2);
  M5Cardputer.Display.drawRect(8, 8, SCREEN_WIDTH - 16, SCREEN_HEIGHT - 16, TFT_DARKCYAN);
  M5Cardputer.Display.drawRect(12, 12, SCREEN_WIDTH - 24, SCREEN_HEIGHT - 24, TFT_BLUE);
  M5Cardputer.Display.drawFastHLine(24, 34, 192, TFT_CYAN);
  M5Cardputer.Display.drawFastHLine(24, 98, 192, TFT_CYAN);
  M5Cardputer.Display.drawString("CYPHER PN532", SCREEN_WIDTH / 2, 62);
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextColor(TFT_WHITE);
  M5Cardputer.Display.drawString("Cardputer ADV NFC", SCREEN_WIDTH / 2, 84);
  M5Cardputer.Display.setTextDatum(top_left);
  display.display();
}

String pn532FirmwareLine(uint32_t ver) {
  if (!ver) return "FW: unavailable";
  return "FW: " + String((ver >> 16) & 0xFF) + "." + String((ver >> 8) & 0xFF);
}

String pn532ChipLine(uint32_t ver) {
  if (!ver) return "Chip: not found";
  return "Chip: PN5" + String((ver >> 24) & 0xFF, HEX);
}

bool initPn532Attempt(bool showStatus) {
  if (showStatus) displayInfo("PN532 NFC", "Recovering bus...");
  nfc.recoverBus();
  delay(80);

  uint32_t ver = nfc.getFirmwareVersion();
  if (!ver) {
    pn532Ready = false;
    pn532FirmwareVersion = 0;
    return false;
  }

  pn532FirmwareVersion = ver;
  pn532Ready = nfc.SAMConfig();
  if (pn532Ready) {
    nfc.setPassiveActivationRetries(0x05);
  }
  return pn532Ready;
}

bool initPn532WithAttempts(uint8_t attempts, bool showProgress) {
  for (uint8_t i = 0; i < attempts; i++) {
    if (showProgress) {
      displayInfo("PN532 NFC", "Initializing...",
                  "Attempt " + String(i + 1) + "/" + String(attempts));
    }
    if (initPn532Attempt(false)) return true;
    delay(180);
  }
  pn532Ready = false;
  return false;
}

bool ensurePn532ReadyOrPrompt(const char* action) {
  if (pn532Ready && nfc.probe()) return true;
  pn532Ready = false;

  if (initPn532WithAttempts(2, true)) {
    displayInfo("PN532 Ready", pn532ChipLine(pn532FirmwareVersion),
                pn532FirmwareLine(pn532FirmwareVersion));
    delay(700);
    return true;
  }

  displayInfo("PN532 Missing", action ? action : "Reader unavailable",
              "Select:Retry", "Back:Menu");
  while (true) {
    int btn = getButtonInput();
    if (btn == BUTTON_BACK) return false;
    if (btn == BUTTON_SELECT) {
      if (initPn532WithAttempts(PN532_BOOT_ATTEMPTS, true)) {
        displayInfo("PN532 Ready", pn532ChipLine(pn532FirmwareVersion),
                    pn532FirmwareLine(pn532FirmwareVersion));
        delay(900);
        return true;
      }
      displayInfo("PN532 Missing", "Power-cycle module",
                  "Select:Retry", "Back:Menu");
    }
    delay(25);
  }
}

void redisplayCurrentMenu() {
  switch (appState) {
    case STATE_MAIN_MENU:
      displayMenuScreen(pn532Ready ? "CYPHER NFC" : "CYPHER NFC !PN532",
                        mainMenuItems, mainMenuCount, currentMenuItem);
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
    case STATE_APDU_SUBMENU:
      displayMenuScreen("APDU Lab", apduMenuItems, apduMenuCount, currentSubMenuItem);
      break;
  }
}

// ============================================================
// BUTTON HANDLING
// ============================================================

int getButtonInput() {
  static uint32_t lastInputMs = 0;
  M5Cardputer.update();
  if (millis() - lastInputMs < 120) return BUTTON_NONE;

  ButtonInput input = BUTTON_NONE;
  if (M5Cardputer.BtnA.wasClicked()) input = BUTTON_SELECT;

  if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
    Keyboard_Class::KeysState keys = M5Cardputer.Keyboard.keysState();
    if (keys.enter || keys.space) input = BUTTON_SELECT;
    if (keys.del || keys.tab) input = BUTTON_BACK;

    for (auto c : keys.word) {
      if (c == ',' || c == ';' || c == 'w' || c == 'W' || c == 'k' || c == 'K') input = BUTTON_UP;
      else if (c == '.' || c == '/' || c == 's' || c == 'S' || c == 'j' || c == 'J') input = BUTTON_DOWN;
      else if (c == ' ' || c == '\n' || c == '\r') input = BUTTON_SELECT;
      else if (c == '`' || c == 'q' || c == 'Q') input = BUTTON_BACK;
    }

    for (const auto& key : M5Cardputer.Keyboard.keyList()) {
      if (key.y == 2 && key.x == 11) input = BUTTON_UP;
      if (key.y == 3 && key.x == 11) input = BUTTON_DOWN;
    }
  }

  if (input != BUTTON_NONE) lastInputMs = millis();
  return input;
}

// Wait for a card, return false if SELECT pressed to cancel
bool waitForCard(uint8_t* uid, uint8_t* uidLength,
                 const char* prompt1 = "Place card",
                 const char* prompt2 = "near reader") {
  if (!ensurePn532ReadyOrPrompt("NFC reader needed")) return false;
  displayInfo("Waiting...", prompt1, prompt2, "Select/Back:Cancel");
  while (true) {
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, uidLength)) {
      pn532Ready = true;
      return true;
    }
    if (nfc.hadTransportFault()) {
      pn532Ready = false;
      displayInfo("PN532 Read Error", "Recovering bus...",
                  "Keep tag still");
      if (!initPn532WithAttempts(2, false)) {
        displayInfo("PN532 Missing", "Select:Retry", "Back:Menu");
        while (true) {
          int retryBtn = getButtonInput();
          if (retryBtn == BUTTON_BACK) return false;
          if (retryBtn == BUTTON_SELECT) {
            if (initPn532WithAttempts(PN532_BOOT_ATTEMPTS, true)) {
              displayInfo("Waiting...", prompt1, prompt2, "Select/Back:Cancel");
              break;
            }
            displayInfo("PN532 Missing", "Power-cycle module",
                        "Select:Retry", "Back:Menu");
          }
          delay(25);
        }
      } else {
        displayInfo("Waiting...", prompt1, prompt2, "Select/Back:Cancel");
      }
    }
    int btn = getButtonInput();
    if (btn == BUTTON_SELECT || btn == BUTTON_BACK) {
      return false;
    }
    delay(20);
  }
}

// ============================================================
// SD CARD
// ============================================================

String appPath(const String& filename) {
  String clean = filename;
  if (clean.startsWith("/")) clean.remove(0, 1);
  return String(APP_DIR) + "/" + clean;
}

String basenameFromPath(const String& path) {
  int slash = path.lastIndexOf('/');
  return slash >= 0 ? path.substring(slash + 1) : path;
}

bool fileMatchesExtension(const String& name, const char* extension) {
  if (strlen(extension) == 0) return true;
  String lowerName = name;
  String lowerExt = String(extension);
  lowerName.toLowerCase();
  lowerExt.toLowerCase();
  return lowerName.endsWith(lowerExt);
}

String makeOpenPath(const String& directory, const String& entryName) {
  if (entryName.startsWith("/")) return entryName;
  String dir = directory;
  if (!dir.startsWith("/")) dir = "/" + dir;
  if (!dir.endsWith("/")) dir += "/";
  return dir + entryName;
}

void addFileListEntry(const String& displayName, const String& openPath) {
  if (fileCount >= 20) return;
  fileList[fileCount] = displayName;
  filePathList[fileCount] = openPath;
  fileCount++;
}

bool collectFilesFromDirectory(const String& directory, const char* extension) {
  File root = SD.open(directory.c_str());
  if (!root) return false;
  if (!root.isDirectory()) {
    root.close();
    return false;
  }
  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;
    if (!entry.isDirectory()) {
      String name = String(entry.name());
      if (fileMatchesExtension(name, extension)) {
        addFileListEntry(basenameFromPath(name), makeOpenPath(directory, name));
      }
    }
    entry.close();
  }
  root.close();
  return true;
}

bool chooseFileFromList(const char* title, const char* emptyMessage) {
  currentFileIndex = 0;
  if (fileCount == 0) {
    displayInfo("No Files", emptyMessage);
    delay(2000);
    return false;
  }

  while (true) {
    M5Cardputer.update();
    display.clearDisplay();
    drawBorder();
    display.setCursor(4, 4);
    display.println(title);
    display.drawLine(0, 14, SCREEN_WIDTH, 14, SSD1306_WHITE);

    int startIdx = max(0, currentFileIndex - 1);
    for (int i = 0; i < 3 && (startIdx + i) < fileCount; i++) {
      display.setCursor(4, 22 + i * 22);
      display.print((startIdx + i == currentFileIndex) ? "> " : "  ");
      String fname = fileList[startIdx + i];
      if (fname.length() > 22) fname = fname.substring(0, 19) + "...";
      display.println(fname);
    }
    display.setCursor(4, 118);
    display.println("Up/Down Select Back");
    display.display();

    int btn = getButtonInput();
    if (btn == BUTTON_UP)
      currentFileIndex = (currentFileIndex > 0) ? currentFileIndex - 1 : fileCount - 1;
    if (btn == BUTTON_DOWN)
      currentFileIndex = (currentFileIndex < fileCount - 1) ? currentFileIndex + 1 : 0;
    if (btn == BUTTON_SELECT) return true;
    if (btn == BUTTON_BACK) return false;
    delay(30);
  }
}

void ensureAppDir() {
  if (!SD.exists(APP_DIR)) {
    SD.mkdir(APP_DIR);
  }
}

void initSDCard() {
  displayInfo("SD Card", "Initializing...");
  pinMode(SD_POWER_HOLD, OUTPUT);
  digitalWrite(SD_POWER_HOLD, HIGH);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  delay(50);
  SD.end();
  SPI.end();
  delay(50);
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  delay(100);

  if (!SD.begin(SD_CS, SPI, 25000000)) {
    displayInfo("SD Error", "Init failed!", "Check SD card");
    sdReady = false;
    delay(2000);
    return;
  }

  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    displayInfo("SD Error", "No card found");
    sdReady = false;
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
  ensureAppDir();
  String sizeStr = String((uint32_t)(SD.cardSize() / (1024 * 1024))) + " MB";
  displayInfo("SD Ready", typeStr, sizeStr, APP_DIR);
  sdReady = true;
  delay(1500);
}

uint16_t readFileCounter() {
  String counter = appPath("COUNTER.TXT");
  if (!SD.exists(counter)) return 0;
  File f = SD.open(counter.c_str(), FILE_READ);
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
  ensureAppDir();
  String counter = appPath("COUNTER.TXT");
  SD.remove(counter);
  File f = SD.open(counter.c_str(), FILE_WRITE);
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

  ensureAppDir();
  File root = SD.open(APP_DIR);
  if (!root) {
    displayInfo("SD Error", "Can't open", APP_DIR);
    delay(2000);
    return false;
  }
  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;
    if (!entry.isDirectory()) {
      String name = basenameFromPath(String(entry.name()));
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
    int btn = getButtonInput();
    if (btn == BUTTON_SELECT) {
      bool ok = SD.remove(appPath(fileList[currentFileIndex]).c_str());
      displayInfo(ok ? "Deleted!" : "Failed!", fileList[currentFileIndex]);
      delay(2000);
      return;
    }
    if (btn == BUTTON_UP || btn == BUTTON_BACK) {
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

  File f = SD.open(appPath(shortName).c_str(), FILE_READ);
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
  if (!sdReady) return;
  ensureAppDir();
  String logPath = appPath("SCANLOG.CSV");
  bool needsHeader = !SD.exists(logPath);
  File f = SD.open(logPath.c_str(), FILE_APPEND);
  if (!f) return;
  if (needsHeader) f.println("uptime_ms,uid,type,action,filename");
  f.print(millis()); f.print(",");
  f.print(csvEscape(uid)); f.print(",");
  f.print(csvEscape(type)); f.print(",");
  f.print(csvEscape(action)); f.print(",");
  f.println(csvEscape(filename.length() ? filename : "-"));
  f.close();
}

String hexBytes(const uint8_t* data, int len, int maxBytes = 12) {
  String out = "";
  int n = min(len, maxBytes);
  for (int i = 0; i < n; i++) {
    if (i) out += " ";
    if (data[i] < 0x10) out += "0";
    out += String(data[i], HEX);
  }
  out.toUpperCase();
  if (len > maxBytes) out += "...";
  return out;
}

String apduStatusString(const uint8_t* resp, uint8_t respLen) {
  if (respLen < 2) return "SW: ----";
  String sw = "SW: ";
  if (resp[respLen - 2] < 0x10) sw += "0";
  sw += String(resp[respLen - 2], HEX);
  if (resp[respLen - 1] < 0x10) sw += "0";
  sw += String(resp[respLen - 1], HEX);
  sw.toUpperCase();
  return sw;
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
// READ-ONLY SD WEB SERVER
// ============================================================

String htmlEscape(const String& in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '&') out += F("&amp;");
    else if (c == '<') out += F("&lt;");
    else if (c == '>') out += F("&gt;");
    else if (c == '"') out += F("&quot;");
    else out += c;
  }
  return out;
}

String jsonEscape(const String& in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (c == '\n') {
      out += F("\\n");
    } else if (c == '\r') {
      out += F("\\r");
    } else if ((uint8_t)c >= 0x20) {
      out += c;
    }
  }
  return out;
}

int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

String percentDecode(const String& value) {
  String out = "";
  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];
    if (c == '%' && i + 2 < value.length()) {
      int hi = hexNibble(value[i + 1]);
      int lo = hexNibble(value[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out += (char)((hi << 4) | lo);
        i += 2;
      } else {
        out += c;
      }
    } else if (c == '+') {
      out += ' ';
    } else {
      out += c;
    }
  }
  return out;
}

String urlEncodeName(const String& in) {
  const char* hex = "0123456789ABCDEF";
  String out;
  for (size_t i = 0; i < in.length(); i++) {
    uint8_t c = (uint8_t)in[i];
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') {
      out += (char)c;
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

bool isSafeWebFileName(const String& name) {
  if (name.length() == 0 || name.length() > 64) return false;
  if (name.indexOf('/') >= 0 || name.indexOf('\\') >= 0) return false;
  if (name.indexOf("..") >= 0) return false;
  return true;
}

String fileKind(const String& name) {
  String upper = name;
  upper.toUpperCase();
  if (upper == "COUNTER.TXT") return "Counter";
  if (upper == "SCANLOG.CSV") return "Scan Log";
  if (upper.startsWith("KEY") && upper.endsWith(".TXT")) return "Key Map";
  if (upper.endsWith(".MFD")) return "MIFARE Dump";
  if (upper.endsWith(".BIN")) return "Binary Dump";
  if (upper.endsWith(".JSON")) return "Metadata";
  if (upper.endsWith(".TXT")) return "Text";
  return "File";
}

String contentTypeForFile(const String& name) {
  String upper = name;
  upper.toUpperCase();
  if (upper.endsWith(".TXT")) return "text/plain";
  if (upper.endsWith(".CSV")) return "text/csv";
  if (upper.endsWith(".JSON")) return "application/json";
  if (upper.endsWith(".HTML") || upper.endsWith(".HTM")) return "text/html";
  return "application/octet-stream";
}

int countAppFiles() {
  if (!sdReady) return 0;
  ensureAppDir();
  File root = SD.open(APP_DIR);
  if (!root) return 0;
  int count = 0;
  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;
    if (!entry.isDirectory()) count++;
    entry.close();
  }
  root.close();
  return count;
}

String statusJson() {
  String json = "{";
  json += "\"ok\":true,\"op\":\"STATUS\"";
  json += ",\"pn532\":\"";
  json += pn532Ready ? "ready" : "not_found";
  json += "\",\"sd\":\"";
  json += sdReady ? "ready" : "error";
  json += "\",\"device\":\"Cardputer PN532";
  json += "\",\"app_dir\":\"";
  json += APP_DIR;
  json += "\",\"files\":";
  json += String(countAppFiles());
  json += ",\"clients\":";
  json += String(WiFi.softAPgetStationNum());
  json += ",\"mode\":\"field_workstation\"";
  json += ",\"operation_busy\":";
  json += operationBusy ? "true" : "false";
  json += ",\"ble\":\"";
  json += bleSerialActive ? (bleClientConnected ? "connected" : "advertising") : "off";
  json += "\"";
  json += ",\"uptime_ms\":";
  json += String(millis());
  if (sdReady) {
    json += ",\"sd_total_mb\":";
    json += String((uint32_t)(SD.cardSize() / (1024 * 1024)));
    json += ",\"sd_used_mb\":";
    json += String((uint32_t)(SD.usedBytes() / (1024 * 1024)));
  }
  json += ",\"last_operation\":";
  json += lastOperationJson;
  json += "}";
  return json;
}

void sendWebHeader(const String& title) {
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.send(200, "text/html", "");
  webServer.sendContent(F("<!doctype html><html><head><meta charset='utf-8'>"));
  webServer.sendContent(F("<meta name='viewport' content='width=device-width,initial-scale=1'>"));
  webServer.sendContent("<title>" + htmlEscape(title) + "</title>");
  webServer.sendContent(F("<style>body{margin:0;background:#071011;color:#e9fffb;font-family:-apple-system,BlinkMacSystemFont,Segoe UI,sans-serif}"));
  webServer.sendContent(F("header{padding:20px 16px;background:#092023;border-bottom:1px solid #1d5a60}main{max-width:980px;margin:0 auto;padding:16px}"));
  webServer.sendContent(F("h1{margin:0;font-size:25px}.sub{color:#9ee2dd;margin-top:5px}.hero{border:1px solid #1d5a60;background:#0a191b;padding:14px;margin:14px 0;border-radius:8px}"));
  webServer.sendContent(F(".status{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:8px;margin:14px 0}.pill{border:1px solid #1d5a60;background:#0d2023;padding:10px;border-radius:6px}.pill b{display:block;color:#77fff1;font-size:12px;text-transform:uppercase}"));
  webServer.sendContent(F("table{width:100%;border-collapse:collapse;background:#0b181a;border:1px solid #1d5a60}th,td{padding:10px;border-bottom:1px solid #12393d;text-align:left}"));
  webServer.sendContent(F("a{color:#79fff0;text-decoration:none}.badge{font-size:12px;color:#001b1b;background:#78ffee;border-radius:4px;padding:3px 6px}pre{white-space:pre-wrap;word-break:break-word;background:#061416;border:1px solid #1d5a60;padding:12px;overflow:auto}"));
  webServer.sendContent(F(".actions a{margin-right:10px}.muted{color:#88c8c5}.warn{color:#ffd27a}.loglink{float:right}button{background:#78ffee;color:#001b1b;border:0;border-radius:5px;padding:7px 9px;font-weight:700}input,select,textarea{width:100%;box-sizing:border-box;background:#061416;color:#e9fffb;border:1px solid #1d5a60;border-radius:5px;padding:7px;margin:4px 0 8px}</style></head><body>"));
  webServer.sendContent("<header><h1>" + htmlEscape(title) + "</h1><div class='sub'>No-auth NFC field workstation controls for /cypher-pn532</div></header><main>");
}

void sendWebFooter() {
  webServer.sendContent(F("</main><script>async function op(f){event.preventDefault();let r=await fetch('/api/op',{method:'POST',body:new FormData(f)});let t=await r.text();let e=document.querySelector('#result');if(e)e.textContent=t;await s();return false;}async function s(){try{let r=await fetch('/api/status');let t=await r.text();let p=document.querySelector('#status-json');if(p)p.textContent=t;let j=JSON.parse(t);for(let k in j){let e=document.querySelector('[data-s='+k+']');if(e)e.textContent=j[k];}}catch(e){}}setInterval(s,3000);s();</script></body></html>"));
  webServer.sendContent("");
}

void handleStatusApi() {
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(200, "application/json", statusJson());
}

void handleFilesApi() {
  if (!sdReady) {
    webServer.send(503, "application/json", "{\"error\":\"sd_not_ready\"}");
    return;
  }
  ensureAppDir();
  File root = SD.open(APP_DIR);
  if (!root) {
    webServer.send(500, "application/json", "{\"error\":\"open_failed\"}");
    return;
  }
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.send(200, "application/json", "");
  webServer.sendContent("[");
  bool first = true;
  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;
    if (!entry.isDirectory()) {
      String name = basenameFromPath(String(entry.name()));
      String encoded = urlEncodeName(name);
      if (!first) webServer.sendContent(",");
      first = false;
      String item = "{\"name\":\"" + jsonEscape(name) + "\",\"size\":" + String(entry.size()) +
                    ",\"type\":\"" + jsonEscape(fileKind(name)) +
                    "\",\"view_url\":\"/view?name=" + jsonEscape(encoded) +
                    "\",\"download_url\":\"/download?name=" + jsonEscape(encoded) + "\"}";
      webServer.sendContent(item);
    }
    entry.close();
  }
  root.close();
  webServer.sendContent("]");
}

void handleOperationApi() {
  OperationArgs args = argsFromWeb();
  String op = webServer.arg("op");
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(200, "application/json", runOperation(op, args, "web"));
}

void handleRootPage() {
  sendWebHeader("Cypher PN532 Control");
  webServer.sendContent(F("<section class='hero'><b>Field workstation</b><span class='muted loglink'>no auth</span><br><span class='muted'>Run full NFC workflows from this device-launched AP. Use only with authorized lab tags.</span></section>"));
  webServer.sendContent(F("<section class='status'>"));
  webServer.sendContent(F("<div class='pill'><b>PN532</b><span data-s='pn532'>"));
  webServer.sendContent(pn532Ready ? "ready" : "not_found");
  webServer.sendContent(F("</span></div><div class='pill'><b>SD</b><span data-s='sd'>"));
  webServer.sendContent(sdReady ? "ready" : "error");
  webServer.sendContent(F("</span></div><div class='pill'><b>Files</b><span data-s='files'>"));
  webServer.sendContent(String(countAppFiles()));
  webServer.sendContent(F("</span></div><div class='pill'><b>Clients</b><span data-s='clients'>"));
  webServer.sendContent(String(WiFi.softAPgetStationNum()));
  webServer.sendContent(F("</span></div></section>"));
  webServer.sendContent(F("<section class='hero'><b>Status JSON</b><pre id='status-json'></pre><b>Last Result</b><pre id='result'></pre></section>"));
  webServer.sendContent(F("<section class='status'>"));
  webServer.sendContent(F("<form class='pill' onsubmit='return op(this)'><input type='hidden' name='op' value='SCAN'><b>Scan</b><button>Scan</button></form>"));
  webServer.sendContent(F("<form class='pill' onsubmit='return op(this)'><input type='hidden' name='op' value='DUMP'><b>Dump</b><button>Dump to SD</button></form>"));
  webServer.sendContent(F("<form class='pill' onsubmit='return op(this)'><input type='hidden' name='op' value='KEY_AUDIT'><b>Key Audit</b><button>Audit</button></form>"));
  webServer.sendContent(F("<form class='pill' onsubmit='return op(this)'><input type='hidden' name='op' value='WRITE_NDEF'><b>Write NDEF</b><select name='type'><option value='url'>URL</option><option value='text'>Text</option><option value='vcard'>vCard</option></select><textarea name='content' rows='2' placeholder='URL or text'></textarea><input name='name' placeholder='vCard name'><input name='tel' placeholder='vCard phone'><input name='email' placeholder='vCard email'><button>Write</button></form>"));
  webServer.sendContent(F("<form class='pill' onsubmit='return op(this)'><input type='hidden' name='op' value='WRITE_FROM_SD'><b>Write From SD</b><input name='name' placeholder='preset.txt'><button>Write</button></form>"));
  webServer.sendContent(F("<form class='pill' onsubmit='return op(this)'><input type='hidden' name='op' value='CLONE'><b>Clone</b><input name='name' placeholder='optional dmp001.mfd'><button>Clone</button></form>"));
  webServer.sendContent(F("<form class='pill' onsubmit='return op(this)'><input type='hidden' name='op' value='VERIFY'><b>Verify</b><button>Verify</button></form>"));
  webServer.sendContent(F("<form class='pill' onsubmit='return op(this)'><input type='hidden' name='op' value='EMULATE_NDEF'><b>Emulate</b><button>Emulate NDEF</button></form>"));
  webServer.sendContent(F("<form class='pill' onsubmit='return op(this)'><input type='hidden' name='op' value='PUT_PRESET'><b>Save Preset</b><input name='name' placeholder='NDEF_URL.TXT'><textarea name='content' rows='2'></textarea><button>Save</button></form>"));
  webServer.sendContent(F("<form class='pill' onsubmit='return op(this)'><input type='hidden' name='op' value='DELETE'><b>Delete</b><input name='name' placeholder='file.ext'><button>Delete</button></form>"));
  webServer.sendContent(F("</section>"));
  if (SD.exists(appPath("SCANLOG.CSV"))) {
    webServer.sendContent(F("<p><a href='/view?name=SCANLOG.CSV'>View scan log</a> <span class='muted'>CSV logger: uptime, UID, type, action, filename</span></p>"));
  }

  if (!sdReady) {
    webServer.sendContent(F("<p class='warn'>SD card is not ready. Reinsert the card and restart the app.</p>"));
    sendWebFooter();
    return;
  }

  ensureAppDir();
  File root = SD.open(APP_DIR);
  if (!root) {
    webServer.sendContent(F("<p class='warn'>Could not open /cypher-pn532.</p>"));
    sendWebFooter();
    return;
  }

  webServer.sendContent(F("<table><thead><tr><th>Name</th><th>Type</th><th>Size</th><th>Actions</th></tr></thead><tbody>"));
  bool any = false;
  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;
    if (!entry.isDirectory()) {
      any = true;
      String name = basenameFromPath(String(entry.name()));
      String encoded = urlEncodeName(name);
      webServer.sendContent("<tr><td>" + htmlEscape(name) + "</td><td><span class='badge'>" +
                            htmlEscape(fileKind(name)) + "</span></td><td>" + String(entry.size()) +
                            "</td><td class='actions'><a href='/view?name=" + encoded +
                            "'>View</a><a href='/download?name=" + encoded + "'>Download</a></td></tr>");
    }
    entry.close();
  }
  root.close();
  if (!any) webServer.sendContent(F("<tr><td colspan='4' class='muted'>No files yet. Run a dump or key map first.</td></tr>"));
  webServer.sendContent(F("</tbody></table>"));
  sendWebFooter();
}

void handleViewFile() {
  String name = webServer.arg("name");
  if (!isSafeWebFileName(name)) {
    webServer.send(400, "text/plain", "Bad file name");
    return;
  }
  String path = appPath(name);
  if (!SD.exists(path)) {
    webServer.send(404, "text/plain", "Not found");
    return;
  }
  File f = SD.open(path.c_str(), FILE_READ);
  if (!f) {
    webServer.send(500, "text/plain", "Open failed");
    return;
  }

  sendWebHeader(name);
  webServer.sendContent("<p><a href='/'>Back to files</a> <span class='muted'>Read-only preview</span></p><pre>");
  String upper = name;
  upper.toUpperCase();
  if (upper.endsWith(".TXT") || upper.endsWith(".CSV")) {
    char buf[97];
    while (f.available()) {
      size_t n = f.readBytes(buf, sizeof(buf) - 1);
      buf[n] = 0;
      webServer.sendContent(htmlEscape(String(buf)));
      delay(1);
    }
  } else {
    uint8_t buf[16];
    uint32_t offset = 0;
    while (f.available() && offset < 2048) {
      size_t n = f.read(buf, sizeof(buf));
      char line[96];
      snprintf(line, sizeof(line), "%04lX: ", (unsigned long)offset);
      String out = line;
      for (size_t i = 0; i < n; i++) {
        if (buf[i] < 0x10) out += "0";
        out += String(buf[i], HEX);
        out += " ";
      }
      out += " | ";
      for (size_t i = 0; i < n; i++) {
        char c = (char)buf[i];
        out += (c >= 0x20 && c < 0x7F) ? c : '.';
      }
      out += "\n";
      out.toUpperCase();
      webServer.sendContent(out);
      offset += n;
      delay(1);
    }
    if (f.available()) webServer.sendContent("\nPreview limited to first 2048 bytes. Use Download for the full file.\n");
  }
  f.close();
  webServer.sendContent(F("</pre>"));
  sendWebFooter();
}

void handleDownloadFile() {
  String name = webServer.arg("name");
  if (!isSafeWebFileName(name)) {
    webServer.send(400, "text/plain", "Bad file name");
    return;
  }
  String path = appPath(name);
  if (!SD.exists(path)) {
    webServer.send(404, "text/plain", "Not found");
    return;
  }
  File f = SD.open(path.c_str(), FILE_READ);
  if (!f) {
    webServer.send(500, "text/plain", "Open failed");
    return;
  }
  webServer.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
  webServer.streamFile(f, contentTypeForFile(name));
  f.close();
}

void drawWebServerScreen() {
  display.clearDisplay();
  drawBorder();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(8, 8);
  display.println("PN532 Control");
  display.drawLine(0, 24, SCREEN_WIDTH, 24, SSD1306_WHITE);
  display.setCursor(8, 34);
  display.print("SSID: "); display.println(WEB_AP_SSID);
  display.setCursor(8, 48);
  display.print("Pass: "); display.println(WEB_AP_PASS);
  display.setCursor(8, 62);
  display.println("URL: http://192.168.4.1");
  display.setCursor(8, 80);
  display.print("PN532: "); display.print(pn532Ready ? "ready" : "missing");
  display.print(" SD: "); display.println(sdReady ? "ok" : "err");
  display.setCursor(8, 94);
  display.print("Clients: "); display.print(WiFi.softAPgetStationNum());
  display.print(" Files: "); display.println(countAppFiles());
  display.setCursor(8, 114);
  display.println("Back/Q exits");
  display.display();
}

void startWebControlServer() {
  if (!sdReady) {
    displayInfo("SD Not Ready", "Cannot start web", "Check card");
    delay(2500);
    redisplayCurrentMenu();
    return;
  }

  displayInfo("Starting AP", WEB_AP_SSID, "Please wait...");
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(WEB_AP_IP, WEB_AP_GATEWAY, WEB_AP_SUBNET);
  if (!WiFi.softAP(WEB_AP_SSID, WEB_AP_PASS)) {
    displayInfo("WiFi Error", "AP start failed");
    delay(2500);
    redisplayCurrentMenu();
    return;
  }

  webServer.on("/", HTTP_GET, handleRootPage);
  webServer.on("/api/status", HTTP_GET, handleStatusApi);
  webServer.on("/api/files", HTTP_GET, handleFilesApi);
  webServer.on("/api/op", HTTP_POST, handleOperationApi);
  webServer.on("/view", HTTP_GET, handleViewFile);
  webServer.on("/download", HTTP_GET, handleDownloadFile);
  webServer.onNotFound([]() {
    webServer.send(404, "text/plain", "Not found");
  });
  webServer.begin();

  uint32_t lastDraw = 0;
  while (true) {
    webServer.handleClient();
    processSerialCommands();
    int btn = getButtonInput();
    if (btn == BUTTON_BACK || btn == BUTTON_SELECT) break;
    if (millis() - lastDraw > 1000) {
      drawWebServerScreen();
      lastDraw = millis();
    }
    delay(5);
  }

  webServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  displayInfo("Web Control", "Stopped");
  delay(1000);
  redisplayCurrentMenu();
}

// ============================================================
// CARD TYPE DETECTION
// ============================================================

bool apduExchange(const uint8_t* cmd, uint8_t cmdLen, uint8_t* resp, uint8_t* respLen) {
  uint8_t local[32];
  if (cmdLen > sizeof(local)) return false;
  memcpy(local, cmd, cmdLen);
  return nfc.inDataExchange(local, cmdLen, resp, respLen);
}

bool apduStatusOk(const uint8_t* resp, uint8_t respLen) {
  return respLen >= 2 && resp[respLen - 2] == 0x90 && resp[respLen - 1] == 0x00;
}

bool probeType4NdefTag() {
  static const uint8_t selectNdefAid[] = {
    0x00, 0xA4, 0x04, 0x00, 0x07,
    0xD2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x01,
    0x00
  };
  uint8_t resp[32];
  uint8_t respLen = sizeof(resp);
  return apduExchange(selectNdefAid, sizeof(selectNdefAid), resp, &respLen) &&
         apduStatusOk(resp, respLen);
}

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
      if (nfc.hadTransportFault()) {
        pn532Ready = false;
        info->type = CARD_NFC_UNKNOWN;
        strncpy(info->typeName, "PN532 read error", sizeof(info->typeName));
      } else if (probeType4NdefTag()) {
        info->type = CARD_ISO14443_4;
        strncpy(info->typeName, "ISO14443-4 T4T", sizeof(info->typeName));
      } else {
        info->type = CARD_MIFARE_CLASSIC_1K;
        info->totalBlocks = 64;
        strncpy(info->typeName, "MIFARE Classic ?K", sizeof(info->typeName));
      }
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
    } else if (nfc.hadTransportFault()) {
      pn532Ready = false;
      info->type = CARD_NFC_UNKNOWN; info->totalPages = 0;
      strncpy(info->typeName, "PN532 read error", sizeof(info->typeName));
    } else if (probeType4NdefTag()) {
      info->type = CARD_ISO14443_4; info->totalPages = 0;
      strncpy(info->typeName, "ISO14443-4 T4T", sizeof(info->typeName));
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

bool parseKeyHexLine(String line, uint8_t* out) {
  line.trim();
  if (line.length() == 0 || line.startsWith("#")) return false;
  line.replace(":", "");
  line.replace(" ", "");
  line.replace("-", "");
  if (line.length() < 12) return false;
  for (int i = 0; i < 6; i++) {
    int hi = hexNibble(line[i * 2]);
    int lo = hexNibble(line[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = (uint8_t)((hi << 4) | lo);
  }
  return true;
}

void loadSdKeys() {
  sdKeyCount = 0;
  sdKeysLoaded = true;
  if (!sdReady) return;
  String path = appPath("KEYS.TXT");
  if (!SD.exists(path)) path = "/KEYS.TXT";
  if (!SD.exists(path)) return;
  File f = SD.open(path.c_str(), FILE_READ);
  if (!f) return;
  String line = "";
  while (f.available() && sdKeyCount < 64) {
    char c = (char)f.read();
    if (c == '\n' || c == '\r') {
      uint8_t key[6];
      if (parseKeyHexLine(line, key)) memcpy(sdKeys[sdKeyCount++], key, 6);
      line = "";
    } else if (line.length() < 64) {
      line += c;
    }
  }
  uint8_t key[6];
  if (line.length() && sdKeyCount < 64 && parseKeyHexLine(line, key)) {
    memcpy(sdKeys[sdKeyCount++], key, 6);
  }
  f.close();
}

int totalKeyCandidates() {
  if (!sdKeysLoaded) loadSdKeys();
  return NUM_DEFAULT_KEYS + sdKeyCount;
}

const uint8_t* keyCandidateAt(int index) {
  if (index < NUM_DEFAULT_KEYS) return defaultKeys[index];
  int sdIndex = index - NUM_DEFAULT_KEYS;
  if (sdIndex >= 0 && sdIndex < sdKeyCount) return sdKeys[sdIndex];
  return defaultKeys[0];
}

int countCrackedSectors(int numSectors) {
  int count = 0;
  for (int s = 0; s < numSectors; s++) {
    if (keyMap.keyAKnown[s] || keyMap.keyBKnown[s]) count++;
  }
  return count;
}

String keySummaryJson(int numSectors) {
  String json = "{";
  json += "\"sectors\":" + String(numSectors);
  json += ",\"cracked_sectors\":" + String(countCrackedSectors(numSectors));
  json += ",\"built_in_keys\":" + String(NUM_DEFAULT_KEYS);
  json += ",\"sd_keys\":" + String(sdKeyCount);
  json += "}";
  return json;
}

void writeMetadataJson(const String& jsonName, const String& uid, const String& cardType,
                       const String& operation, int capacity, const String& primaryFile,
                       const String& companionFile, const String& ndefJson = "{}") {
  if (!sdReady) return;
  ensureAppDir();
  File f = SD.open(appPath(jsonName).c_str(), FILE_WRITE);
  if (!f) return;
  f.println("{");
  f.println("  \"schema_version\": 1,");
  f.println("  \"device\": \"CYPHER NFC Cardputer\",");
  f.println("  \"firmware\": \"cypher-pn532-field-workstation\",");
  f.print("  \"created_ms\": "); f.print(millis()); f.println(",");
  f.print("  \"uid\": \""); f.print(jsonEscape(uid)); f.println("\",");
  f.print("  \"card_type\": \""); f.print(jsonEscape(cardType)); f.println("\",");
  f.print("  \"capacity\": "); f.print(capacity); f.println(",");
  f.print("  \"operation\": \""); f.print(jsonEscape(operation)); f.println("\",");
  f.print("  \"files\": {\"primary\":\""); f.print(jsonEscape(primaryFile));
  f.print("\",\"companion\":\""); f.print(jsonEscape(companionFile)); f.println("\"},");
  f.print("  \"key_summary\": "); f.print(keySummaryJson(keyMap.numSectors));
  f.println(",");
  f.print("  \"ndef\": "); f.println(ndefJson);
  f.println("}");
  f.close();
  lastSavedJson = jsonName;
}

// ============================================================
// MIFARE CLASSIC DUMP
// ============================================================

void dumpMifareClassic(uint8_t* uid, uint8_t uidLength, bool is4K) {
  int numBlocks  = is4K ? 256 : 64;
  int numSectors = totalSectors(is4K);
  loadSdKeys();

  mifDump.totalBlocks = numBlocks;
  mifDump.uidLength   = uidLength;
  memcpy(mifDump.uid, uid, uidLength);
  memset(mifDump.blockRead, false, sizeof(mifDump.blockRead));

  // Populate keyMap while discovering keys
  keyMap.numSectors    = numSectors;
  keyMap.crackedCount  = 0;
  memset(keyMap.keyAKnown, false, sizeof(keyMap.keyAKnown));
  memset(keyMap.keyBKnown, false, sizeof(keyMap.keyBKnown));

  // Phase 1 — find working Key A/B for each sector
  for (int s = 0; s < numSectors; s++) {
    int trailer = sectorTrailerBlock(s, is4K);
    displayProgress("Finding Keys", s, numSectors,
                    ("Sector " + String(s)).c_str());

    for (int k = 0; k < totalKeyCandidates(); k++) {
      const uint8_t* key = keyCandidateAt(k);
      if (nfc.mifareclassic_AuthenticateBlock(uid, uidLength, trailer, 0,
          (uint8_t*)key)) {
        memcpy(keyMap.keyA[s], key, 6);
        keyMap.keyAKnown[s] = true;
      }
      if (nfc.mifareclassic_AuthenticateBlock(uid, uidLength, trailer, 1,
          (uint8_t*)key)) {
        memcpy(keyMap.keyB[s], key, 6);
        keyMap.keyBKnown[s] = true;
      }
      if (keyMap.keyAKnown[s] && keyMap.keyBKnown[s]) {
        break;
      }
      if (k % 5 == 0) delay(1); // feed watchdog
    }
  }
  keyMap.crackedCount = countCrackedSectors(numSectors);

  // Phase 2 — read every block using its sector's cached Key A
  for (int b = 0; b < numBlocks; b++) {
    int s = blockToSector(b, is4K);
    displayProgress("Reading", b, numBlocks, ("Block " + String(b)).c_str());

    if (keyMap.keyAKnown[s] || keyMap.keyBKnown[s]) {
      if (keyMap.keyAKnown[s]) {
        nfc.mifareclassic_AuthenticateBlock(uid, uidLength, b, 0, keyMap.keyA[s]);
      } else {
        nfc.mifareclassic_AuthenticateBlock(uid, uidLength, b, 1, keyMap.keyB[s]);
      }
      if (nfc.mifareclassic_ReadDataBlock(b, mifDump.data[b])) {
        mifDump.blockRead[b] = true;
        memcpy(mifDump.keyUsed[b], keyMap.keyAKnown[s] ? keyMap.keyA[s] : keyMap.keyB[s], 6);
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
  ensureAppDir();
  lastSavedFilename = "";
  lastSavedCompanion = "";
  lastSavedJson = "";
  String mfdName = generateUniqueFilename("dmp", "mfd");
  File mfd = SD.open(appPath(mfdName).c_str(), FILE_WRITE);
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
  File txt = SD.open(appPath(txtName).c_str(), FILE_WRITE);
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
  String jsonName = mfdName.substring(0, mfdName.lastIndexOf('.')) + ".json";
  writeMetadataJson(jsonName, uidToString(mifDump.uid, mifDump.uidLength),
                    is4K ? "MIFARE Classic 4K" : "MIFARE Classic 1K",
                    "dump_mifare", mifDump.totalBlocks * BLOCK_SIZE,
                    mfdName, txtName);
  displayInfo("Saved!", mfdName, txtName);
  delay(2500);
  return true;
}

bool loadMifareDumpFromSD() {
  if (!browseFiles(".mfd")) return false;

  String filename = appPath(fileList[currentFileIndex]);
  File f = SD.open(filename.c_str(), FILE_READ);
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

  while (true) {
    int btn = getButtonInput();
    if (btn == BUTTON_SELECT || btn == BUTTON_BACK) break;
    delay(20);
  }
}

void saveKeyMapToSD(int numSectors) {
  ensureAppDir();
  String fname = generateUniqueFilename("key", "txt");
  File f = SD.open(appPath(fname).c_str(), FILE_WRITE);
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
  String jsonName = fname.substring(0, fname.lastIndexOf('.')) + ".json";
  writeMetadataJson(jsonName, "-", "MIFARE Classic", "key_audit",
                    numSectors, fname, "", "{}");
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
  loadSdKeys();
  keyMap.numSectors   = numSectors;
  keyMap.crackedCount = 0;
  memset(keyMap.keyAKnown, false, sizeof(keyMap.keyAKnown));
  memset(keyMap.keyBKnown, false, sizeof(keyMap.keyBKnown));

  for (int s = 0; s < numSectors; s++) {
    int trailer = sectorTrailerBlock(s, is4K);

    // Try Key A
    displayProgress("Dict Attack", s * 2, numSectors * 2,
                    ("S" + String(s) + " Key A").c_str());
    for (int k = 0; k < totalKeyCandidates() && !keyMap.keyAKnown[s]; k++) {
      const uint8_t* key = keyCandidateAt(k);
      if (nfc.mifareclassic_AuthenticateBlock(uid, uidLength, trailer, 0,
          (uint8_t*)key)) {
        memcpy(keyMap.keyA[s], key, 6);
        keyMap.keyAKnown[s] = true;
      }
      if (k % 3 == 0) delay(1);
    }

    // Try Key B
    displayProgress("Dict Attack", s * 2 + 1, numSectors * 2,
                    ("S" + String(s) + " Key B").c_str());
    for (int k = 0; k < totalKeyCandidates() && !keyMap.keyBKnown[s]; k++) {
      const uint8_t* key = keyCandidateAt(k);
      if (nfc.mifareclassic_AuthenticateBlock(uid, uidLength, trailer, 1,
          (uint8_t*)key)) {
        memcpy(keyMap.keyB[s], key, 6);
        keyMap.keyBKnown[s] = true;
      }
      if (k % 3 == 0) delay(1);
    }
  }
  keyMap.crackedCount = countCrackedSectors(numSectors);

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
  if (!nfc.inDataExchange(cmd, 2, resp, &respLen)) {
    if (nfc.hadTransportFault()) {
      pn532Ready = false;
      if (initPn532WithAttempts(1, false)) {
        respLen = sizeof(resp);
        if (!nfc.inDataExchange(cmd, 2, resp, &respLen)) return false;
      } else {
        return false;
      }
    } else {
      return false;
    }
  }
  if (respLen < 4) return false;
  memcpy(buf, resp, 4); // first 4 bytes = requested page data
  return true;
}

bool readNDEFPages(CardInfo* info, int* maxPage, int* readCount, int* failCount,
                   bool* cancelled) {
  *maxPage = min((int)info->totalPages, 42);
  *readCount = 0;
  *failCount = 0;
  if (cancelled) *cancelled = false;
  memset(ntagDump.pageRead, false, sizeof(ntagDump.pageRead));
  memset(ntagDump.pages, 0, sizeof(ntagDump.pages));

  uint8_t consecutiveFails = 0;
  for (int p = 0; p < *maxPage; p++) {
    displayProgress("Read NDEF", p + 1, *maxPage, ("Page " + String(p)).c_str());
    int btn = getButtonInput();
    if (btn == BUTTON_BACK || btn == BUTTON_SELECT) {
      if (cancelled) *cancelled = true;
      return false;
    }
    bool ok = readNTAGPageRaw(p, ntagDump.pages[p]);
    ntagDump.pageRead[p] = ok;
    if (ok) {
      (*readCount)++;
      consecutiveFails = 0;
    } else {
      (*failCount)++;
      consecutiveFails++;
      if (consecutiveFails >= PN532_READ_FAIL_LIMIT) {
        pn532Ready = false;
        break;
      }
    }
    M5Cardputer.update();
    delay(15);
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
    display.println("U/D:Scroll S/Back:Exit");
    display.display();

    int btn = getButtonInput();
    if (btn == BUTTON_DOWN && viewPage + 4 < maxPage) viewPage += 4;
    if (btn == BUTTON_UP   && viewPage > 0)           viewPage -= 4;
    if (btn == BUTTON_SELECT || btn == BUTTON_BACK) break;
    delay(50);
  }
}

void showDecodedNDEFOrRaw(CardInfo* info, int maxPage, NDEFDecoded* decoded) {
  if (decoded->ok) {
    displayInfo(decoded->label,
                decoded->value.substring(0, 24),
                decoded->value.substring(24, 48),
                "Select:Raw Back:Menu");
    while (true) {
      int btn = getButtonInput();
      if (btn == BUTTON_BACK) return;
      if (btn == BUTTON_SELECT) break;
      delay(30);
    }
  }
  showRawNDEFPages(String(info->typeName) + " NDEF", maxPage);
}

void dumpNTAG(uint8_t* uid, uint8_t uidLength, CardInfo* info) {
  memcpy(ntagDump.uid, uid, uidLength);
  ntagDump.uidLength  = uidLength;
  ntagDump.totalPages = info->totalPages;
  strncpy(ntagDump.typeName, info->typeName, sizeof(ntagDump.typeName));
  memset(ntagDump.pageRead, false, sizeof(ntagDump.pageRead));

  uint8_t consecutiveFails = 0;
  for (int p = 0; p < info->totalPages; p++) {
    displayProgress("NTAG Dump", p, info->totalPages, ("Page " + String(p)).c_str());
    int btn = getButtonInput();
    if (btn == BUTTON_BACK || btn == BUTTON_SELECT) break;
    bool ok = readNTAGPageRaw(p, ntagDump.pages[p]);
    ntagDump.pageRead[p] = ok;
    if (ok) {
      consecutiveFails = 0;
    } else if (++consecutiveFails >= PN532_READ_FAIL_LIMIT) {
      pn532Ready = false;
      break;
    }
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
  ensureAppDir();
  lastSavedFilename = "";
  lastSavedCompanion = "";
  lastSavedJson = "";
  String binName = generateUniqueFilename("ntg", "bin");
  File f = SD.open(appPath(binName).c_str(), FILE_WRITE);
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
  File t = SD.open(appPath(txtName).c_str(), FILE_WRITE);
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
  String jsonName = binName.substring(0, binName.lastIndexOf('.')) + ".json";
  writeMetadataJson(jsonName, uidToString(ntagDump.uid, ntagDump.uidLength),
                    String(ntagDump.typeName), "dump_ntag",
                    ntagDump.totalPages * 4, binName, txtName);
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
  bool cancelled = false;
  bool readOk = readNDEFPages(&info, &maxPage, &readCount, &failCount, &cancelled);
  ntagDump.uidLength  = uidLength;
  memcpy(ntagDump.uid, uid, uidLength);

  if (!readOk) {
    displayInfo(cancelled ? "NDEF Cancelled" : "Read failed",
                String(readCount) + " pages read",
                String(failCount) + " failed",
                pn532Ready ? "Retry or Back" : "Retry / power-cycle");
    delay(1500);
    redisplayCurrentMenu();
    return;
  }

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
  String name = String(filename);
  String local = appPath(name);
  String root = name.startsWith("/") ? name : "/" + name;
  String selected = SD.exists(local) ? local : root;
  if (!SD.exists(selected)) return defaultVal;
  File f = SD.open(selected.c_str(), FILE_READ);
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
  String url = loadNDEFPreset("NDEF_URL.TXT",
                              "github.com/dkyazzentwatwa/cypher-pn532");
  NDEFPreset preset = parseNDEFPreset(String("url:") + url);
  String visible = ndefUriPrefix(preset.prefix) + preset.payload;
  displayInfo("NDEF URL", visible.substring(0, 24), "Place NTAG card");
  delay(2000);

  uint8_t uid[7]; uint8_t uidLength;
  if (!waitForCard(uid, &uidLength, "Place NTAG", "card now")) {
    redisplayCurrentMenu(); return;
  }

  displayInfo("Writing URL...");
  bool ok = buildAndWriteNDEF(uid, uidLength, 'U', preset.prefix, preset.payload.c_str());
  appendScanLog(uidToString(uid, uidLength), "NTAG", ok ? "write_url" : "write_url_failed", "NDEF_URL.TXT");
  displayInfo(ok ? "URL Written!" : "Write Failed", visible.substring(0, 24),
              ok ? "Scan with phone!" : "");
  delay(2500);
  redisplayCurrentMenu();
}

void writeNDEFText() {
  String text = loadNDEFPreset("NDEF_TXT.TXT", "CYPHER PN532 2026");
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

  File f = SD.open(appPath(fileList[currentFileIndex]).c_str(), FILE_READ);
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
              visible.substring(0, 24), "Place NTAG card");
  delay(2000);

  uint8_t uid[7]; uint8_t uidLength;
  if (!waitForCard(uid, &uidLength, "Place NTAG", "card now")) {
    redisplayCurrentMenu(); return;
  }

  bool ok = buildAndWriteNDEF(uid, uidLength, preset.recordType, preset.prefix, preset.payload.c_str());
  appendScanLog(uidToString(uid, uidLength), "NTAG",
                ok ? (preset.recordType == 'U' ? "write_sd_url" : "write_sd_text") : "write_sd_failed",
                fileList[currentFileIndex]);
  displayInfo(ok ? "Written!" : "Failed", visible.substring(0, 24));
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

  while (true) {
    int btn = getButtonInput();
    if (btn == BUTTON_SELECT || btn == BUTTON_BACK) break;
    delay(20);
  }
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
// APDU LAB (safe read-only ISO7816 / Type 4 NDEF probes)
// ============================================================

bool apduSelectNdefAid(uint8_t* resp, uint8_t* respLen) {
  static const uint8_t selectNdefAid[] = {
    0x00, 0xA4, 0x04, 0x00, 0x07,
    0xD2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x01,
    0x00
  };
  return apduExchange(selectNdefAid, sizeof(selectNdefAid), resp, respLen);
}

bool apduSelectFile(uint16_t fileId, uint8_t* resp, uint8_t* respLen) {
  uint8_t cmd[] = {
    0x00, 0xA4, 0x00, 0x0C, 0x02,
    (uint8_t)(fileId >> 8), (uint8_t)(fileId & 0xFF)
  };
  return apduExchange(cmd, sizeof(cmd), resp, respLen);
}

bool apduReadBinary(uint16_t offset, uint8_t len, uint8_t* resp, uint8_t* respLen) {
  uint8_t cmd[] = {
    0x00, 0xB0,
    (uint8_t)(offset >> 8), (uint8_t)(offset & 0xFF),
    len
  };
  return apduExchange(cmd, sizeof(cmd), resp, respLen);
}

void showApduResult(const String& title, const String& line1,
                    const uint8_t* resp, uint8_t respLen, uint16_t holdMs = 2600) {
  String data = (respLen > 2) ? hexBytes(resp, respLen - 2, 12) : "No data";
  displayInfo(title, line1, apduStatusString(resp, respLen), data);
  delay(holdMs);
}

void apduSelectNdefAidFlow() {
  uint8_t uid[7]; uint8_t uidLength = 0;
  if (!waitForCard(uid, &uidLength, "Place Type4", "ISO7816 tag")) {
    redisplayCurrentMenu(); return;
  }

  uint8_t resp[64];
  uint8_t respLen = sizeof(resp);
  bool ok = apduSelectNdefAid(resp, &respLen);
  String uidStr = uidToString(uid, uidLength);
  appendScanLog(uidStr, "ISO7816", ok && apduStatusOk(resp, respLen) ? "apdu_select_ndef" : "apdu_select_ndef_failed", apduStatusString(resp, respLen));
  if (!ok) {
    displayInfo("APDU Failed", uidStr.substring(0, 24), "No response");
    delay(2500);
  } else {
    showApduResult("Select NDEF AID", uidStr.substring(0, 24), resp, respLen);
  }
  redisplayCurrentMenu();
}

void apduType4NdefProbeFlow() {
  uint8_t uid[7]; uint8_t uidLength = 0;
  if (!waitForCard(uid, &uidLength, "Place Type4", "NDEF tag")) {
    redisplayCurrentMenu(); return;
  }

  String uidStr = uidToString(uid, uidLength);
  uint8_t resp[80];
  uint8_t respLen = sizeof(resp);
  if (!apduSelectNdefAid(resp, &respLen) || !apduStatusOk(resp, respLen)) {
    appendScanLog(uidStr, "ISO7816", "apdu_type4_probe_failed", "select_aid");
    showApduResult("Type4 Probe", "SELECT AID failed", resp, respLen);
    redisplayCurrentMenu();
    return;
  }
  showApduResult("Type4 Probe", "NDEF app selected", resp, respLen, 1200);

  respLen = sizeof(resp);
  if (!apduSelectFile(0xE103, resp, &respLen) || !apduStatusOk(resp, respLen)) {
    appendScanLog(uidStr, "ISO7816", "apdu_type4_probe_failed", "select_cc");
    showApduResult("Type4 Probe", "CC select failed", resp, respLen);
    redisplayCurrentMenu();
    return;
  }

  respLen = sizeof(resp);
  if (!apduReadBinary(0, 15, resp, &respLen) || !apduStatusOk(resp, respLen) || respLen < 17) {
    appendScanLog(uidStr, "ISO7816", "apdu_type4_probe_failed", "read_cc");
    showApduResult("Type4 Probe", "CC read failed", resp, respLen);
    redisplayCurrentMenu();
    return;
  }

  uint16_t ndefFileId = ((uint16_t)resp[9] << 8) | resp[10];
  uint16_t maxNdef = ((uint16_t)resp[11] << 8) | resp[12];
  displayInfo("Type4 CC",
              "File: " + String(ndefFileId, HEX),
              "Max: " + String(maxNdef) + " bytes",
              "Reading NLEN...");
  delay(1600);

  respLen = sizeof(resp);
  if (!apduSelectFile(ndefFileId, resp, &respLen) || !apduStatusOk(resp, respLen)) {
    appendScanLog(uidStr, "ISO7816", "apdu_type4_probe_failed", "select_ndef_file");
    showApduResult("Type4 Probe", "NDEF file failed", resp, respLen);
    redisplayCurrentMenu();
    return;
  }

  respLen = sizeof(resp);
  if (!apduReadBinary(0, 2, resp, &respLen) || !apduStatusOk(resp, respLen) || respLen < 4) {
    appendScanLog(uidStr, "ISO7816", "apdu_type4_probe_failed", "read_nlen");
    showApduResult("Type4 Probe", "NLEN read failed", resp, respLen);
    redisplayCurrentMenu();
    return;
  }

  uint16_t nlen = ((uint16_t)resp[0] << 8) | resp[1];
  uint8_t readLen = (uint8_t)min((uint16_t)32, nlen);
  if (readLen > 0) {
    respLen = sizeof(resp);
    if (apduReadBinary(2, readLen, resp, &respLen) && apduStatusOk(resp, respLen)) {
      showApduResult("Type4 NDEF",
                     "NLEN: " + String(nlen),
                     resp, respLen, 3200);
    } else {
      showApduResult("Type4 NDEF", "Payload read failed", resp, respLen);
    }
  } else {
    displayInfo("Type4 NDEF", "NLEN: 0", "Empty message");
    delay(2500);
  }

  appendScanLog(uidStr, "ISO7816", "apdu_type4_probe", "nlen=" + String(nlen));
  redisplayCurrentMenu();
}

// ============================================================
// DEMO MODE
// ============================================================

void waitForDemoExit() {
  while (true) {
    int btn = getButtonInput();
    if (btn == BUTTON_SELECT || btn == BUTTON_BACK) break;
    delay(30);
  }
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
    bool cancelled = false;
    if (!readNDEFPages(&info, &maxPage, &readCount, &failCount, &cancelled)) {
      if (!cancelled) {
        displayInfo("Read failed", String(readCount) + " pages read",
                    String(failCount) + " failed",
                    pn532Ready ? "Retry or Back" : "Retry / power-cycle");
        delay(1500);
      }
      redisplayCurrentMenu();
      return;
    }
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
      displayInfo("Puzzle Hunt", clue.substring(0, 24), clue.substring(24, 48), "Select/Back:Exit");
      waitForDemoExit();
    } else if (decoded.ok) {
      displayInfo("Tag Studio", decoded.label,
                  decoded.value.substring(0, 24),
                  decoded.value.substring(24, 48));
      delay(3500);
    } else {
      displayInfo("Tag Studio", info.typeName, "No decoded NDEF", uidStr.substring(0, 24));
      delay(3000);
    }
  } else {
    appendScanLog(uidStr, info.typeName, action);
    displayInfo(puzzleMode ? "Puzzle Hunt" : "Tag Studio",
                info.typeName,
                "UID: " + uidStr.substring(0, 20),
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

  displayInfo("Dump Saved", lastSavedFilename, "Select:Start Web", "Back:Menu");
  while (true) {
    int btn = getButtonInput();
    if (btn == BUTTON_SELECT) { startWebControlServer(); return; }
    if (btn == BUTTON_BACK) break;
    delay(30);
  }
  redisplayCurrentMenu();
}

// ============================================================
// CARD EMULATION (ISO14443A target mode)
//
// Drives the PN532 as a tag via the M5Pn532 tgInitAsTarget/tgGetData/tgSetData
// methods. NDEF from SD uses Type 4 NDEF APDUs for phone-friendly reads; NTAG
// Dump and UID Only remain Type 2 lab modes.
// LIMITATION: MIFARE Classic emulation is NOT possible (chip can't run Crypto1
// auth as a target); the RF UID is only partly controllable.
// ============================================================

#define EMU_MAX_PAGES 64
static uint8_t emuPageImage[EMU_MAX_PAGES * 4];
static int     emuPageCount = 0;
static const int EMU_TYPE4_MAX_NDEF_FILE = 240;
static const int EMU_TYPE4_MAX_RESPONSE_DATA = 59;
static uint8_t emuType4NdefFile[EMU_TYPE4_MAX_NDEF_FILE];
static int emuType4NdefFileLength = 0;

static void emuPrepareHeader(const uint8_t* uid, uint8_t uidLen, uint8_t* uid3) {
  memset(emuPageImage, 0, sizeof(emuPageImage));
  uid3[0] = uidLen > 0 ? uid[0] : 0x04;
  uid3[1] = uidLen > 1 ? uid[1] : 0x12;
  uid3[2] = uidLen > 2 ? uid[2] : 0x34;
  emuPageImage[0] = uid3[0];
  emuPageImage[1] = uid3[1];
  emuPageImage[2] = uid3[2];
  emuPageImage[3] = uid3[0] ^ uid3[1] ^ uid3[2] ^ 0x88;
  emuPageImage[12] = 0xE1;  // Capability Container: NTAG213, 144B, read/write
  emuPageImage[13] = 0x10;
  emuPageImage[14] = 0x12;
  emuPageImage[15] = 0x00;
}

// Serve the prebuilt emuPageImage as a Type 2 tag until Select/Back is pressed.
void runType2Emulation(const String& sourceLabel, const uint8_t* uid3) {
  displayInfo("Emulating Tag", sourceLabel, "Tap phone now", "Select/Back: exit");
  appendScanLog(uidToString(uid3, 3), "NTAG", "emulate", sourceLabel);

  int bytes = emuPageCount * 4;
  if (bytes <= 0) bytes = 16;
  bool exit = false;
  while (!exit) {
    M5Cardputer.update();
    int b = getButtonInput();
    if (b == BUTTON_SELECT || b == BUTTON_BACK) break;

    if (!nfc.tgInitAsTarget(uid3, 1200)) { delay(10); continue; }

    while (!exit) {
      M5Cardputer.update();
      b = getButtonInput();
      if (b == BUTTON_SELECT || b == BUTTON_BACK) { exit = true; break; }
      uint8_t in[64];
      int n = nfc.tgGetData(in, sizeof(in), 600);
      if (n < 1) break;                          // field lost -> re-init
      if (in[0] == 0x30 && n >= 2) {             // Type 2 READ <page>
        uint8_t page = in[1];
        uint8_t out[16];
        for (int i = 0; i < 16; i++)
          out[i] = emuPageImage[((page * 4) + i) % bytes];
        nfc.tgSetData(out, 16, 400);
      } else if (in[0] == 0x50) {                // HALT
        break;
      } else {
        nfc.tgSetData(NULL, 0, 400);             // NAK / ignore others
      }
    }
  }
  displayInfo("Emulation", "Stopped");
  delay(1000);
  redisplayCurrentMenu();
}

int buildBareNDEFRecord(char recordType, uint8_t prefix,
                        const char* payload, uint8_t* msg, int maxLen) {
  int payloadLen = (recordType == 'U')
      ? 1 + min((int)strlen(payload), 180)
      : 3 + min((int)strlen(payload), 178);
  if (payloadLen > 255 || maxLen < payloadLen + 4) return -1;

  int idx = 0;
  msg[idx++] = 0xD1;
  msg[idx++] = 0x01;
  msg[idx++] = (uint8_t)payloadLen;
  msg[idx++] = (uint8_t)recordType;

  if (recordType == 'U') {
    msg[idx++] = prefix;
    int urlLen = payloadLen - 1;
    memcpy(&msg[idx], payload, urlLen);
    idx += urlLen;
  } else {
    msg[idx++] = 0x02;
    msg[idx++] = 'e';
    msg[idx++] = 'n';
    int textLen = payloadLen - 3;
    memcpy(&msg[idx], payload, textLen);
    idx += textLen;
  }
  return idx;
}

bool buildType4NdefFile(const NDEFPreset& preset) {
  memset(emuType4NdefFile, 0, sizeof(emuType4NdefFile));
  int recordBytes = buildBareNDEFRecord(preset.recordType, preset.prefix,
                                        preset.payload.c_str(),
                                        &emuType4NdefFile[2],
                                        EMU_TYPE4_MAX_NDEF_FILE - 2);
  if (recordBytes <= 0) return false;
  emuType4NdefFile[0] = (recordBytes >> 8) & 0xFF;
  emuType4NdefFile[1] = recordBytes & 0xFF;
  emuType4NdefFileLength = recordBytes + 2;
  return true;
}

bool loadEmulatedNdefPreset(NDEFPreset* preset, String* sourceLabel, String* visible) {
  String content = "";
  sourceLabel->remove(0);

  if (sdReady) {
    fileCount = 0;
    currentFileIndex = 0;
    collectFilesFromDirectory(appPath("ndef"), ".txt");
    if (fileCount > 0) {
      displayInfo("NDEF Emulate", "Select payload", "/ndef/*.txt");
      delay(900);
      if (!chooseFileFromList("NDEF Emulate", "/ndef/*.txt")) return false;

      File f = SD.open(filePathList[currentFileIndex].c_str(), FILE_READ);
      if (!f) {
        displayInfo("Error", "Cannot open", fileList[currentFileIndex]);
        delay(2000);
        return false;
      }
      while (f.available() && content.length() < 180) content += (char)f.read();
      f.close();
      content.trim();
      *sourceLabel = fileList[currentFileIndex];
    }
  }

  if (content.length() == 0) {
    String url = loadNDEFPreset("NDEF_URL.TXT", "");
    String text = loadNDEFPreset("NDEF_TXT.TXT", "");
    if (url.length() > 0) {
      String lower = url;
      lower.toLowerCase();
      content = lower.startsWith("url:") ? url : String("url:") + url;
      *sourceLabel = "NDEF_URL.TXT";
    } else if (text.length() > 0) {
      String lower = text;
      lower.toLowerCase();
      content = lower.startsWith("text:") ? text : String("text:") + text;
      *sourceLabel = "NDEF_TXT.TXT";
    } else {
      content = "url:https://github.com/dkyazzentwatwa/cypher-pn532";
      *sourceLabel = "Built-in URL";
    }
  }

  *preset = parseNDEFPreset(content);
  if (preset->payload.length() == 0) return false;
  *visible = (preset->recordType == 'U')
      ? ndefUriPrefix(preset->prefix) + preset->payload
      : preset->payload;
  return true;
}

int type4Status(uint8_t sw1, uint8_t sw2, uint8_t* out) {
  out[0] = sw1;
  out[1] = sw2;
  return 2;
}

bool type4BytesEqual(const uint8_t* a, const uint8_t* b, int len) {
  for (int i = 0; i < len; i++) {
    if (a[i] != b[i]) return false;
  }
  return true;
}

int handleType4Apdu(const uint8_t* in, int inLen, Type4SelectedFile* selectedFile, uint8_t* out) {
  static const uint8_t appAid[] = { 0xD2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x01 };
  static const uint8_t ccFile[] = {
    0x00, 0x0F, 0x20, 0x00, 0x3B, 0x00, 0x34, 0x04,
    0x06, 0xE1, 0x04, 0x00, 0xF0, 0x00, 0xFF
  };

  if (inLen >= 12 && in[1] == 0xA4 && in[2] == 0x04 && in[4] == sizeof(appAid) &&
      type4BytesEqual(&in[5], appAid, sizeof(appAid))) {
    *selectedFile = TYPE4_FILE_NONE;
    Serial.println("Type4 APDU: SELECT NDEF application");
    return type4Status(0x90, 0x00, out);
  }

  if (inLen >= 7 && in[1] == 0xA4 && in[2] == 0x00 && in[4] == 0x02) {
    uint16_t fileId = ((uint16_t)in[5] << 8) | in[6];
    if (fileId == 0xE103) {
      *selectedFile = TYPE4_FILE_CC;
      Serial.println("Type4 APDU: SELECT CC file");
      return type4Status(0x90, 0x00, out);
    }
    if (fileId == 0xE104) {
      *selectedFile = TYPE4_FILE_NDEF;
      Serial.println("Type4 APDU: SELECT NDEF file");
      return type4Status(0x90, 0x00, out);
    }
    return type4Status(0x6A, 0x82, out);
  }

  if (inLen >= 5 && in[1] == 0xB0) {
    const uint8_t* fileData = nullptr;
    int fileLen = 0;
    if (*selectedFile == TYPE4_FILE_CC) {
      fileData = ccFile;
      fileLen = sizeof(ccFile);
    } else if (*selectedFile == TYPE4_FILE_NDEF) {
      fileData = emuType4NdefFile;
      fileLen = emuType4NdefFileLength;
    } else {
      return type4Status(0x69, 0x86, out);
    }

    int offset = ((int)in[2] << 8) | in[3];
    int le = in[4] == 0 ? 256 : in[4];
    if (offset > fileLen) return type4Status(0x6B, 0x00, out);

    int available = fileLen - offset;
    int take = min(le, available);
    take = min(take, EMU_TYPE4_MAX_RESPONSE_DATA);
    if (take > 0) memcpy(out, &fileData[offset], take);
    out[take++] = 0x90;
    out[take++] = 0x00;
    Serial.printf("Type4 APDU: READ file=%d offset=%d len=%d\n", (int)*selectedFile, offset, take - 2);
    return take;
  }

  if (inLen >= 2 && in[1] == 0xD6) {
    return type4Status(0x69, 0x86, out);
  }

  Serial.printf("Type4 APDU: unsupported INS=0x%02X len=%d\n", inLen >= 2 ? in[1] : 0x00, inLen);
  return type4Status(0x6D, 0x00, out);
}

void runType4NdefEmulation(const String& sourceLabel, const String& visible, const uint8_t* uid3) {
  displayInfo("Emulating URL", sourceLabel.substring(0, 24),
              visible.substring(0, 24), "Select/Back: exit");
  appendScanLog(uidToString(uid3, 3), "Type4", "emulate_ndef", sourceLabel);
  Serial.printf("Type4 emulation ready source=%s ndef_file_bytes=%d visible=%s\n",
                sourceLabel.c_str(), emuType4NdefFileLength, visible.c_str());

  bool exit = false;
  while (!exit) {
    M5Cardputer.update();
    int b = getButtonInput();
    if (b == BUTTON_SELECT || b == BUTTON_BACK) break;

    if (!nfc.tgInitAsType4Target(uid3, 1200)) { delay(10); continue; }

    Type4SelectedFile selectedFile = TYPE4_FILE_NONE;
    Serial.println("Type4 target activated");
    while (!exit) {
      M5Cardputer.update();
      b = getButtonInput();
      if (b == BUTTON_SELECT || b == BUTTON_BACK) { exit = true; break; }

      uint8_t in[96];
      int inLen = nfc.tgGetData(in, sizeof(in), 800);
      if (inLen < 1) break;

      uint8_t out[EMU_TYPE4_MAX_RESPONSE_DATA + 2];
      int outLen = handleType4Apdu(in, inLen, &selectedFile, out);
      nfc.tgSetData(out, (uint8_t)outLen, 400);
    }
  }

  nfc.SAMConfig();
  displayInfo("Emulation", "Stopped");
  delay(1000);
  redisplayCurrentMenu();
}

void emulateNdefFromSD() {
  NDEFPreset preset;
  String sourceLabel;
  String visible;
  if (!loadEmulatedNdefPreset(&preset, &sourceLabel, &visible)) {
    displayInfo("Cancelled", "NDEF Emulation");
    delay(900);
    redisplayCurrentMenu();
    return;
  }

  if (!buildType4NdefFile(preset)) {
    displayInfo("Too Long", "NDEF payload", "Shorten content");
    delay(2500);
    redisplayCurrentMenu();
    return;
  }

  uint8_t uid3[3];
  uint8_t noUid[1] = {0};
  emuPrepareHeader(noUid, 0, uid3);
  runType4NdefEmulation(sourceLabel, visible, uid3);
}

void emulateNtagDumpFromSD() {
  if (!browseFiles(".bin")) { redisplayCurrentMenu(); return; }
  String shortName = fileList[currentFileIndex];
  File f = SD.open(appPath(shortName).c_str(), FILE_READ);
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
  runType2Emulation(shortName.substring(0, 16), uid3);
}

void emulateUidOnly() {
  uint8_t uid[7]; uint8_t uidLength;
  if (!waitForCard(uid, &uidLength, "Scan UID to", "emulate")) {
    redisplayCurrentMenu(); return;
  }
  uint8_t uid3[3];
  emuPrepareHeader(uid, uidLength, uid3);
  emuPageImage[16] = 0x03;  // empty NDEF TLV
  emuPageImage[17] = 0x00;
  emuPageImage[18] = 0xFE;
  emuPageCount = 6;
  runType2Emulation("UID: " + uidToString(uid, uidLength).substring(0, 13), uid3);
}

void executeEmulateMenuItem(int idx) {
  switch (idx) {
    case 0: emulateNdefFromSD();     break;
    case 1: emulateNtagDumpFromSD(); break;
    case 2: emulateUidOnly();        break;
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
// FIELD WORKSTATION OPERATIONS (USB SERIAL + WEB CONTROL)
// ============================================================

String opResultJson(bool ok, const String& op, const String& message,
                    const String& uid = "", const String& cardType = "",
                    const String& filename = "", const String& error = "") {
  String json = "{";
  json += "\"ok\":" + String(ok ? "true" : "false");
  json += ",\"op\":\"" + jsonEscape(op) + "\"";
  json += ",\"message\":\"" + jsonEscape(message) + "\"";
  if (uid.length()) json += ",\"uid\":\"" + jsonEscape(uid) + "\"";
  if (cardType.length()) json += ",\"card_type\":\"" + jsonEscape(cardType) + "\"";
  if (filename.length()) json += ",\"filename\":\"" + jsonEscape(filename) + "\"";
  if (lastSavedJson.length()) json += ",\"metadata\":\"" + jsonEscape(lastSavedJson) + "\"";
  if (error.length()) json += ",\"error\":\"" + jsonEscape(error) + "\"";
  json += ",\"job_id\":" + String(millis());
  json += "}";
  return json;
}

String argValue(const OperationArgs& args, const String& key, const String& fallback = "") {
  for (int i = 0; i < args.count; i++) {
    if (args.key[i].equalsIgnoreCase(key)) return args.value[i];
  }
  return fallback;
}

void addArg(OperationArgs* args, const String& key, const String& value) {
  if (args->count >= 12 || key.length() == 0) return;
  args->key[args->count] = key;
  args->value[args->count] = value;
  args->count++;
}

String parseSerialOperation(const String& line, OperationArgs* args) {
  args->count = 0;
  String work = line;
  work.trim();
  int firstSpace = work.indexOf(' ');
  String op = firstSpace < 0 ? work : work.substring(0, firstSpace);
  op.toUpperCase();
  int pos = firstSpace < 0 ? work.length() : firstSpace + 1;
  while (pos < (int)work.length()) {
    while (pos < (int)work.length() && work[pos] == ' ') pos++;
    int next = work.indexOf(' ', pos);
    if (next < 0) next = work.length();
    String token = work.substring(pos, next);
    int eq = token.indexOf('=');
    if (eq > 0) addArg(args, token.substring(0, eq), percentDecode(token.substring(eq + 1)));
    pos = next + 1;
  }
  return op;
}

OperationArgs argsFromWeb() {
  OperationArgs args;
  args.count = 0;
  for (int i = 0; i < webServer.args() && i < 12; i++) {
    addArg(&args, webServer.argName(i), webServer.arg(i));
  }
  return args;
}

String filesJsonArray() {
  if (!sdReady) return "[]";
  ensureAppDir();
  File root = SD.open(APP_DIR);
  if (!root) return "[]";
  String json = "[";
  bool first = true;
  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;
    if (!entry.isDirectory()) {
      String name = basenameFromPath(String(entry.name()));
      if (!first) json += ",";
      first = false;
      json += "{\"name\":\"" + jsonEscape(name) + "\",\"size\":" + String(entry.size()) +
              ",\"type\":\"" + jsonEscape(fileKind(name)) +
              "\",\"view_url\":\"/view?name=" + jsonEscape(urlEncodeName(name)) +
              "\",\"download_url\":\"/download?name=" + jsonEscape(urlEncodeName(name)) + "\"}";
    }
    entry.close();
  }
  root.close();
  json += "]";
  return json;
}

bool loadMifareDumpByName(const String& name) {
  if (!isSafeWebFileName(name)) return false;
  File f = SD.open(appPath(name).c_str(), FILE_READ);
  if (!f) return false;
  long fileSize = f.size();
  bool is4K = (fileSize >= 4096);
  mifDump.totalBlocks = is4K ? 256 : 64;
  mifDump.uidLength = 0;
  for (int b = 0; b < mifDump.totalBlocks; b++) {
    if (f.available() >= BLOCK_SIZE) {
      f.read(mifDump.data[b], BLOCK_SIZE);
      mifDump.blockRead[b] = true;
    } else {
      memset(mifDump.data[b], 0, BLOCK_SIZE);
      mifDump.blockRead[b] = false;
    }
    memset(mifDump.keyUsed[b], 0xFF, 6);
  }
  f.close();
  return true;
}

String makeVcardPayload(const OperationArgs& args) {
  String name = argValue(args, "name", "Cypher Contact");
  String tel = argValue(args, "tel");
  String email = argValue(args, "email");
  String card = "BEGIN:VCARD\nVERSION:3.0\nFN:" + name;
  if (tel.length()) card += "\nTEL:" + tel;
  if (email.length()) card += "\nEMAIL:" + email;
  card += "\nEND:VCARD";
  return card;
}

String opScanCard(const String& op) {
  uint8_t uid[7]; uint8_t uidLength = 0;
  if (!waitForCard(uid, &uidLength)) return opResultJson(false, op, "cancelled", "", "", "", "cancelled");
  CardInfo info;
  detectCardType(uid, uidLength, &info);
  String uidStr = uidToString(uid, uidLength);
  appendScanLog(uidStr, info.typeName, "op_scan");
  return opResultJson(true, op, "card scanned", uidStr, info.typeName);
}

String opDumpCard(const String& op) {
  uint8_t uid[7]; uint8_t uidLength = 0;
  if (!waitForCard(uid, &uidLength)) return opResultJson(false, op, "cancelled", "", "", "", "cancelled");
  CardInfo info;
  detectCardType(uid, uidLength, &info);
  String uidStr = uidToString(uid, uidLength);
  if (info.type == CARD_MIFARE_CLASSIC_1K || info.type == CARD_MIFARE_CLASSIC_4K) {
    bool is4K = (info.type == CARD_MIFARE_CLASSIC_4K);
    dumpMifareClassic(uid, uidLength, is4K);
    if (saveMifareDumpToSD(is4K)) {
      appendScanLog(uidStr, info.typeName, "op_dump", lastSavedFilename);
      return opResultJson(true, op, "mifare dump saved", uidStr, info.typeName, lastSavedFilename);
    }
    return opResultJson(false, op, "save failed", uidStr, info.typeName, "", "sd_save_failed");
  }
  if (info.totalPages > 0) {
    dumpNTAG(uid, uidLength, &info);
    saveNTAGDumpToSD();
    appendScanLog(uidStr, info.typeName, "op_dump", lastSavedFilename);
    return opResultJson(true, op, "ntag dump saved", uidStr, info.typeName, lastSavedFilename);
  }
  return opResultJson(false, op, "unsupported card", uidStr, info.typeName, "", "unsupported_card");
}

String opKeyAudit(const String& op) {
  uint8_t uid[7]; uint8_t uidLength = 0;
  if (!waitForCard(uid, &uidLength, "Place MIFARE", "card now")) return opResultJson(false, op, "cancelled", "", "", "", "cancelled");
  CardInfo info;
  detectCardType(uid, uidLength, &info);
  if (info.type != CARD_MIFARE_CLASSIC_1K && info.type != CARD_MIFARE_CLASSIC_4K) {
    return opResultJson(false, op, "not mifare classic", uidToString(uid, uidLength), info.typeName, "", "unsupported_card");
  }
  bool is4K = (info.type == CARD_MIFARE_CLASSIC_4K);
  int numSectors = totalSectors(is4K);
  loadSdKeys();
  keyMap.numSectors = numSectors;
  keyMap.crackedCount = 0;
  memset(keyMap.keyAKnown, false, sizeof(keyMap.keyAKnown));
  memset(keyMap.keyBKnown, false, sizeof(keyMap.keyBKnown));
  for (int s = 0; s < numSectors; s++) {
    int trailer = sectorTrailerBlock(s, is4K);
    displayProgress("Key Audit", s, numSectors, ("Sector " + String(s)).c_str());
    for (int k = 0; k < totalKeyCandidates(); k++) {
      const uint8_t* key = keyCandidateAt(k);
      if (!keyMap.keyAKnown[s] && nfc.mifareclassic_AuthenticateBlock(uid, uidLength, trailer, 0, (uint8_t*)key)) {
        memcpy(keyMap.keyA[s], key, 6);
        keyMap.keyAKnown[s] = true;
      }
      if (!keyMap.keyBKnown[s] && nfc.mifareclassic_AuthenticateBlock(uid, uidLength, trailer, 1, (uint8_t*)key)) {
        memcpy(keyMap.keyB[s], key, 6);
        keyMap.keyBKnown[s] = true;
      }
      if (keyMap.keyAKnown[s] && keyMap.keyBKnown[s]) break;
      if (k % 3 == 0) delay(1);
    }
  }
  keyMap.crackedCount = countCrackedSectors(numSectors);
  saveKeyMapToSD(numSectors);
  String uidStr = uidToString(uid, uidLength);
  appendScanLog(uidStr, info.typeName, "op_key_audit", lastSavedJson.length() ? lastSavedJson : "-");
  return opResultJson(true, op, "key audit saved", uidStr, info.typeName, lastSavedJson.length() ? lastSavedJson : "keymap");
}

String opWriteNdef(const String& op, const OperationArgs& args) {
  String type = argValue(args, "type", "text");
  String content = argValue(args, "content", argValue(args, "value", ""));
  type.toLowerCase();
  char recordType = 'T';
  uint8_t prefix = 0;
  if (type == "url" || content.startsWith("url:")) {
    NDEFPreset preset = parseNDEFPreset(content.startsWith("url:") ? content : String("url:") + content);
    recordType = 'U';
    prefix = preset.prefix;
    content = preset.payload;
  } else if (type == "vcard") {
    content = makeVcardPayload(args);
  }
  if (content.length() == 0) return opResultJson(false, op, "missing content", "", "", "", "missing_content");
  uint8_t msg[128];
  int ndefLen = buildNDEFMessage(recordType, prefix, content.c_str(), msg);
  if (ndefLen > 120) return opResultJson(false, op, "payload too long", "", "", "", "capacity");
  uint8_t uid[7]; uint8_t uidLength = 0;
  if (!waitForCard(uid, &uidLength, "Place NTAG", "card now")) return opResultJson(false, op, "cancelled", "", "", "", "cancelled");
  bool ok = buildAndWriteNDEF(uid, uidLength, recordType, prefix, content.c_str());
  String uidStr = uidToString(uid, uidLength);
  appendScanLog(uidStr, "NTAG", ok ? "op_write_ndef" : "op_write_ndef_failed", content.substring(0, 32));
  return opResultJson(ok, op, ok ? "ndef written" : "write failed", uidStr, "NTAG", "", ok ? "" : "write_failed");
}

String opWriteFromSd(const String& op, const OperationArgs& args) {
  String name = argValue(args, "name", argValue(args, "file", ""));
  if (!isSafeWebFileName(name)) return opResultJson(false, op, "bad file name", "", "", "", "bad_name");
  File f = SD.open(appPath(name).c_str(), FILE_READ);
  if (!f) return opResultJson(false, op, "file not found", "", "", name, "not_found");
  String content = "";
  while (f.available() && content.length() < 120) content += (char)f.read();
  f.close();
  content.trim();
  NDEFPreset preset = parseNDEFPreset(content);
  OperationArgs writeArgs;
  writeArgs.count = 0;
  addArg(&writeArgs, "type", preset.recordType == 'U' ? "url" : "text");
  addArg(&writeArgs, "content", preset.recordType == 'U' ? ndefUriPrefix(preset.prefix) + preset.payload : preset.payload);
  String result = opWriteNdef(op, writeArgs);
  appendScanLog("-", "NTAG", "op_write_from_sd", name);
  return result;
}

String opCloneMagic(const String& op, const OperationArgs& args) {
  String name = argValue(args, "name", argValue(args, "file", ""));
  if (name.length() && !loadMifareDumpByName(name)) return opResultJson(false, op, "cannot load dump", "", "", name, "load_failed");
  if (mifDump.totalBlocks == 0) return opResultJson(false, op, "no dump loaded", "", "", "", "no_dump");
  bool ok = writeDumpToMagicCard();
  appendScanLog("-", "MIFARE", ok ? "op_clone" : "op_clone_failed", name.length() ? name : lastSavedFilename);
  return opResultJson(ok, op, ok ? "clone complete" : "clone failed", "", "MIFARE Classic", name, ok ? "" : "clone_failed");
}

String opVerifyClone(const String& op) {
  if (mifDump.totalBlocks == 0) return opResultJson(false, op, "no dump loaded", "", "", "", "no_dump");
  uint8_t uid[7]; uint8_t uidLength = 0;
  if (!waitForCard(uid, &uidLength, "Place CLONE", "card now")) return opResultJson(false, op, "cancelled", "", "", "", "cancelled");
  uint8_t defKey[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
  int mismatch = 0, skipped = 0;
  for (int b = 0; b < mifDump.totalBlocks; b++) {
    if (!mifDump.blockRead[b]) { skipped++; continue; }
    displayProgress("Verifying", b, mifDump.totalBlocks, ("Blk " + String(b)).c_str());
    bool authed = nfc.mifareclassic_AuthenticateBlock(uid, uidLength, b, 0, defKey);
    if (!authed) authed = nfc.mifareclassic_AuthenticateBlock(uid, uidLength, b, 0, mifDump.keyUsed[b]);
    if (!authed) { skipped++; continue; }
    uint8_t readBack[16];
    if (!nfc.mifareclassic_ReadDataBlock(b, readBack)) { skipped++; continue; }
    if (memcmp(readBack, mifDump.data[b], 16) != 0) mismatch++;
    if (b % 4 == 0) delay(1);
  }
  String uidStr = uidToString(uid, uidLength);
  appendScanLog(uidStr, "MIFARE", mismatch == 0 ? "op_verify_ok" : "op_verify_fail", "mismatch=" + String(mismatch));
  return opResultJson(mismatch == 0, op, "mismatch=" + String(mismatch) + " skipped=" + String(skipped), uidStr, "MIFARE Classic");
}

String opPutPreset(const String& op, const OperationArgs& args) {
  String name = argValue(args, "name", argValue(args, "file", ""));
  String content = argValue(args, "content", argValue(args, "value", ""));
  if (!isSafeWebFileName(name) || !name.endsWith(".TXT")) return opResultJson(false, op, "bad preset name", "", "", name, "bad_name");
  ensureAppDir();
  File f = SD.open(appPath(name).c_str(), FILE_WRITE);
  if (!f) return opResultJson(false, op, "open failed", "", "", name, "open_failed");
  f.print(content);
  f.close();
  appendScanLog("-", "SD", "op_put_preset", name);
  return opResultJson(true, op, "preset saved", "", "", name);
}

String opDeleteFile(const String& op, const OperationArgs& args) {
  String name = argValue(args, "name", argValue(args, "file", ""));
  if (!isSafeWebFileName(name)) return opResultJson(false, op, "bad file name", "", "", name, "bad_name");
  bool ok = SD.remove(appPath(name).c_str());
  appendScanLog("-", "SD", ok ? "op_delete" : "op_delete_failed", name);
  return opResultJson(ok, op, ok ? "file deleted" : "delete failed", "", "", name, ok ? "" : "delete_failed");
}

String commandHelpJson() {
  return F("{\"ok\":true,\"op\":\"HELP\",\"message\":\"commands use key=value percent-encoded args; responses are newline-delimited JSON\",\"commands\":[\"HELP\",\"STATUS\",\"SCAN\",\"DUMP\",\"KEY_AUDIT\",\"WRITE_NDEF\",\"WRITE_FROM_SD\",\"CLONE\",\"VERIFY\",\"FILES\",\"GET_FILE\",\"PUT_PRESET\",\"DELETE\",\"EMULATE_NDEF\"],\"examples\":[\"STATUS\",\"WRITE_NDEF type=url content=https%3A%2F%2Fexample.com\",\"GET_FILE name=SCANLOG.CSV\"]}");
}

String runOperation(String op, const OperationArgs& args, const String& source) {
  op.toUpperCase();
  if (op == "HELP") return commandHelpJson();
  if (op == "STATUS") return statusJson();
  if (op == "FILES") return "{\"ok\":true,\"op\":\"FILES\",\"files\":" + filesJsonArray() + "}";
  if (operationBusy) return opResultJson(false, op, "busy", "", "", "", "busy");
  operationBusy = true;
  String result;
  if (op == "SCAN") result = opScanCard(op);
  else if (op == "DUMP") result = opDumpCard(op);
  else if (op == "KEY_AUDIT") result = opKeyAudit(op);
  else if (op == "WRITE_NDEF") result = opWriteNdef(op, args);
  else if (op == "WRITE_FROM_SD") result = opWriteFromSd(op, args);
  else if (op == "CLONE") result = opCloneMagic(op, args);
  else if (op == "VERIFY") result = opVerifyClone(op);
  else if (op == "PUT_PRESET") result = opPutPreset(op, args);
  else if (op == "DELETE") result = opDeleteFile(op, args);
  else if (op == "EMULATE_NDEF") {
    emulateNdefFromSD();
    result = opResultJson(true, op, "emulation stopped");
  } else {
    result = opResultJson(false, op, "unknown operation", "", "", "", "unknown_op");
  }
  appendScanLog("-", "workstation", "op_" + source, op);
  operationBusy = false;
  lastOperationJson = result;
  return result;
}

String hexEncodeBuffer(const uint8_t* data, int len) {
  String out = "";
  for (int i = 0; i < len; i++) {
    if (data[i] < 0x10) out += "0";
    out += String(data[i], HEX);
  }
  out.toUpperCase();
  return out;
}

void bleSendLine(const String& line) {
  if (!bleSerialActive || !bleClientConnected || bleTxCharacteristic == nullptr) return;
  String out = line + "\n";
  const size_t chunkSize = 20;
  for (size_t offset = 0; offset < out.length(); offset += chunkSize) {
    size_t n = min(chunkSize, out.length() - offset);
    bleTxCharacteristic->setValue((const uint8_t*)out.c_str() + offset, n);
    bleTxCharacteristic->notify();
    delay(2);
  }
}

void sendTransportLine(CommandTransport transport, const String& line) {
  if (transport == TRANSPORT_BLE) bleSendLine(line);
  else Serial.println(line);
}

const char* transportSourceName(CommandTransport transport) {
  return transport == TRANSPORT_BLE ? "ble" : "usb";
}

void sendFileChunks(CommandTransport transport, const OperationArgs& args) {
  String name = argValue(args, "name", argValue(args, "file", ""));
  if (!isSafeWebFileName(name)) {
    sendTransportLine(transport, opResultJson(false, "GET_FILE", "bad file name", "", "", name, "bad_name"));
    return;
  }
  File f = SD.open(appPath(name).c_str(), FILE_READ);
  if (!f) {
    sendTransportLine(transport, opResultJson(false, "GET_FILE", "not found", "", "", name, "not_found"));
    return;
  }
  sendTransportLine(transport, "{\"ok\":true,\"op\":\"GET_FILE\",\"filename\":\"" + jsonEscape(name) + "\",\"size\":" + String(f.size()) + ",\"start\":true}");
  uint8_t buf[48];
  uint32_t offset = 0;
  while (f.available()) {
    int n = f.read(buf, sizeof(buf));
    sendTransportLine(transport, "{\"ok\":true,\"op\":\"GET_FILE\",\"filename\":\"" + jsonEscape(name) +
                                 "\",\"offset\":" + String(offset) + ",\"hex\":\"" + hexEncodeBuffer(buf, n) + "\"}");
    offset += n;
    delay(1);
  }
  f.close();
  sendTransportLine(transport, "{\"ok\":true,\"op\":\"GET_FILE\",\"filename\":\"" + jsonEscape(name) + "\",\"end\":true}");
}

void handleTransportCommand(CommandTransport transport, const String& line) {
  OperationArgs args;
  String op = parseSerialOperation(line, &args);
  if (op.length() == 0) return;
  if (op == "GET_FILE") sendFileChunks(transport, args);
  else sendTransportLine(transport, runOperation(op, args, transportSourceName(transport)));
}

void handleSerialCommand(const String& line) {
  handleTransportCommand(TRANSPORT_USB, line);
}

void processSerialCommands() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialLineBuffer.length()) {
        handleSerialCommand(serialLineBuffer);
        serialLineBuffer = "";
      }
    } else if (serialLineBuffer.length() < 240) {
      serialLineBuffer += c;
    }
  }
}

bool enqueueBleCommand(const String& command) {
  if (bleCommandCount >= 4) return false;
  bleCommandQueue[bleCommandTail] = command;
  bleCommandTail = (bleCommandTail + 1) % 4;
  bleCommandCount++;
  return true;
}

bool dequeueBleCommand(String* command) {
  if (bleCommandCount == 0) return false;
  *command = bleCommandQueue[bleCommandHead];
  bleCommandQueue[bleCommandHead] = "";
  bleCommandHead = (bleCommandHead + 1) % 4;
  bleCommandCount--;
  return true;
}

void clearBleCommandQueue() {
  bleCommandHead = 0;
  bleCommandTail = 0;
  bleCommandCount = 0;
  for (int i = 0; i < 4; i++) bleCommandQueue[i] = "";
}

void queueBleSerialInput(const std::string& value) {
  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];
    if (c == '\n' || c == '\r') {
      if (bleLineBuffer.length()) {
        if (!enqueueBleCommand(bleLineBuffer)) {
          bleSendLine(opResultJson(false, "BLE", "command queue full", "", "", "", "busy"));
        }
        bleLineBuffer = "";
      }
    } else if (bleLineBuffer.length() < 240) {
      bleLineBuffer += c;
    }
  }
}

void processBleSerialCommands() {
  String command;
  while (dequeueBleCommand(&command)) {
    handleTransportCommand(TRANSPORT_BLE, command);
  }
}

void setupBleSerialService() {
  if (bleServer != nullptr) return;
  NimBLEDevice::init(BLE_DEVICE_NAME);
  bleServer = NimBLEDevice::createServer();
  bleServer->setCallbacks(new CypherBleServerCallbacks());
  NimBLEService* service = bleServer->createService(BLE_NUS_SERVICE_UUID);
  bleTxCharacteristic = service->createCharacteristic(BLE_NUS_TX_UUID, NIMBLE_PROPERTY::NOTIFY);
  NimBLECharacteristic* rxCharacteristic = service->createCharacteristic(
    BLE_NUS_RX_UUID,
    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
  );
  rxCharacteristic->setCallbacks(new CypherBleRxCallbacks());
  service->start();

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->setName(BLE_DEVICE_NAME);
  advertising->addServiceUUID(BLE_NUS_SERVICE_UUID);
  advertising->enableScanResponse(true);
}

void drawBleSerialScreen() {
  display.clearDisplay();
  drawBorder();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(8, 8);
  display.println("BLE Serial");
  display.drawLine(0, 24, SCREEN_WIDTH, 24, SSD1306_WHITE);
  display.setCursor(8, 34);
  display.print("Name: "); display.println(BLE_DEVICE_NAME);
  display.setCursor(8, 50);
  display.println("Nordic UART / no auth");
  display.setCursor(8, 66);
  display.print("State: ");
  display.println(bleClientConnected ? "connected" : "advertising");
  display.setCursor(8, 86);
  display.println("Send HELP for commands");
  display.setCursor(8, 114);
  display.println("Back/Q exits");
  display.display();
}

void startBleSerialMode() {
  displayInfo("BLE Serial", "Starting", "No auth controls");
  setupBleSerialService();
  bleSerialActive = true;
  bleClientConnected = bleServer != nullptr && bleServer->getConnectedCount() > 0;
  bleLineBuffer = "";
  clearBleCommandQueue();
  NimBLEDevice::startAdvertising();

  uint32_t lastDraw = 0;
  while (true) {
    M5Cardputer.update();
    processSerialCommands();
    processBleSerialCommands();
    int btn = getButtonInput();
    if (btn == BUTTON_BACK || btn == BUTTON_SELECT) break;
    if (millis() - lastDraw > 1000) {
      drawBleSerialScreen();
      lastDraw = millis();
    }
    delay(5);
  }

  NimBLEDevice::stopAdvertising();
  bleSerialActive = false;
  if (bleServer != nullptr && bleConnHandle != 0xFFFF) {
    bleServer->disconnect(bleConnHandle);
  }
  bleClientConnected = false;
  bleConnHandle = 0xFFFF;
  clearBleCommandQueue();
  displayInfo("BLE Serial", "Stopped");
  delay(1000);
  redisplayCurrentMenu();
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

void executeApduMenuItem(int idx) {
  switch (idx) {
    case 0: apduType4NdefProbeFlow(); break;
    case 1: apduSelectNdefAidFlow();  break;
    case 2: appState = STATE_MAIN_MENU; redisplayCurrentMenu(); break;
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
    case 8:
      appState = STATE_APDU_SUBMENU; currentSubMenuItem = 0;
      displayMenuScreen("APDU Lab", apduMenuItems, apduMenuCount, 0);
      break;
    case 9:
      startWebControlServer();
      break;
    case 10:
      startBleSerialMode();
      break;
    case 11:
      returnToLauncher();
      break;
  }
}

// ============================================================
// STATE MACHINE
// ============================================================

void handleButtonPress() {
  int btn = getButtonInput();
  if (btn == 0) return;

  // Resolve active menu context
  const char** items  = nullptr;
  int count           = 0;
  const char* title   = "";
  int* selPtr         = &currentMenuItem;

  switch (appState) {
    case STATE_MAIN_MENU:
      items = (const char**)mainMenuItems; count = mainMenuCount;
      title = "CYPHER NFC"; selPtr = &currentMenuItem;
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
    case STATE_APDU_SUBMENU:
      items = (const char**)apduMenuItems; count = apduMenuCount;
      title = "APDU Lab"; selPtr = &currentSubMenuItem;
      break;
    default: return;
  }

  if (btn == BUTTON_UP) {
    *selPtr = (*selPtr - 1 + count) % count;
    displayMenuScreen(title, items, count, *selPtr);
  } else if (btn == BUTTON_DOWN) {
    *selPtr = (*selPtr + 1) % count;
    displayMenuScreen(title, items, count, *selPtr);
  } else if (btn == BUTTON_BACK) {
    if (appState == STATE_MAIN_MENU) {
      returnToLauncher();
    } else {
      appState = STATE_MAIN_MENU;
      currentSubMenuItem = 0;
      redisplayCurrentMenu();
    }
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
      case STATE_APDU_SUBMENU:  executeApduMenuItem(currentSubMenuItem);   break;
    }
  }
}

// ============================================================
// SETUP & LOOP
// ============================================================

void returnToLauncher() {
#if __has_include(<CypherPuterReturn.h>)
  cypherPuterReturnToLauncher();
#else
  displayInfo("Standalone", "Launcher helper", "not linked", "Restarting...");
  delay(1000);
  ESP.restart();
#endif
}

void initDisplay() {
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setBrightness(180);
  M5Cardputer.Display.setTextDatum(top_left);
  M5Cardputer.Display.setTextWrap(false);
  display.clearDisplay();
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(200);

  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  initDisplay();

  displayTitleScreen();
  delay(2500);

  initSDCard();

  if (initPn532WithAttempts(PN532_BOOT_ATTEMPTS, true)) {
    displayInfo("PN532 Ready", pn532ChipLine(pn532FirmwareVersion),
                pn532FirmwareLine(pn532FirmwareVersion));
    delay(1500);
  } else {
    displayInfo("PN532 Missing", "Menu still works",
                "Select in NFC action", "Back item returns OS");
    delay(1800);
  }

  // Init global state — no dump loaded yet
  mifDump.totalBlocks = 0;
  ntagDump.totalPages = 0;
  keyMap.crackedCount = 0;
  keyMap.numSectors   = 0;

  redisplayCurrentMenu();
}

void loop() {
  M5Cardputer.update();
  processSerialCommands();
  handleButtonPress();
  delay(8);
}
