# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Cypher PN532 is an Arduino firmware for an ESP32-C3 Super Mini-based NFC/RFID handheld device and open PN532 field workstation. The canonical ESP32-C3/Cypherbox source is `cypher_pn532/cypher_pn532.ino`; the Cardputer port mirrors workstation behavior in `CardputerPN532/CardputerPN532.ino`.

## Hardware

| Component | Pin(s) |
|-----------|--------|
| PN532 NFC module (I2C) | IRQ=2, RESET=3 |
| SSD1306 128x64 OLED (I2C) | I2C address 0x3C, SDA=8, SCL=9 |
| SD card (SPI) | CS=10, MOSI=6, MISO=5, SCK=4 |
| Button UP | **GPIO 7** (rewired from 3 — was conflicting with PN532_RESET) |
| Button DOWN | GPIO 1 |
| Button SELECT | **GPIO 0** (rewired from 2 — was conflicting with PN532_IRQ) |

## Build & Flash

Open `cypher_pn532/cypher_pn532.ino` in Arduino IDE 2.x. Select board: **XIAO_ESP32C3** (Tools → Board → ESP32 Arduino → XIAO_ESP32C3). CLI FQBN: `esp32:esp32:XIAO_ESP32C3:CDCOnBoot=cdc,PartitionScheme=huge_app`.

Required libraries (install via Library Manager):
- `Adafruit PN532`
- `Adafruit GFX Library`
- `Adafruit SSD1306`
- `U8g2_for_Adafruit_GFX`
- `NimBLE-Arduino`
- `SD` (built-in ESP32 SD library)

Serial debug output at 115200 baud.

## Workstation Control Surfaces

All variants expose foreground NFC operations through device menus, USB serial
commands at 115200 baud, a manually launched no-auth Web Control AP at
`http://192.168.4.1`, and menu-launched no-auth BLE Serial using Nordic UART
Service as `CYPHER-PN532`.

USB/BLE/Web operations use these names: `HELP`, `STATUS`, `SCAN`, `DUMP`, `KEY_AUDIT`,
`WRITE_NDEF`, `WRITE_FROM_SD`, `CLONE`, `VERIFY`, `FILES`, `GET_FILE`,
`PUT_PRESET`, `DELETE`, and `EMULATE_NDEF`. Arguments are `key=value` tokens
with percent-encoded values; responses are newline-delimited JSON. The host
helper is `tools/cypher_pn532_cli.py`.

Web Control intentionally has no HTTP auth and includes destructive controls.
Keep it device-launched and lab-scoped; do not make it auto-start in `setup()`.
BLE Serial intentionally has no pairing/app auth and includes destructive
controls. Keep it menu-launched only; do not make it auto-start in `setup()`.

## SD Card Presets (optional)

Place these files on the SD card to customize NDEF write defaults:
- `/NDEF_URL.TXT` — URL suffix for "Write NDEF URL" (e.g. `example.com`; `https://` prefix is prepended automatically)
- `/NDEF_TXT.TXT` — Text string for "Write NDEF Text"
- `/ndef/*.txt` — selectable payloads for Type 4 "NDEF from SD" emulation on ESP32-C3/Cypherbox Mini.
- `.txt` files selected by "Write from SD" may begin with `url:` for URI NDEF or `text:` for text NDEF; bare content is written as text.
- `/KEYS.TXT` — optional extra MIFARE Classic keys, one 6-byte hex key per line, merged after the built-in 50-key dictionary.
- Scans, reads, writes, demos, and dumps append to `/SCANLOG.CSV` on ESP32-C3 or `/cypher-pn532/SCANLOG.CSV` on Cardputer using `uptime_ms,uid,type,action,filename`.
- Dumps and key audits also write `.json` metadata sidecars with schema version, device, firmware, UID, card type, capacity, files, key summary, and NDEF metadata.

## Architecture

### State Machine

`loop()` calls `handleButtonPress()` which switches on `AppState` enum:

```
STATE_MAIN_MENU
STATE_READ_SUBMENU
STATE_ATTACK_SUBMENU
STATE_CLONE_SUBMENU
STATE_WRITE_SUBMENU
STATE_SD_SUBMENU
STATE_DEMO_SUBMENU
STATE_EMULATE_SUBMENU
STATE_APDU_SUBMENU
```

Navigation (`currentMenuItem` for main, `currentSubMenuItem` for all submenus) is handled uniformly. "Back" items in submenus set `appState = STATE_MAIN_MENU`.

### Menu Structure

```
CYPHER NFC (main)
├── Scan & Info       → detectCardType() + display UID/type/size
├── Demo Mode         → Tag Studio / Dump + Web / Badge Writer / Puzzle Hunt
├── Read Card         → UID Only / Read NDEF / Dump MIFARE / Dump NTAG
├── Key Attack        → Dictionary Attack (50-key table vs Key A+B per sector)
├── Clone Card        → Dump to SD / SD to Magic Card / Verify Clone
├── Write Card        → Write NDEF URL / Write NDEF Text / Write from SD
├── SD Card           → Browse Files / Hex View File / Delete File
├── Emulate Tag       → NDEF from SD (Type 4) / NTAG Dump / UID Only
├── APDU Lab          → Type4 NDEF Probe / Select NDEF AID
├── Web Control       → no-auth AP control surface
└── BLE Serial        → no-auth Nordic UART command surface
```

### Card Emulation (Emulate Tag)

The PN532 acts as an ISO14443A **target** (`TgInitAsTarget` 0x8C / `TgGetData`
0x86 / `TgSetData` 0x8E). `NDEF from SD` presents an ISO14443-4 **Type 4**
NDEF tag for phone-friendly handoff demos. `NTAG Dump` and `UID Only` keep the
older Type 2-style page image (`emuPageImage`, page 3 = CC `E1 10 12 00`) for
lab use:

