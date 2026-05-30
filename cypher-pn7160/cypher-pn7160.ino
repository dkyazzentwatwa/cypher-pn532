#include <Arduino.h>
#include <Wire.h>

#include <Electroniccats_PN7150.h>

static const uint8_t PIN_I2C_SDA = 21;
static const uint8_t PIN_I2C_SCL = 22;
static const uint8_t PIN_PN7160_IRQ = 14;
static const uint8_t PIN_PN7160_VEN = 13;
static const uint8_t PN7160_PRIMARY_ADDR = 0x28;
static const uint8_t PN7160_FALLBACK_ADDR = 0x7C;
static const uint32_t SERIAL_BAUD = 115200;
static const uint32_t DEFAULT_TIMEOUT_MS = 5000;
static const uint16_t MAX_SERIAL_LINE = 240;

Electroniccats_PN7150 nfcPrimary(PIN_PN7160_IRQ, PIN_PN7160_VEN, PN7160_PRIMARY_ADDR, PN7160, &Wire);
Electroniccats_PN7150 nfcFallback(PIN_PN7160_IRQ, PIN_PN7160_VEN, PN7160_FALLBACK_ADDR, PN7160, &Wire);
Electroniccats_PN7150 *nfc = &nfcPrimary;
NdefMessage ndefMessage;

String serialLine;
bool nfcReady = false;
bool tagActive = false;
bool ndefReceived = false;
bool expectedI2CFound = false;
bool fallbackI2CFound = false;
uint8_t activeNfcAddr = PN7160_PRIMARY_ADDR;
String lastError = "not_initialized";
uint32_t commandCount = 0;

String jsonEscape(const String &input);
String bytesToHex(const uint8_t *data, size_t len, char separator = 0);
String byteToHex(uint8_t value);
String commandName(const String &line);
String argValue(const String &line, const char *key, const String &fallback);
uint32_t argUint(const String &line, const char *key, uint32_t fallback, uint32_t maxValue);
String percentDecode(const String &input);
bool parseHexKey(String input, uint8_t key[6]);
bool scanI2CAddress(uint8_t address);
String scanI2CAddressesJson(bool *expectedFound);
void rebuildNfcDrivers();
bool initNfc();
bool initNfcAt(Electroniccats_PN7150 &candidate, uint8_t address);
bool ensureNfcReady(const String &op);
bool waitForTag(uint32_t timeoutMs);
bool prepareTagOperation(const String &op, uint32_t timeoutMs);
String protocolName(uint8_t protocol);
String techName(uint8_t tech);
String interfaceName(uint8_t interfaceValue);
String currentUidHex();
String currentTagJsonFields();
uint8_t mifareSectorForBlock(uint8_t block);
void printError(const String &op, const String &error, const String &message);
void handleHelp();
void handleStatus();
void handleI2CScan();
void handleReset();
void handleRelease(const String &line);
void handleScan(const String &line);
void handleReadNdef(const String &line);
void handleT2TRead(const String &line);
void handleMfcRead(const String &line);
void handleIso15693Read(const String &line);
void processCommand(String line);
void processSerialInput();
void onNdefReceived();

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(250);

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(100000);

  bool initOk = initNfc();
  Serial.print(F("{\"ok\":"));
  Serial.print(initOk ? F("true") : F("false"));
  Serial.print(F(",\"op\":\"BOOT\",\"message\":\"Cypher PN7160 DevKitC V4 lab firmware\",\"i2c_0x28\":"));
  Serial.print(expectedI2CFound ? F("true") : F("false"));
  Serial.print(F(",\"i2c_0x7c\":"));
  Serial.print(fallbackI2CFound ? F("true") : F("false"));
  Serial.print(F(",\"active_addr\":\"0x"));
  Serial.print(byteToHex(activeNfcAddr));
  Serial.print(F("\""));
  Serial.print(F(",\"nfc_ready\":"));
  Serial.print(nfcReady ? F("true") : F("false"));
  if (!initOk) {
    Serial.print(F(",\"error\":\""));
    Serial.print(jsonEscape(lastError));
    Serial.print(F("\""));
  }
  Serial.println(F("}"));
  handleHelp();
}

void loop() {
  processSerialInput();
}

