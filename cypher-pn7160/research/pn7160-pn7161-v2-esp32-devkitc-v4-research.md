# PN7160/PN7161 V2 ESP32 DevKitC V4 Research Brief

Research snapshot: 2026-05-27

## Bottom Line

PN7160/PN7161 is a serious next-step NFC controller for ESP32 DevKitC V4 work, especially if the goal is to move beyond the PN532's older command model and limited protocol coverage. It is not a drop-in replacement for `Adafruit_PN532`: it is an NCI 2.0 controller with integrated firmware, an event/discovery model, IRQ-driven host communication, and different software expectations.

For this repo, the right move is a standalone `cypher-pn7160/` lab sketch on a normal ESP32 DevKitC V4. Prove power, I2C, discovery, NDEF, ISO15693, MIFARE Classic, and Type 4 card-emulation behavior there before merging anything into the current PN532 field-workstation firmware.

PN7161 is effectively the PN7160 family plus Apple Enhanced Contactless Polling (ECP). Apple-related capability is gated by formal authorization and should not be described as DIY Apple Wallet, Apple Pay, or general payment support.

## What The Chip Adds

NXP positions PN7160 as a plug-and-play NFC controller with integrated firmware, NCI 2.0, Android/Linux/Windows support, and RTOS/no-OS support. The current NXP product page says it supports all NFC Forum modes and is available as either I2C or SPI silicon variants. NXP's datasheet notes PN7161 adds Apple ECP support, but ECP is available after formal authorization only.

Practical ESP32 possibilities:

| Capability | ESP32 DevKitC V4 + PN7160/PN7161 outlook |
|---|---|
| NFC-A polling | Yes. Good replacement for basic UID/tag detect. |
| NFC-B polling | Yes. A meaningful step beyond this repo's PN532 assumptions. |
| NFC-F/FeliCa polling | Yes, hardware family supports it. Library support needs validation. |
| NFC-V/ISO15693 | Yes. Big upgrade over stock PN532 behavior in this repo. |
| NFC Forum Type 1-5 tags | Yes at the controller capability level; Type 5 means ISO15693-backed NFC Forum tags. |
| MIFARE Classic read/write | Possible with keys. Electronic Cats examples include MIFARE Classic block read/write flows. |
| NDEF read/write | Yes. Electronic Cats and NXP examples cover NDEF read/write, with caveats by tag type and formatting state. |
| Raw ISO-DEP/APDU experiments | Yes. PN7160 uses NCI and can expose raw interactions once a target is discovered. |
| Type 4 NDEF card emulation | Yes. This is one of the main reasons to test PN716x. NXP documents Type 4 card-emulation scenarios. |
| Phone-readable "tap my device" demos | Yes, best framed as Type 4 NDEF card emulation. |
| P2P | Technically in the family, but low priority. NXP says P2P has been disappearing from modern phone stacks. |
| Apple ECP | PN7161 only, and gated. Do not frame as DIY Apple Wallet/Apple Pay support. |

## Do Not Overclaim

| Claim | Reality |
|---|---|
| "This does 125 kHz RFID" | No. PN7160/PN7161 is 13.56 MHz HF/NFC, not LF RFID. |
| "This can sniff RF traffic like Proxmark/PN532Killer-class tools" | No. It is an NFC controller, not a general RF sniffer. |
| "This makes the device EMVCo/payment compliant" | No. NXP explicitly says PN7160/PN7161 are not intended for EMVCo 3.x compliance and points to PN5180, PN5190, or PN7220 for those applications. |
| "PN7161 means Apple Wallet access" | No. PN7161 adds Apple ECP support, but ECP requires formal authorization. It does not create an open Apple Pay or Wallet reader/writer path. |
| "It can fully emulate a MIFARE Classic card" | Do not claim this for v1. PN7160 card mode is strongest for NFC Forum Type 4 / ISO-DEP style emulation. MIFARE Classic target behavior and Crypto1-authenticated card emulation should remain out of scope unless proven with real hardware and docs. |
| "It replaces Adafruit_PN532 calls one-for-one" | No. PN716x uses NCI 2.0 and different libraries/state machines. Existing PN532 code must be adapted through a driver layer. |

## DevKitC V4 Wiring

Default assumption: I2C PN7160/PN7161 module. ELECHOUSE's current PN7160 page lists an I2C module with `VDD` at 1.8 V or 3.3 V, `VANT` at 5 V, an onboard antenna, and ESP32 guide support.

Recommended normal ESP32 DevKitC V4 wiring:

