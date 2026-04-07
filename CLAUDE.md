# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Cypher PN532 is an Arduino firmware for an ESP32-C3 Super Mini-based NFC/RFID handheld device. The single source file is `cypher_pn532/cypher_pn532.ino`.

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

Open `cypher_pn532/cypher_pn532.ino` in Arduino IDE 2.x. Select board: **ESP32C3 Dev Module**.

Required libraries (install via Library Manager):
- `Adafruit PN532`
- `Adafruit GFX Library`
- `Adafruit SSD1306`
- `U8g2_for_Adafruit_GFX`
- `SD` (built-in ESP32 SD library)

Serial debug output at 115200 baud.

## SD Card Presets (optional)

Place these files on the SD card to customize NDEF write defaults:
- `/NDEF_URL.TXT` — URL suffix for "Write NDEF URL" (e.g. `example.com`; `https://` prefix is prepended automatically)
- `/NDEF_TXT.TXT` — Text string for "Write NDEF Text"

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
```

Navigation (`currentMenuItem` for main, `currentSubMenuItem` for all submenus) is handled uniformly. "Back" items in submenus set `appState = STATE_MAIN_MENU`.

### Menu Structure

```
CYPHER NFC (main)
├── Scan & Info       → detectCardType() + display UID/type/size
├── Read Card         → UID Only / Read NDEF / Dump MIFARE / Dump NTAG
├── Key Attack        → Dictionary Attack (50-key table vs Key A+B per sector)
├── Clone Card        → Dump to SD / SD to Magic Card / Verify Clone
├── Write Card        → Write NDEF URL / Write NDEF Text / Write from SD
└── SD Card           → Browse Files / Hex View File / Delete File
```

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

### SD File Naming

Sequential filenames using `/COUNTER.TXT` on the SD card:

| Type | Prefix | Extension | Example |
|------|--------|-----------|---------|
| MIFARE binary dump | `dmp` | `.mfd` | `dmp001.mfd` |
| MIFARE text dump | `dmp` | `.txt` | `dmp001.txt` |
| NTAG binary dump | `ntg` | `.bin` | `ntg002.bin` |
| NTAG text dump | `ntg` | `.txt` | `ntg002.txt` |
| Key map | `key` | `.txt` | `key003.txt` |

### NDEF Write Format

`buildAndWriteNDEF()` writes a TLV-wrapped NDEF record starting at page 4:
- URL records: `[0x03][len][0xD1][0x01][payLen]['U'][prefix][url...][0xFE]`
- Text records: `[0x03][len][0xD1][0x01][payLen]['T'][0x02]['e']['n'][text...][0xFE]`
Max payload ~120 bytes (fits NTAG213's 144-byte user area).

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
