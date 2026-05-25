# CYPHER NFC v2.0
## Advanced ESP32-C3 NFC/RFID Device with MIFARE Cloning, Dictionary Attacks & Full Card Dumps

<img src="img/img6.JPG" alt="RFID/NFC Module" width="500" height="600">

This project provides a comprehensive, handheld NFC/RFID toolkit built on the ESP32-C3 Super Mini with PN532 chipset. **v2.0 is an open PN532 field workstation** featuring proper state machines, card type auto-detection, full MIFARE Classic and NTAG dumps, dictionary attacks, magic card cloning, NDEF writing, Type 4 NDEF emulation, a safe APDU lab, USB/BLE serial control, no-auth Web Control mode, JSON sidecar artifacts, and a hex file viewer.

- **Handheld device**: ESP32-C3 Super Mini + SSD1306 128x64 OLED + SD card module + PN532 RFID/NFC + 3-button UI
- **Open field workstation**: Device UI, USB/BLE CLI, SD artifacts, and browser controls for authorized NFC lab work
- **Fully open-source**: Schematics and PCB files available; create your own via PCBWay! https://pcbway.com/g/87Pi52

## Current Features (v2.0)

### Core Scanning
- **Scan & Info**: Automatic card type detection (MIFARE Classic 1K/4K, NTAG213/215/216, Ultralight). Displays UID, card type, and capacity.
- **Demo Mode**: Show-friendly workflows for Tag Studio, Dump + Web, Badge Writer, and Puzzle Hunt without hiding the deeper research tools.

### Read Operations
- **UID Only**: Quick card UID capture
- **Read NDEF**: Extract and decode human-friendly URL/Text NDEF records from NTAG cards, with raw page view fallback.
- **Full MIFARE Dump**: Complete 1K or 4K block dump with automatic key discovery via 50-key dictionary attack. Outputs binary (`.mfd`) + text (`.txt`) files to SD card.
- **Full NTAG Dump**: All pages (45–231 depending on model). Outputs binary (`.bin`) + text (`.txt`) to SD card.

### Key Recovery & Attacks
- **Dictionary Attack / Key Audit**: Test 50 built-in MIFARE Classic keys plus optional SD keys from `KEYS.TXT` against all sectors. Displays per-sector cracked status and saves key-map artifacts.

### Cloning
- **Dump to SD**: Scan source card → full dump with key discovery → save to SD.
- **SD to Magic Card**: Select saved dump → place magic card → clone all blocks from dump → handles sector trailers and access bits.
- **Verify Clone**: Block-by-block comparison of cloned card against source dump. Reports exact mismatch count.

### Writing
- **Write NDEF URL**: Write `http://`, `https://`, `http://www.`, or `https://www.` URLs to NTAG cards.
- **Write NDEF Text**: Write language-tagged text to NTAG cards.
- **Write from SD**: Select a `.txt` preset; files beginning with `url:` write URI records, files beginning with `text:` write text records, and bare content writes text.

### SD Card Tools
- **Browse Files**: Navigate SD card directory (displays via scrolling list).
- **Hex View File**: Open any file → navigate with UP/DOWN buttons → view 16-byte rows with hex + ASCII columns.
- **Delete File**: Remove files from SD card.
- **Web Control Mode**: Start a no-auth Wi-Fi AP at `http://192.168.4.1` for full control: scan, dump, key-audit, write, clone, verify, preset edit, delete, preview, and download.
- **BLE Serial Mode**: Menu-launched no-auth Nordic UART Service as `CYPHER-PN532`, mirroring the USB command protocol for nearby lab control.

### Card Emulation (Emulate Tag)
- **NDEF from SD**: Emulate an ISO14443-4 / Type 4 NDEF tag serving `/ndef/*.txt`, `/NDEF_URL.TXT`, or `/NDEF_TXT.TXT` content. This is the iPhone-friendly path for event badge / handoff demos.
- **NTAG Dump**: Replay a previously saved `ntgNNN.bin` dump as an emulated tag.
- **UID Only**: Spoof a scanned UID with an empty NDEF body.
- ⚠️ **Limits**: MIFARE Classic emulation is **not possible** (the PN532 can't run Crypto1 auth as a target — `.mfd` dumps can't be replayed to a reader). Only ~3 UID bytes are controllable, and cheap antennas radiate weakly in target mode, so tap the phone directly (iPhone is most reliable).