| PN7160/PN7161 I2C | ESP32 DevKitC V4 | Notes |
|---|---:|---|
| `SDA` | `GPIO21` | Default ESP32 I2C data pin |
| `SCL` | `GPIO22` | Default ESP32 I2C clock pin |
| `IRQ` | `GPIO14` | Interrupt from PN716x to host |
| `VEN` | `GPIO13` | Enable/reset control |
| `DWL` | `GPIO19` or unconnected | Optional download/firmware mode control |
| `VDD` | `3V3` | Logic/interface supply |
| `VANT` | `5V` / `VIN` | Antenna/RF supply on common ELECHOUSE modules |
| `GND` | `GND` | Common ground |

Design cautions:

- Do not leave `VANT` out of the wiring plan. The ESP32 signal pins are not the whole power contract.
- Keep first-pass control pins away from common ESP32 boot strapping pins such as GPIO0, GPIO2, GPIO4, GPIO5, GPIO12, and GPIO15.
- Confirm whether the purchased board is I2C or SPI. PN7160/PN7161 bus type is not a DIP-switch setting like many PN532 modules; it is tied to the module/silicon variant.
- The common I2C address is `0x28`, but NXP hardware docs and ELECHOUSE address notes should be checked if a scan does not show it.
- If `VDD_UP`/antenna supply differs from the expected module design, NXP's hardware design guide warns that PMU/TXLDO settings can affect whether the RF field starts.

## PN7161 MINI SPI Note

ELECHOUSE documents a PN7161 MINI V1 SPI variant. Its ESP32 VSPI reference wiring is `SCK=GPIO18`, `MISO=GPIO19`, `MOSI=GPIO23`, `NSS=GPIO5`, `IRQ=GPIO14`, `VEN=GPIO13`, `VDD=3.3V`, `VANT=5V`, and `GND=GND`.

For DevKitC V4, SPI is easy electrically, but I2C is still the cleaner first target if your module is I2C. If you do have the SPI MINI, move `DWL` off GPIO19 because GPIO19 is also VSPI MISO.

## PN532 vs PN7160/PN7161

| Area | Current PN532 firmware | PN7160/PN7161 DevKitC direction |
|---|---|---|
| Host API | `Adafruit_PN532` style direct commands | NCI 2.0 controller lifecycle: connect, configure, discover, activate, exchange |
| Transport | This repo uses I2C for PN532 | I2C first on DevKitC `GPIO21/22`; SPI only for SPI module variants |
| IRQ/reset | PN532 IRQ=2, RESET=3 in the ESP32-C3 build | PN716x DevKitC plan uses IRQ=14 and VEN=13 |
| Protocol coverage | NFC-A/NTAG/MIFARE focus; Type 4 probe; no ISO15693 | NFC-A/B/F/V, Type 1-5, MIFARE, ISO15693, Type 4 card emulation |
| NDEF write | Implemented for NTAG-style Type 2 in current firmware | NDEF can be handled through NCI libraries across more tag families, depending on library support |
| Card emulation | Current PN532 Type 4 NDEF path is fragile and hardware-limited | PN7160 has documented card-emulation scenarios and better fit for phone-readable Type 4 NDEF |
| Existing workstation commands | `SCAN`, `DUMP`, `WRITE_NDEF`, `EMULATE_NDEF`, APDU lab | Keep command names later, but start with an isolated PN716x lab command surface |

## Software Paths

### Fastest Arduino Proof

Use the Electronic Cats PN7150/PN7160 Arduino library first. As of this research pass, the repository advertises version `3.1.1`, ESP32 compatibility, and a PN7160 constructor path:

```cpp
#include <Electroniccats_PN7150.h>

#define PN7160_IRQ 14
#define PN7160_VEN 13
#define PN7160_ADDR 0x28

Electroniccats_PN7150 nfc(PN7160_IRQ, PN7160_VEN, PN7160_ADDR, PN7160);
```

Recommended example order:

1. `DetectTags` - proves power, I2C, IRQ, VEN, discovery, and basic protocol reporting.
2. `NDEFReadMessage` - proves NDEF extraction from known-good NTAG/phone-written tags.
3. `NDEFSendMessage` - proves Type 4 card-emulation style phone-readable output.
4. `MifareClassic_read_block` - proves MIFARE Classic auth/read with a known default-key card.
5. `ISO15693_read_block` - proves the new Type 5 / NFC-V value over PN532.

Library caveat: several examples still name `PN7150` in variable names/comments even when the constructor supports `PN7160`. Treat the constructor chip model, actual module wiring, and serial output as the source of truth.

### ESPHome Proof

ESPHome has a `pn7160` component with I2C and SPI variants. It supports tag reading/writing and optional tag emulation via `emulation_message`, with actions for polling/emulation on/off and NDEF operations.

This is useful for a quick Home Assistant or "does the module behave" test. It is not the best fit for Cypher's standalone field-workstation firmware because the repo already has a custom Arduino menu/SD/USB/BLE/Web command surface.