- **NDEF from SD** — `/ndef/*.txt`, `/NDEF_URL.TXT`, or `/NDEF_TXT.TXT` via a Type 4 NDEF file and APDU handler.
- **NTAG Dump** — replays a saved `ntgNNN.bin` (sequential 4-byte pages).
- **UID Only** — spoofs the scanned UID with an empty NDEF body.

`runType4NdefEmulation()` handles SELECT NDEF app, SELECT CC file, SELECT NDEF file, and READ BINARY. `runType2Emulation()` loops until SELECT/Back, answering Type 2 READ (0x30).

**Hard limits (do not work around):**
- **No MIFARE Classic emulation** — the PN532 firmware can't run Crypto1
  authentication as a target, so `.mfd` dumps cannot be replayed to a reader.
- Only 3 NFCID1 bytes are controllable, so the RF UID is a best-effort spoof.
- Cheap PN532 antennas radiate weakly in target mode; tap the phone directly.
  iPhone is the most reliable reader.

The ESP32-C3 build talks to the chip with standalone raw-I2C frame helpers
(`pn532_sendCmd` / `pn532_readResp`) since `Adafruit_PN532::readdata` is private;
the Cardputer build adds `tgInitAsTarget`/`tgGetData`/`tgSetData` to its own
`M5Pn532` driver class. `CypherboxMiniPN532` `#include`s the C3 source, so it
inherits the feature.

### APDU Lab

`APDU Lab` is intentionally read-only and fixed to safe NDEF probes:
- `Type4 NDEF Probe` selects the NFC Forum NDEF AID, reads CC, selects the NDEF file, and reads NLEN + first payload bytes.
- `Select NDEF AID` only displays response length and SW1/SW2.
- Do not add payment AID presets or generic payment-card flows here. Apple's `NFCPaymentTagReaderSession` is a separate EU-gated iOS API and does not change PN532 firmware behavior directly.
- Do not claim parity with PN532Killer-class RF features on stock PN532 hardware. RF sniffing, MIFARE Classic target emulation, ISO15693/ICODE, EM4100/125 kHz LF, and Mfkey32/Mfkey64-style workflows require different hardware or a host/coprocessor path outside this firmware.

### Key Global Data Structures

| Variable | Type | Size | Purpose |
|----------|------|------|---------|
| `mifDump` | `MifareDump` | ~5.9 KB | MIFARE 1K/4K dump buffer |
| `ntagDump` | `NTAGDump` | ~1.2 KB | NTAG/Ultralight dump buffer |
| `keyMap` | `SectorKeyMap` | ~570 B | Per-sector Key A/B results from dictionary attack |
| `defaultKeys[50][6]` | const array | 300 B | Common MIFARE Classic default keys |

### Card Type Detection

`detectCardType(uid, uidLen, &info)` identifies:
- 4-byte UID → MIFARE Classic 1K (64 blocks) or 4K (256 blocks); probes block 128 to distinguish
- 7-byte UID → NTAG; reads page 3 CC byte: `0x12`=NTAG213, `0x3E`=NTAG215, `0x6D`=NTAG216, else Ultralight
- failed MIFARE/NTAG-specific read → safe NFC Forum NDEF AID SELECT probe; success is reported as `ISO14443-4 T4T`

### SD File Naming

Sequential filenames using `/COUNTER.TXT` on the SD card:

| Type | Prefix | Extension | Example |
|------|--------|-----------|---------|
| MIFARE binary dump | `dmp` | `.mfd` | `dmp001.mfd` |
| MIFARE text dump | `dmp` | `.txt` | `dmp001.txt` |
| NTAG binary dump | `ntg` | `.bin` | `ntg002.bin` |
| NTAG text dump | `ntg` | `.txt` | `ntg002.txt` |
| Key map | `key` | `.txt` | `key003.txt` |
| Scan log | `SCANLOG` | `.CSV` | `SCANLOG.CSV` |

### NDEF Write Format

`buildAndWriteNDEF()` writes a TLV-wrapped NDEF record starting at page 4:
- URL records: `[0x03][len][0xD1][0x01][payLen]['U'][prefix][url...][0xFE]`
- Text records: `[0x03][len][0xD1][0x01][payLen]['T'][0x02]['e']['n'][text...][0xFE]`
Max payload ~120 bytes (fits NTAG213's 144-byte user area).
`readNDEF()` decodes short well-known URI (`U`) and Text (`T`) records first, then keeps raw page view as fallback.

### Magic Card Detection

`detectMagicCard()` authenticates sector 0 with the default key, reads block 0, then attempts to write it back unchanged. Real MIFARE Classic rejects block 0 writes at hardware level; Gen1a/CUID magic cards accept them.

### Key Adafruit_PN532 Methods Used

| Operation | Method |
|-----------|--------|
| Detect card | `nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen)` |
| MIFARE auth | `nfc.mifareclassic_AuthenticateBlock(uid, uidLen, block, keyType, key)` |
| MIFARE read block | `nfc.mifareclassic_ReadDataBlock(block, buf)` |
| MIFARE write block | `nfc.mifareclassic_WriteDataBlock(block, buf)` |
| NTAG read page | `nfc.ntag2xx_ReadPage(page, buf)` |
| NTAG write page | `nfc.ntag2xx_WritePage(page, buf)` |
| Raw APDU / NTAG extended | `nfc.inDataExchange(cmd, len, resp, &respLen)` |

`readNTAGPageRaw()` uses `inDataExchange({0x30, page})` for reliable reads across all page ranges.