String jsonEscape(const String &input) {
  String output;
  output.reserve(input.length() + 8);
  for (size_t i = 0; i < input.length(); i++) {
    char c = input[i];
    switch (c) {
      case '\\': output += F("\\\\"); break;
      case '"': output += F("\\\""); break;
      case '\n': output += F("\\n"); break;
      case '\r': output += F("\\r"); break;
      case '\t': output += F("\\t"); break;
      default:
        if ((uint8_t)c < 0x20) {
          output += ' ';
        } else {
          output += c;
        }
        break;
    }
  }
  return output;
}

String byteToHex(uint8_t value) {
  const char hex[] = "0123456789ABCDEF";
  String out;
  out += hex[(value >> 4) & 0x0F];
  out += hex[value & 0x0F];
  return out;
}

String bytesToHex(const uint8_t *data, size_t len, char separator) {
  if (data == nullptr || len == 0) return "";
  String out;
  out.reserve(len * (separator ? 3 : 2));
  for (size_t i = 0; i < len; i++) {
    if (separator && i > 0) out += separator;
    out += byteToHex(data[i]);
  }
  return out;
}

String commandName(const String &line) {
  int firstSpace = line.indexOf(' ');
  String cmd = firstSpace < 0 ? line : line.substring(0, firstSpace);
  cmd.trim();
  cmd.toUpperCase();
  return cmd;
}

String argValue(const String &line, const char *key, const String &fallback) {
  String wanted = String(key) + "=";
  int start = 0;
  while (start < line.length()) {
    while (start < line.length() && line[start] == ' ') start++;
    int end = line.indexOf(' ', start);
    if (end < 0) end = line.length();
    String token = line.substring(start, end);
    if (token.startsWith(wanted)) {
      return percentDecode(token.substring(wanted.length()));
    }
    start = end + 1;
  }
  return fallback;
}

uint32_t argUint(const String &line, const char *key, uint32_t fallback, uint32_t maxValue) {
  String value = argValue(line, key, "");
  if (value.length() == 0) return fallback;
  char *endPtr = nullptr;
  uint32_t parsed = strtoul(value.c_str(), &endPtr, 0);
  if (endPtr == value.c_str()) return fallback;
  if (parsed > maxValue) return maxValue;
  return parsed;
}