### APDU Lab
- **Type4 NDEF Probe**: Read-only ISO7816/ISO14443-4 probe that selects the NFC Forum NDEF application, reads the CC file, selects the NDEF file, and shows NLEN plus the first bytes.
- **Select NDEF AID**: Sends only the NFC Forum NDEF AID select APDU and displays response length plus SW1/SW2 status words.
- **Scope**: This is for safe educational Type 4/NDEF testing. It does not include payment AID presets or generic payment-card processing.

### Supporting Features
- **Unique file naming**: Persistent `/COUNTER.TXT` on SD auto-increments filenames (`dmp001.mfd`, `dmp002.mfd`, `ntg001.bin`, etc.)
- **Scan log CSV**: Appends `uptime_ms,uid,type,action,filename` to `SCANLOG.CSV` for scans, NDEF reads, writes, web/USB/BLE operations, demo actions, and dumps.
- **JSON sidecars**: Dumps and key audits create `.json` metadata with schema version, device, firmware, UID, card type, capacity, files, and key summary.
- **USB serial control**: `tools/cypher_pn532_cli.py` exposes `help`, `status`, `scan`, `dump`, `key-audit`, `write-ndef`, `write-from-sd`, `clone`, `verify`, `files`, `download`, `upload-preset`, `delete`, and `emulate-ndef`.
- **Progress feedback**: Long operations (dumps, dictionary attacks) show real-time progress bars
- **Card type auto-detection**: Identifies MIFARE Classic vs NTAG variants via UID length and capability container inspection

## Menu Structure

The ESP32-C3 and Cypherbox Mini builds include `Web Control` and `BLE Serial`. The Cardputer port adds `Web Control`, `BLE Serial`, and `Return to Cypher OS`, making it a 12-item main menu.

```
MAIN MENU
├─ Scan & Info              → Auto-detect + display card info
├─ Demo Mode
│  ├─ Tag Studio            → Scan + decoded NDEF demo view
│  ├─ Dump + Web            → Dump to SD; Cardputer can launch web browser
│  ├─ Badge Writer          → Pick SD preset and write URL/Text NDEF
│  ├─ Puzzle Hunt           → Show clue:/puzzle: NDEF text cleanly
│  └─ Back
├─ Read Card
│  ├─ UID Only
│  ├─ Read NDEF             → Decoded URL/Text first, raw pages as fallback
│  ├─ Full Dump MIFARE      → Auto key discovery + save
│  ├─ Full Dump NTAG        → All pages + save
│  └─ Back
├─ Key Attack
│  ├─ Dictionary Attack     → 50 keys × all sectors
│  └─ Back
├─ Clone Card
│  ├─ Dump to SD            → Scan source + dump
│  ├─ SD to Magic Card      → Select file + clone
│  ├─ Verify Clone          → Block-by-block check
│  └─ Back
├─ Write Card
│  ├─ Write NDEF URL        → Select URL prefix + write
│  ├─ Write NDEF Text       → Select text + write
│  ├─ Write from SD         → `url:` / `text:` presets
│  └─ Back
├─ SD Card
│  ├─ Browse Files          → Navigate + view filenames
│  ├─ Hex View File         → Open + scroll hex+ASCII
│  ├─ Delete File           → Select + confirm delete
│  └─ Back
├─ Emulate Tag
│  ├─ NDEF from SD          → Serve SD NDEF preset to a phone (Type 4)
│  ├─ NTAG Dump             → Replay a saved ntgNNN.bin dump
│  ├─ UID Only              → Spoof a scanned UID, empty NDEF
│  └─ Back
├─ APDU Lab
│  ├─ Type4 NDEF Probe      → Safe read-only CC/NLEN probe
│  ├─ Select NDEF AID       → Show ISO7816 status words
│  └─ Back
├─ Web Control              → No-auth AP control surface
├─ BLE Serial               → No-auth Nordic UART command surface
└─ Return to Cypher OS      → Launcher return helper (Cardputer)
```

**Navigation:** UP/DOWN buttons scroll menu items. SELECT button chooses item or confirms action. "Back" item returns to previous menu.