### Deeper NXP Path

For a serious ESP-IDF or custom driver path, start from NXP's NCI 2.0 examples and user manual concepts rather than from PN532 code. NXP's MCUXpresso guide describes:

- `RWandCE`: reader mode plus card emulation with NDEF.
- `RW`: raw communication with ISO14443-3A, ISO14443-4, ISO15693, and MIFARE cards.
- `P2P`: NDEF exchange with NFC P2P devices, but this should be low priority for modern phone compatibility.

NXP's card-emulation note is especially relevant: it separates card emulation hosted by the device host from card emulation over the NFCC, and shows that Type 4 NDEF behavior needs a real state machine rather than the older PN532 target-mode assumptions.

## Cypher PN7160 Lab Roadmap

Phase 0: Keep current PN532 firmware untouched.

- Do not swap libraries inside `cypher_pn532/cypher_pn532.ino` first.
- Add no PN532 workstation command until hardware proof exists.
- Treat the PN716x module as a new controller family.

Phase 1: Standalone DevKitC V4 sketch.

- Use `cypher-pn7160/cypher-pn7160.ino`.
- Use DevKitC V4 pins: `SDA=21`, `SCL=22`, `IRQ=14`, `VEN=13`, optional `DWL=19`.
- Start at 115200 serial even if upstream examples use 9600.
- Print structured probe results: chip init, firmware version if exposed, discovery mode, protocol, technology, UID/NFCID, and NDEF summary.

Phase 2: Feature proof matrix.

| Test | Why it matters |
|---|---|
| NTAG213 scan/read | Baseline parity with current Type 2 demos |
| NTAG213 NDEF write/readback | Replaces current `WRITE_NDEF` confidence |
| MIFARE Classic read block with known key | Parity with current dump/key-audit direction |
| ISO15693 read block | New PN716x-only win |
| Android NDEF read from PN716x emulation | Proves phone-readable Type 4 card emulation |
| iPhone NDEF read from PN716x emulation | Proves the demo that PN532 target mode struggled with |
| Raw ISO-DEP select/read APDU | Validates APDU Lab upgrade path |

Phase 3: Driver abstraction.

- Add an NFC backend boundary only after the standalone sketch proves value.
- Keep existing command response contracts as newline-delimited JSON.
- Route `SCAN` to a common result shape with backend-specific fields.
- Keep `operationBusy` style single-owner behavior so USB/BLE/Web/menu flows cannot collide.

Future interface candidates:

| Candidate | Purpose | Notes |
|---|---|---|
| `PN716X_SCAN` | Explicit lab command during bring-up | Avoids changing existing `SCAN` behavior too early |
| Type 5 / ISO15693 read/write | Unlocks PN716x-specific value | Should be separate from NTAG/MIFARE flows |
| Type 4 card-emulation payload picker | Better `EMULATE_NDEF` path | Reuse SD payload ideas from current firmware |
| PN716x driver layer | Long-term coexistence with PN532 | Lets one firmware family support both controllers |

## First Bench Checklist

1. Identify the exact module variant: PN7160 I2C, PN7161 I2C, or PN7161 MINI SPI.
2. Power the module with both `VDD=3V3` and `VANT=5V`, plus common ground.
3. Wire I2C on DevKitC `GPIO21/22`, then wire `IRQ=14` and `VEN=13`.
4. Run a simple I2C scan and look for `0x28`.
5. Run Electronic Cats `DetectTags` with the constructor set to `PN7160`.
6. Test one known NTAG213, one known MIFARE Classic card with default key, and one ISO15693 tag.
7. Run `NDEFSendMessage` and scan the module with Android and iPhone.
8. Record results in this folder before attempting repo integration.

## Source Links

- [NXP PN7160 product page](https://www.nxp.com/products/PN7160)
- [PN7160/PN7161 data sheet](https://www.nxp.com/docs/en/data-sheet/PN7160_PN7161.pdf)
- [NXP PN7160 hardware design guide](https://www.nxp.com/docs/en/application-note/AN12988.pdf)
- [NXP PN7160 card emulation application note](https://www.nxp.com/docs/en/application-note/AN13861.pdf)
- [NXP NCI 2.0 MCUXpresso examples guide](https://www.nxp.com/docs/en/application-note/AN13288.pdf)
- [ELECHOUSE PN7160 I2C module page](https://www.elechouse.com/product/pn7160-nfc-rfid-module/)
- [ELECHOUSE PN7161 I2C and MINI SPI docs](https://www.elechouse.com/docs/pn7161/)
- [Electronic Cats PN7150/PN7160 Arduino library](https://github.com/ElectronicCats/ElectronicCats-PN7150)
- [ESPHome PN7160 component docs](https://esphome.io/components/pn7160/)