String percentDecode(const String &input) {
  String out;
  out.reserve(input.length());
  for (size_t i = 0; i < input.length(); i++) {
    char c = input[i];
    if (c == '%' && i + 2 < input.length()) {
      char hex[3] = {input[i + 1], input[i + 2], 0};
      char *endPtr = nullptr;
      long decoded = strtol(hex, &endPtr, 16);
      if (endPtr && *endPtr == '\0') {
        out += (char)decoded;
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

bool parseHexKey(String input, uint8_t key[6]) {
  input.replace(":", "");
  input.replace("-", "");
  input.replace(" ", "");
  input.toUpperCase();
  if (input.length() != 12) return false;

  for (uint8_t i = 0; i < 6; i++) {
    String pair = input.substring(i * 2, i * 2 + 2);
    char *endPtr = nullptr;
    long value = strtol(pair.c_str(), &endPtr, 16);
    if (endPtr == pair.c_str() || value < 0 || value > 255) return false;
    key[i] = (uint8_t)value;
  }
  return true;
}

bool scanI2CAddress(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

String scanI2CAddressesJson(bool *expectedFound) {
  String found = "[";
  bool first = true;
  bool sawExpected = false;
  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      if (!first) found += ',';
      found += "\"0x";
      found += byteToHex(address);
      found += "\"";
      first = false;
      if (address == PN7160_PRIMARY_ADDR) sawExpected = true;
    }
  }
  found += "]";
  if (expectedFound) *expectedFound = sawExpected;
  return found;
}

void rebuildNfcDrivers() {
  nfcPrimary = Electroniccats_PN7150(PIN_PN7160_IRQ, PIN_PN7160_VEN, PN7160_PRIMARY_ADDR, PN7160, &Wire);
  nfcFallback = Electroniccats_PN7150(PIN_PN7160_IRQ, PIN_PN7160_VEN, PN7160_FALLBACK_ADDR, PN7160, &Wire);
  nfc = &nfcPrimary;
}

bool initNfc() {
  rebuildNfcDrivers();
  expectedI2CFound = scanI2CAddress(PN7160_PRIMARY_ADDR);
  fallbackI2CFound = scanI2CAddress(PN7160_FALLBACK_ADDR);
  nfcReady = false;
  tagActive = false;
  lastError = "nfc_begin_failed";

  if (initNfcAt(nfcPrimary, PN7160_PRIMARY_ADDR)) return true;
  if (initNfcAt(nfcFallback, PN7160_FALLBACK_ADDR)) return true;

  nfc = &nfcPrimary;
  activeNfcAddr = PN7160_PRIMARY_ADDR;
  return false;
}

bool initNfcAt(Electroniccats_PN7150 &candidate, uint8_t address) {
  ndefReceived = false;
  candidate.setReadMsgCallback(onNdefReceived);

  if (candidate.begin() != NFC_SUCCESS) {
    nfcReady = false;
    lastError = "nfc_begin_failed";
    return false;
  }

  ndefMessage.begin();

  if (!candidate.setReaderWriterMode()) {
    nfcReady = false;
    lastError = "reader_writer_mode_failed";
    return false;
  }

  nfc = &candidate;
  activeNfcAddr = address;
  nfcReady = true;
  lastError = "";
  return true;
}

bool ensureNfcReady(const String &op) {
  if (nfcReady) return true;
  if (initNfc()) return true;
  printError(op, "nfc_not_ready", lastError.length() ? lastError : "PN7160 is not initialized");
  return false;
}

bool waitForTag(uint32_t timeoutMs) {
  uint32_t start = millis();
  while ((uint32_t)(millis() - start) < timeoutMs) {
    uint32_t elapsed = millis() - start;
    uint32_t remaining = timeoutMs > elapsed ? timeoutMs - elapsed : 0;
    uint16_t slice = remaining > 250 ? 250 : (uint16_t)remaining;
    if (slice == 0) slice = 1;
    if (nfc->isTagDetected(slice)) return true;
    delay(5);
  }
  return false;
}

bool prepareTagOperation(const String &op, uint32_t timeoutMs) {
  if (!ensureNfcReady(op)) return false;

  if (tagActive && nfc->remoteDevice.getProtocol() != PROT_UNDETERMINED) {
    lastError = "";
    return true;
  }

  tagActive = false;
  if (!waitForTag(timeoutMs)) {
    printError(op, "timeout", "no tag detected");
    return false;
  }

  tagActive = true;
  lastError = "";
  return true;
}

String protocolName(uint8_t protocol) {
  switch (protocol) {
    case PROT_T1T: return "T1T";
    case PROT_T2T: return "T2T";
    case PROT_T3T: return "T3T";
    case PROT_ISODEP: return "ISO-DEP";
    case PROT_NFCDEP: return "NFC-DEP";
    case PROT_ISO15693: return "ISO15693";
    case PROT_MIFARE: return "MIFARE";
    case PROT_UNDETERMINED:
    default: return "UNDETERMINED";
  }
}

String techName(uint8_t tech) {
  switch (tech) {
    case TECH_PASSIVE_NFCA: return "NFC-A";
    case TECH_PASSIVE_NFCB: return "NFC-B";
    case TECH_PASSIVE_NFCF: return "NFC-F";
    case TECH_ACTIVE_NFCA: return "NFC-A-ACTIVE";
    case TECH_ACTIVE_NFCF: return "NFC-F-ACTIVE";
    case TECH_PASSIVE_15693: return "NFC-V";
    default: return "UNKNOWN";
  }
}

String interfaceName(uint8_t interfaceValue) {
  switch (interfaceValue) {
    case INTF_FRAME: return "FRAME";
    case INTF_ISODEP: return "ISO-DEP";
    case INTF_NFCDEP: return "NFC-DEP";
    case INTF_TAGCMD: return "TAG";
    case INTF_UNDETERMINED:
    default: return "UNDETERMINED";
  }
}

String currentUidHex() {
  if (nfc->remoteDevice.getModeTech() == TECH_PASSIVE_15693 ||
      nfc->remoteDevice.getProtocol() == PROT_ISO15693) {
    return bytesToHex((const uint8_t *)nfc->remoteDevice.getID(), 8);
  }
  return bytesToHex((const uint8_t *)nfc->remoteDevice.getNFCID(),
                    nfc->remoteDevice.getNFCIDLen());
}

String currentTagJsonFields() {
  String fields;
  fields += ",\"interface\":\"" + interfaceName(nfc->remoteDevice.getInterface()) + "\"";
  fields += ",\"protocol\":\"" + protocolName(nfc->remoteDevice.getProtocol()) + "\"";
  fields += ",\"tech\":\"" + techName(nfc->remoteDevice.getModeTech()) + "\"";
  fields += ",\"uid\":\"" + currentUidHex() + "\"";
  fields += ",\"more_tags\":";
  fields += nfc->remoteDevice.hasMoreTags() ? "true" : "false";
  fields += ",\"tag_active\":";
  fields += tagActive ? "true" : "false";
  if (nfc->remoteDevice.getProtocol() == PROT_ISO15693) {
    fields += ",\"afi\":" + String(nfc->remoteDevice.getAFI());
    fields += ",\"dsfid\":" + String(nfc->remoteDevice.getDSFID());
  }
  return fields;
}

uint8_t mifareSectorForBlock(uint8_t block) {
  if (block < 128) return block / 4;
  return 32 + ((block - 128) / 16);
}

void printError(const String &op, const String &error, const String &message) {
  Serial.print(F("{\"ok\":false,\"op\":\""));
  Serial.print(jsonEscape(op));
  Serial.print(F("\",\"error\":\""));
  Serial.print(jsonEscape(error));
  Serial.print(F("\",\"message\":\""));
  Serial.print(jsonEscape(message));
  Serial.println(F("\"}"));
}

void handleHelp() {
  Serial.println(F("{\"ok\":true,\"op\":\"HELP\",\"message\":\"newline-delimited JSON; commands are uppercase with optional key=value args\",\"commands\":[\"HELP\",\"STATUS\",\"I2C_SCAN\",\"SCAN timeout_ms=5000\",\"READ_NDEF timeout_ms=5000\",\"T2T_READ block=5\",\"MFC_READ block=4 key=FFFFFFFFFFFF\",\"ISO15693_READ block=8\",\"RELEASE wait_remove=1\",\"RESET\"]}"));
}

void handleStatus() {
  Serial.print(F("{\"ok\":true,\"op\":\"STATUS\",\"uptime_ms\":"));
  Serial.print(millis());
  Serial.print(F(",\"command_count\":"));
  Serial.print(commandCount);
  Serial.print(F(",\"nfc_ready\":"));
  Serial.print(nfcReady ? F("true") : F("false"));
  Serial.print(F(",\"tag_active\":"));
  Serial.print(tagActive ? F("true") : F("false"));
  Serial.print(F(",\"i2c_0x28_boot\":"));
  Serial.print(expectedI2CFound ? F("true") : F("false"));
  Serial.print(F(",\"i2c_0x7c_boot\":"));
  Serial.print(fallbackI2CFound ? F("true") : F("false"));
  Serial.print(F(",\"active_addr\":\"0x"));
  Serial.print(byteToHex(activeNfcAddr));
  Serial.print(F("\",\"primary_addr\":\"0x28\",\"fallback_addr\":\"0x7C\",\"pins\":{\"sda\":21,\"scl\":22,\"irq\":14,\"ven\":13}"));
  if (lastError.length()) {
    Serial.print(F(",\"last_error\":\""));
    Serial.print(jsonEscape(lastError));
    Serial.print(F("\""));
  }
  Serial.println(F("}"));
}

void handleI2CScan() {
  bool primaryFound = false;
  String addresses = scanI2CAddressesJson(&primaryFound);
  expectedI2CFound = primaryFound;
  fallbackI2CFound = scanI2CAddress(PN7160_FALLBACK_ADDR);
  Serial.print(F("{\"ok\":true,\"op\":\"I2C_SCAN\",\"primary\":\"0x28\",\"primary_found\":"));
  Serial.print(primaryFound ? F("true") : F("false"));
  Serial.print(F(",\"fallback\":\"0x7C\",\"fallback_found\":"));
  Serial.print(fallbackI2CFound ? F("true") : F("false"));
  Serial.print(F(",\"addresses\":"));
  Serial.print(addresses);
  Serial.println(F("}"));
}

void handleReset() {
  tagActive = false;
  bool ok = initNfc();
  Serial.print(F("{\"ok\":"));
  Serial.print(ok ? F("true") : F("false"));
  Serial.print(F(",\"op\":\"RESET\",\"i2c_0x28\":"));
  Serial.print(expectedI2CFound ? F("true") : F("false"));
  Serial.print(F(",\"i2c_0x7c\":"));
  Serial.print(fallbackI2CFound ? F("true") : F("false"));
  Serial.print(F(",\"active_addr\":\"0x"));
  Serial.print(byteToHex(activeNfcAddr));
  Serial.print(F("\""));
  Serial.print(F(",\"nfc_ready\":"));
  Serial.print(nfcReady ? F("true") : F("false"));
  Serial.print(F(",\"tag_active\":"));
  Serial.print(tagActive ? F("true") : F("false"));
  if (!ok) {
    Serial.print(F(",\"error\":\""));
    Serial.print(jsonEscape(lastError));
    Serial.print(F("\""));
  }
  Serial.println(F("}"));
}

void handleRelease(const String &line) {
  const String op = "RELEASE";
  if (!ensureNfcReady(op)) return;

  bool waitRemove = argUint(line, "wait_remove", 0, 1) == 1;
  if (waitRemove && tagActive) {
    nfc->waitForTagRemoval();
  }

  tagActive = false;
  String recovery = "reset";
  bool resetOk = nfc->reset();
  if (!resetOk) {
    recovery = "full_init";
    resetOk = initNfc();
  }

  if (resetOk) {
    lastError = "";
  } else {
    if (!lastError.length()) lastError = "recovery_failed";
  }

  Serial.print(F("{\"ok\":"));
  Serial.print(resetOk ? F("true") : F("false"));
  Serial.print(F(",\"op\":\"RELEASE\",\"wait_remove\":"));
  Serial.print(waitRemove ? F("true") : F("false"));
  Serial.print(F(",\"reset_ok\":"));
  Serial.print(resetOk ? F("true") : F("false"));
  Serial.print(F(",\"recovery\":\""));
  Serial.print(jsonEscape(resetOk ? recovery : "failed"));
  Serial.print(F("\""));
  Serial.print(F(",\"nfc_ready\":"));
  Serial.print(nfcReady ? F("true") : F("false"));
  Serial.print(F(",\"tag_active\":"));
  Serial.print(tagActive ? F("true") : F("false"));
  if (!resetOk) {
    Serial.print(F(",\"error\":\"recovery_failed\",\"detail\":\""));
    Serial.print(jsonEscape(lastError));
    Serial.print(F("\",\"message\":\"remove the tag and retry RELEASE wait_remove=1 or RESET\""));
  } else {
    Serial.print(F(",\"message\":\"discovery restarted\""));
  }
  Serial.println(F("}"));
}

void handleScan(const String &line) {
  const String op = "SCAN";
  uint32_t timeoutMs = argUint(line, "timeout_ms", DEFAULT_TIMEOUT_MS, 60000);
  if (!prepareTagOperation(op, timeoutMs)) return;
  Serial.print(F("{\"ok\":true,\"op\":\"SCAN\""));
  Serial.print(currentTagJsonFields());
  Serial.println(F(",\"message\":\"tag detected\"}"));
}

void handleReadNdef(const String &line) {
  const String op = "READ_NDEF";
  uint32_t timeoutMs = argUint(line, "timeout_ms", DEFAULT_TIMEOUT_MS, 60000);
  if (!prepareTagOperation(op, timeoutMs)) return;

  uint8_t protocol = nfc->remoteDevice.getProtocol();
  if (!(protocol == PROT_T1T || protocol == PROT_T2T || protocol == PROT_T3T ||
        protocol == PROT_ISODEP || protocol == PROT_MIFARE)) {
    Serial.print(F("{\"ok\":false,\"op\":\"READ_NDEF\",\"error\":\"unsupported_protocol\""));
    Serial.print(currentTagJsonFields());
    Serial.println(F(",\"message\":\"NDEF read is only attempted on Type 1/2/3/4 and MIFARE in this v1 firmware\"}"));
    return;
  }

  ndefReceived = false;
  nfc->readNdefMessage();
  if (!ndefReceived || ndefMessage.isEmpty()) {
    Serial.print(F("{\"ok\":false,\"op\":\"READ_NDEF\",\"error\":\"ndef_not_found\""));
    Serial.print(currentTagJsonFields());
    Serial.println(F(",\"message\":\"no NDEF message was returned by the library\"}"));
    return;
  }

  Serial.print(F("{\"ok\":true,\"op\":\"READ_NDEF\""));
  Serial.print(currentTagJsonFields());
  Serial.print(F(",\"ndef_len\":"));
  Serial.print(ndefMessage.getContentLength());
  Serial.print(F(",\"ndef_hex\":\""));
  Serial.print(bytesToHex((const uint8_t *)ndefMessage.getContent(), ndefMessage.getContentLength()));
  Serial.println(F("\",\"message\":\"NDEF read\"}"));
}

void handleT2TRead(const String &line) {
  const String op = "T2T_READ";
  uint8_t block = (uint8_t)argUint(line, "block", 5, 255);
  uint32_t timeoutMs = argUint(line, "timeout_ms", DEFAULT_TIMEOUT_MS, 60000);
  if (!prepareTagOperation(op, timeoutMs)) return;
  if (nfc->remoteDevice.getProtocol() != PROT_T2T) {
    Serial.print(F("{\"ok\":false,\"op\":\"T2T_READ\",\"error\":\"wrong_protocol\""));
    Serial.print(currentTagJsonFields());
    Serial.println(F(",\"message\":\"tag is not Type 2\"}"));
    return;
  }

  unsigned char command[] = {0x30, block};
  unsigned char response[256] = {0};
  unsigned char responseSize = 0;
  bool status = nfc->readerTagCmd(command, sizeof(command), response, &responseSize);
  if (status == NFC_ERROR || responseSize < 4) {
    Serial.print(F("{\"ok\":false,\"op\":\"T2T_READ\",\"error\":\"read_failed\""));
    Serial.print(currentTagJsonFields());
    Serial.print(F(",\"block\":"));
    Serial.print(block);
    Serial.print(F(",\"response_hex\":\""));
    Serial.print(bytesToHex((const uint8_t *)response, responseSize));
    Serial.println(F("\"}"));
    return;
  }

  Serial.print(F("{\"ok\":true,\"op\":\"T2T_READ\""));
  Serial.print(currentTagJsonFields());
  Serial.print(F(",\"block\":"));
  Serial.print(block);
  Serial.print(F(",\"data_hex\":\""));
  Serial.print(bytesToHex((const uint8_t *)response, 4));
  Serial.print(F("\",\"raw_hex\":\""));
  Serial.print(bytesToHex((const uint8_t *)response, responseSize));
  Serial.println(F("\",\"message\":\"Type 2 block/page read\"}"));
}

void handleMfcRead(const String &line) {
  const String op = "MFC_READ";
  uint8_t block = (uint8_t)argUint(line, "block", 4, 255);
  uint32_t timeoutMs = argUint(line, "timeout_ms", DEFAULT_TIMEOUT_MS, 60000);
  uint8_t key[6];
  if (!parseHexKey(argValue(line, "key", "FFFFFFFFFFFF"), key)) {
    printError(op, "bad_key", "key must be 6 hex bytes, for example FFFFFFFFFFFF");
    return;
  }

  if (!prepareTagOperation(op, timeoutMs)) return;
  if (nfc->remoteDevice.getProtocol() != PROT_MIFARE) {
    Serial.print(F("{\"ok\":false,\"op\":\"MFC_READ\",\"error\":\"wrong_protocol\""));
    Serial.print(currentTagJsonFields());
    Serial.println(F(",\"message\":\"tag is not MIFARE Classic\"}"));
    return;
  }

  unsigned char auth[] = {0x40, mifareSectorForBlock(block), 0x10, key[0], key[1], key[2], key[3], key[4], key[5]};
  unsigned char read[] = {0x10, 0x30, block};
  unsigned char response[256] = {0};
  unsigned char responseSize = 0;

  bool status = nfc->readerTagCmd(auth, sizeof(auth), response, &responseSize);
  if (status == NFC_ERROR || responseSize == 0 || response[responseSize - 1] != 0x00) {
    Serial.print(F("{\"ok\":false,\"op\":\"MFC_READ\",\"error\":\"auth_failed\""));
    Serial.print(currentTagJsonFields());
    Serial.print(F(",\"block\":"));
    Serial.print(block);
    Serial.print(F(",\"sector\":"));
    Serial.print(mifareSectorForBlock(block));
    Serial.print(F(",\"response_hex\":\""));
    Serial.print(bytesToHex((const uint8_t *)response, responseSize));
    Serial.println(F("\"}"));
    return;
  }

  memset(response, 0, sizeof(response));
  responseSize = 0;
  status = nfc->readerTagCmd(read, sizeof(read), response, &responseSize);
  if (status == NFC_ERROR || responseSize < 3 || response[responseSize - 1] != 0x00) {
    Serial.print(F("{\"ok\":false,\"op\":\"MFC_READ\",\"error\":\"read_failed\""));
    Serial.print(currentTagJsonFields());
    Serial.print(F(",\"block\":"));
    Serial.print(block);
    Serial.print(F(",\"response_hex\":\""));
    Serial.print(bytesToHex((const uint8_t *)response, responseSize));
    Serial.println(F("\"}"));
    return;
  }

  Serial.print(F("{\"ok\":true,\"op\":\"MFC_READ\""));
  Serial.print(currentTagJsonFields());
  Serial.print(F(",\"block\":"));
  Serial.print(block);
  Serial.print(F(",\"sector\":"));
  Serial.print(mifareSectorForBlock(block));
  Serial.print(F(",\"data_hex\":\""));
  Serial.print(bytesToHex((const uint8_t *)(response + 1), responseSize - 2));
  Serial.println(F("\",\"message\":\"MIFARE Classic block read\"}"));
}

void handleIso15693Read(const String &line) {
  const String op = "ISO15693_READ";
  uint8_t block = (uint8_t)argUint(line, "block", 8, 255);
  uint32_t timeoutMs = argUint(line, "timeout_ms", DEFAULT_TIMEOUT_MS, 60000);
  if (!prepareTagOperation(op, timeoutMs)) return;
  if (nfc->remoteDevice.getProtocol() != PROT_ISO15693) {
    Serial.print(F("{\"ok\":false,\"op\":\"ISO15693_READ\",\"error\":\"wrong_protocol\""));
    Serial.print(currentTagJsonFields());
    Serial.println(F(",\"message\":\"tag is not ISO15693 / NFC-V\"}"));
    return;
  }

  unsigned char read[] = {0x02, 0x20, block};
  unsigned char response[256] = {0};
  unsigned char responseSize = 0;
  bool status = nfc->readerTagCmd(read, sizeof(read), response, &responseSize);
  if (status == NFC_ERROR || responseSize < 3 || response[responseSize - 1] != 0x00) {
    Serial.print(F("{\"ok\":false,\"op\":\"ISO15693_READ\",\"error\":\"read_failed\""));
    Serial.print(currentTagJsonFields());
    Serial.print(F(",\"block\":"));
    Serial.print(block);
    Serial.print(F(",\"response_hex\":\""));
    Serial.print(bytesToHex((const uint8_t *)response, responseSize));
    Serial.println(F("\"}"));
    return;
  }

  Serial.print(F("{\"ok\":true,\"op\":\"ISO15693_READ\""));
  Serial.print(currentTagJsonFields());
  Serial.print(F(",\"block\":"));
  Serial.print(block);
  Serial.print(F(",\"data_hex\":\""));
  Serial.print(bytesToHex((const uint8_t *)(response + 1), responseSize - 2));
  Serial.println(F("\",\"message\":\"ISO15693 block read\"}"));
}

void processCommand(String line) {
  line.trim();
  if (line.length() == 0) return;
  commandCount++;

  String cmd = commandName(line);
  if (cmd == "HELP" || cmd == "?") handleHelp();
  else if (cmd == "STATUS") handleStatus();
  else if (cmd == "I2C_SCAN") handleI2CScan();
  else if (cmd == "RESET") handleReset();
  else if (cmd == "RELEASE") handleRelease(line);
  else if (cmd == "SCAN") handleScan(line);
  else if (cmd == "READ_NDEF") handleReadNdef(line);
  else if (cmd == "T2T_READ") handleT2TRead(line);
  else if (cmd == "MFC_READ") handleMfcRead(line);
  else if (cmd == "ISO15693_READ") handleIso15693Read(line);
  else printError(cmd, "unknown_command", "send HELP for supported commands");
}

void processSerialInput() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      processCommand(serialLine);
      serialLine = "";
      continue;
    }
    if (serialLine.length() >= MAX_SERIAL_LINE) {
      serialLine = "";
      printError("SERIAL", "line_too_long", "command line was discarded");
      continue;
    }
    serialLine += c;
  }
}

void onNdefReceived() {
  ndefReceived = true;
}