## Building & Flashing

### M5Stack Cardputer ADV Port

This repo also includes an isolated Cardputer ADV port at
`CardputerPN532/CardputerPN532.ino`. It keeps the original ESP32-C3 sketch
unchanged and is intended for Cypher OS packaging.

Build it with Arduino CLI:

```bash
arduino-cli compile --profile adv CardputerPN532
```

For Cypher OS, the sibling launcher repo builds it as `cypher-pn532.bin` and
installs it from `/cypher-puter/apps`.

Cardputer EXT wiring:

| PN532 | Cardputer ADV EXT |
| --- | --- |
| VCC | pin 6 `5VOUT` |
| GND | pin 4 `GND` |
| SDA | pin 8 `G8 / I2C_SDA` |
| SCL | pin 10 `G9 / I2C_SCL` |

Leave PN532 `RESET`, `INT`, and `BUSY` unconnected. Do not use EXT pin 2
`5VIN` to power the module.

If the PN532 is missing or temporarily wedged, the Cardputer build now opens
the menu instead of blocking at boot. NFC actions show a retry prompt, recover
the EXT I2C bus, and return to the menu if the reader still does not answer.
If repeated retries fail after a frozen read, physically power-cycle the PN532
module or the Cardputer so the module loses 5V power.

Cardputer runtime files live under `/cypher-pn532/` on the SD card. Optional
NDEF presets can be placed at `/cypher-pn532/NDEF_URL.TXT` and
`/cypher-pn532/NDEF_TXT.TXT`; the port also falls back to root-level preset
files for compatibility with the original sketch. Type 4 emulation first looks
for selectable payloads in `/cypher-pn532/ndef/*.txt`, then falls back to those
preset files.

The Cardputer port also includes Web Control and BLE Serial modes. Choose `Web Control` from
the app menu, connect to SSID `CYPHER-PN532` with password `cypher532`, then
open `http://192.168.4.1`. The browser can run full workstation operations
against `/cypher-pn532/`: scan, dump, key-audit, write NDEF, write from SD,
clone, verify, emulate NDEF, edit presets, delete files, preview, and download.
The AP has no HTTP auth by design, so launch it only in a trusted lab setting.
`/api/files` returns `name`, `size`, `type`, `view_url`, and `download_url`;
`POST /api/op` runs the same foreground operations as the device UI and USB CLI.
Choose `BLE Serial` to advertise `CYPHER-PN532` over Nordic UART Service until
Back/Select exits the mode. BLE uses the same commands and JSON responses as USB,
has no pairing or app auth, and can run destructive write/clone/delete operations.

### Cypherbox Mini PN532 Port

This repo now includes an isolated Cypherbox Mini profile at
`CypherboxMiniPN532/CypherboxMiniPN532.ino`. It reuses the main NFC firmware
with the Cypherbox Mini pin map and leaves the original ESP32-C3 sketch intact.

Build it with Arduino CLI:

```bash
arduino-cli compile --profile cypherbox_mini CypherboxMiniPN532
```

Cypherbox Mini hardware contract:

| Function | GPIO |
| --- | --- |
| I2C SDA for OLED + PN532 | `8` |
| I2C SCL for OLED + PN532 | `9` |
| PN532 IRQ / RESET | not connected, code uses `-1` |
| SD SCK / MISO / MOSI / CS | `4` / `5` / `6` / `10` |
| Buttons UP / DOWN / SELECT | `1` / `2` / `3` |

Set the PN532 module DIP switches for I2C mode. Runtime files and optional
NDEF presets stay at the SD root, matching the ESP32-C3 firmware:
`/COUNTER.TXT`, `/SCANLOG.CSV`, `/NDEF_URL.TXT`, and `/NDEF_TXT.TXT`.
Type 4 emulation also checks `/ndef/*.txt` before falling back to root `.txt`
files and then the preset files.

### Requirements
- **Arduino IDE 2.x** (https://www.arduino.cc/en/software)
- **ESP32 Board Support** installed (via Arduino IDE → Boards Manager, search "esp32", install latest)
- **Required Libraries**:
  - `Adafruit_PN532` (via Arduino IDE → Library Manager)
  - `Adafruit_SSD1306` (via Arduino IDE → Library Manager)
  - `Adafruit_BusIO` (dependency for Adafruit libs)

### Build Steps

1. **Clone or download** this repository
2. **Install libraries** via Arduino IDE Library Manager (Sketch → Include Library → Manage Libraries)
3. **Select board**: Tools → Board → ESP32 Arduino → **XIAO_ESP32C3**
4. **Set upload speed**: Tools → Upload Speed → 115200
5. **Connect ESP32-C3** via USB cable to your computer
6. **Select port**: Tools → Port → (your USB port)
7. **Open** `cypher_pn532/cypher_pn532.ino` in Arduino IDE
8. **Compile**: Sketch → Verify
9. **Flash**: Sketch → Upload
10. **Monitor** (optional): Tools → Serial Monitor (set to 115200 baud)

Arduino CLI compile check:

```bash
arduino-cli compile --fqbn 'esp32:esp32:XIAO_ESP32C3:CDCOnBoot=cdc,PartitionScheme=huge_app' cypher_pn532
arduino-cli compile --profile cypherbox_mini CypherboxMiniPN532
```

On the current ESP32 Arduino core, the bare `esp32:esp32:XIAO_ESP32C3` CLI FQBN
can link-fail on `HWCDCSerial`; adding `CDCOnBoot=cdc` matches the serial-ready
configuration used for validation. NimBLE also requires the ESP32-C3/Cypherbox
builds to use `PartitionScheme=huge_app`.

## USB and BLE Serial Workstation Control

The firmware accepts newline-delimited USB serial commands at 115200 baud, and
the menu-launched `BLE Serial` mode exposes the same command language through
Nordic UART Service. Arguments use `key=value` tokens with percent-encoded
values, and responses are newline-delimited JSON. The supported operations are:

`HELP`, `STATUS`, `SCAN`, `DUMP`, `KEY_AUDIT`, `WRITE_NDEF`, `WRITE_FROM_SD`,
`CLONE`, `VERIFY`, `FILES`, `GET_FILE`, `PUT_PRESET`, `DELETE`, and
`EMULATE_NDEF`.

BLE advertises as `CYPHER-PN532` only while the device is in BLE Serial mode.
It is no-auth proximity lab access and mirrors USB exactly, including the hex
chunk stream used by `GET_FILE`.

Use the host helper:

```bash
python3 tools/cypher_pn532_cli.py --self-test
python3 tools/cypher_pn532_cli.py --port /dev/cu.usbmodem3101 help
python3 tools/cypher_pn532_cli.py --port /dev/cu.usbmodem3101 status
python3 tools/cypher_pn532_cli.py --port /dev/cu.usbmodem3101 scan
python3 tools/cypher_pn532_cli.py --port /dev/cu.usbmodem3101 write-ndef --type url --content https://example.com
python3 tools/cypher_pn532_cli.py --port /dev/cu.usbmodem3101 download SCANLOG.CSV -o SCANLOG.CSV
```

Install `pyserial` if needed:

```bash
python3 -m pip install pyserial
```

### On First Boot
- Device will initialize PN532 module and SD card
- Display will show "Cypher NFC v2.0" splash screen
- Main menu loads automatically

## SD Card File Formats

All dumps saved to SD card root directory with auto-incremented counters.

| File Type | Extension | Format | Notes |
|-----------|-----------|--------|-------|
| MIFARE Dump (Binary) | `.mfd` | Raw 1024 (1K) or 4096 (4K) bytes | Backup/restore format |
| MIFARE Dump (Text) | `.txt` | Hex sector headers + per-block hex/ASCII | Human-readable reference |
| NTAG Dump (Binary) | `.bin` | Raw pages (45–231 pages × 4 bytes) | Backup/restore format |
| NTAG Dump (Text) | `.txt` | Hex page headers + hex/ASCII | Human-readable reference |
| Metadata sidecar | `.json` | Device, firmware, UID, card type, files, key summary | Created for dumps/key audits |
| Key dictionary | `KEYS.TXT` | One 6-byte hex key per line | Optional extra MIFARE keys after built-ins |
| Scan Log | `.CSV` | `uptime_ms,uid,type,action,filename` | Field/demo activity log |
| Counter | `COUNTER.TXT` | Plain text number | Auto-managed; do not edit |

**Example filenames:**
- `dmp001.mfd` / `dmp001.txt` — First MIFARE dump (1K or 4K, files paired)
- `dmp002.mfd` / `dmp002.txt` — Second MIFARE dump
- `ntg001.bin` / `ntg001.txt` — First NTAG dump
- `dmp001.json` / `ntg001.json` — Metadata sidecar for a saved dump
- `KEYS.TXT` — Optional user key dictionary (`FFFFFFFFFFFF`, `A0:A1:A2:A3:A4:A5`, etc.)
- `SCANLOG.CSV` — Scan, write, dump, and demo activity
- `COUNTER.TXT` — Current counter value (managed automatically)

## Parts List

| Component                     | Description                                      |
|-------------------------------|--------------------------------------------------|
| **ESP32-C3 Super Mini**       | Microcontroller with Wi-Fi and Bluetooth support |
| **SSD1306 128x64 OLED Display** | .96-inch screen for displaying information      |
| **SD Card Module**            | Module for reading and writing SD cards         |
| **PN532 RFID/NFC Module**     | Module for RFID/NFC reading and writing         |
| **Push Buttons**              | 3 buttons for user interaction                   |
| **Resistors**                 | 10kΩ resistors (optional)         |
| **Breadboard**                | For prototyping connections                       |
| **Jumper Wires**              | For making connections between components        |
| **3V Power Supply**              | Suitable power source for the ESP32             |

## Parts used to make this device:
- **ESP32-C3 Super Mini**:
https://amzn.to/3XtgL9G

- **PN532 NFC/RFID Module**:
https://amzn.to/3XqYQjN

- **SSD1306 128x64 Screen**:
https://amzn.to/3TqELJe

- **SD Card Module**:
https://amzn.to/3zsvJot

- **Tactile Buttons**:
https://amzn.to/4gripRD

## Hardware & Wiring

### PN532 NFC/RFID Module (I2C)
- **SDA**: GPIO 8
- **SCL**: GPIO 9
- **IRQ**: GPIO 2 (optional, handled in code)
- **RESET**: GPIO 3 (hardware wire, do not change)

### SD Card Module (SPI)
- **CS**: GPIO 10
- **MOSI**: GPIO 6
- **MISO**: GPIO 5
- **SCK**: GPIO 4

### User Interface Buttons
- **UP** (BUTTON_UP): GPIO 7 ⚠️ **(Hardware Change v2.0: was GPIO 3, conflicted with PN532_RESET)**
- **DOWN** (BUTTON_DOWN): GPIO 1
- **SELECT** (BUTTON_SELECT): GPIO 0 ⚠️ **(Hardware Change v2.0: was GPIO 2, conflicted with PN532_IRQ)**

### SSD1306 OLED Display (I2C)
- Shares I2C bus with PN532 (GPIO 8/9)
- Default address: 0x3C

### ⚠️ Critical Hardware Rewiring Required

**If upgrading from v1.x, you MUST physically rewire the buttons:**
1. Disconnect BUTTON_UP from GPIO 3 and reconnect to GPIO 7
2. Disconnect BUTTON_SELECT from GPIO 2 and reconnect to GPIO 0
3. Verify all connections before powering on

Failure to rewire will cause conflicts with the PN532 hardware pins.

## Technical Features (Under the Hood)

### Card Type Auto-Detection
- **MIFARE Classic 1K vs 4K**: Detected via UID length (4 bytes) + block 128 probing
- **NTAG vs Ultralight**: Detected via capability container byte at page 3 (CC field)
- **Type 4 NDEF**: Cards that do not answer MIFARE/NTAG-specific reads are probed with a read-only NFC Forum NDEF AID SELECT APDU.
- **Full type support**: MIFARE Classic, NTAG213/215/216, MIFARE Ultralight, ISO14443-4

### Dictionary Attack Algorithm
- Embeds 50 most common MIFARE keys (factory defaults, transport, vendor keys)
- Merges optional SD keys from `KEYS.TXT` after the built-in dictionary
- Tests Key A + Key B per sector
- Shows live per-sector progress; completes ~30 seconds for typical 1K card
- Caches discovered keys for efficient block reading

### Magic Card Detection & Cloning
- Non-destructive magic card test: attempts backdoor auth on block 0
- Supports Gen1a magic cards (UID-writable via InDataExchange)
- Block 0 clone via raw `0xA0` write command; sectors 1+ via standard auth + write
- Sector trailer (access bits) handled separately to avoid locking card

### Full Dumps
- **MIFARE**: All 64 (1K) or 256 (4K) blocks captured with key recovery
- **NTAG**: All 45–231 pages depending on model; pages 42+ read via raw InDataExchange (0x30 command)
- Both output binary + human-readable text formats

### State Machine Architecture
- `AppState` enum replacing ad-hoc boolean flags
- Each menu context owns its own item list and navigation state
- Clean dispatch pattern from menu selection to operation execution

### Memory-Efficient Design
- ~28 KB total RAM footprint (safe on ESP32-C3 with 400 KB)
- Global dump buffers (MifareDump, NTAGDump) reused across multiple operations
- Serial I/O async-friendly (no infinite polling loops)

## Compatibility & Limitations

- **Supported cards**: ISO14443A (Type 2 & Type 4) — MIFARE, NTAG, Ultralight
- **Not supported without different hardware**: RF sniffing, MIFARE Classic target emulation, ISO15693/ICODE, EM4100/125 kHz LF, Mfkey32/Mfkey64-style workflows that require sniffing, HF ISO14443B, FeliCa, and other non-PN532-supported standards
- **APDU Lab scope**: Read-only Type 4/NDEF APDUs only; no payment AID presets. Apple's `NFCPaymentTagReaderSession` is a separate, EU-gated iOS API and does not change what the PN532 firmware can do directly.
- **Magic card cloning**: Only Gen1a backdoor method; Gen2/CUID requires alternate approach (not implemented)
- **NDEF writing**: URL/Text only; nested NDEF messages not supported
- **Encryption**: MIFARE DES/3DES authentication is symmetric (no real encryption); key recovery is the point

## Troubleshooting

| Issue | Solution |
|-------|----------|
| **OLED display is blank** | Check I2C wiring (GPIO 8/9); verify address is 0x3C via Arduino IDE I2C scanner |
| **PN532 not detected** | Check I2C wiring; ensure GPIO 8/9 are not in use by other hardware; try power-cycling |
| **SD card not found** | Check SPI wiring (GPIO 4/5/6/10); ensure card is FAT32 formatted; try reinserting card |
| **Button not responding** | Check GPIO wiring; verify pullup resistors (10kΩ recommended on all three buttons) |
| **Dictionary attack fails** | Ensure MIFARE Classic card (not Plus/Pro/EV1); some vendor keys may be missing from 50-key list |
| **Watchdog reset during dump** | Normal on heavily protected sectors; card is still safe; try reducing SPI clock (in code) |
| **Cannot detect card type** | Card may be unsupported (HF ISO14443B, ISO15693, etc.); or PN532 antenna needs adjustment |

## Development and Updates

v2.0 represents a complete architectural rewrite from the proof-of-concept v1.0. All code is open-source; contributions welcome. See `CLAUDE.md` for development notes, architecture details, and known technical debt.

Key improvements in v2.0:
- Fixed 8 critical bugs from v1.0 (pin conflicts, infinite loops, broken file naming, uninitialized variables)
- Removed ~300 lines of dead code
- Implemented proper state machine for menu navigation
- Added card type auto-detection with blockage-friendly key discovery
- Full MIFARE/NTAG dump with binary + text export
- Magic card cloning with Gen1a backdoor support
- NDEF URL/text writing with TLV format
- Hex file viewer for SD card inspection
- Progress feedback for all long-running operations

<img src="img/img.jpg" alt="RFID/NFC Module" width="500" height="600">
<img src="img/img2.jpg" alt="RFID/NFC Module" width="500" height="600">
<img src="img/img3.jpg" alt="RFID/NFC Module" width="500" height="600">
<img src="img/img7.JPG" alt="RFID/NFC Module" width="500" height="600">
<img src="img/img8.JPG" alt="RFID/NFC Module" width="500" height="600">
<img src="img/img5.JPG" alt="RFID/NFC Module" width="500" height="600">
