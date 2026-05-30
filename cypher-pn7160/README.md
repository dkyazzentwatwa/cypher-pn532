# Cypher PN7160 DevKitC V4 Lab

Standalone Arduino bring-up sketch for an I2C PN7160/PN7161 module on a normal ESP32 DevKitC V4. This is intentionally separate from the current PN532 firmware so scan/read behavior can be proven before any PN532 workstation commands are changed.

## Wiring

Power the board off before wiring.

| PN7160/PN7161 I2C | ESP32 DevKitC V4 | Notes |
|---|---:|---|
| `SDA` | `GPIO21` | Default ESP32 I2C data pin |
| `SCL` | `GPIO22` | Default ESP32 I2C clock pin |
| `IRQ` | `GPIO14` | PN716x interrupt into ESP32 |
| `VEN` | `GPIO13` | PN716x enable/reset control |
| `DWL` | unconnected for v1 | Use `GPIO19` later only if download mode is needed |
| `VDD` | `3V3` | Logic/interface supply |
| `VANT` | `5V` / `VIN` | Antenna/RF supply on common ELECHOUSE modules |
| `GND` | `GND` | Common ground |

The firmware tries the normal PN7160 I2C address `0x28` first, then the observed fallback address `0x7C`. If `I2C_SCAN` does not show either address, check `VANT=5V`, common ground, SDA/SCL order, and whether the module is actually an SPI variant.

## Install And Build

```sh
arduino-cli lib install "Electronic Cats PN7150@3.1.1"
arduino-cli compile --profile devkitc cypher-pn7160
```

The `devkitc` profile uses:

- `esp32:esp32:esp32:PartitionScheme=huge_app,UploadSpeed=460800`
- `Electronic Cats PN7150 (3.1.1)`
- Serial console at `115200`

## Flash And Monitor

Find the current DevKitC USB serial port:

```sh
arduino-cli board list
```

Classic ESP32 DevKitC boards usually appear as `/dev/cu.usbserial*`.

Upload:

```sh
arduino-cli upload --profile devkitc -p /dev/cu.usbserial-XXXX cypher-pn7160
```

Monitor:

```sh
arduino-cli monitor -p /dev/cu.usbserial-XXXX --config baudrate=115200
```

If upload fails at `460800`, retry by changing the profile upload speed to `115200` before changing code or pins.

## Serial Commands

Commands are newline-delimited. Responses are compact JSON lines.

| Command | Purpose |
|---|---|
| `HELP` | Print supported commands |
| `STATUS` | Print firmware, pin, and init status |
| `I2C_SCAN` | Scan I2C bus and report whether `0x28` or fallback `0x7C` is present |
| `SCAN timeout_ms=5000` | Detect one tag and print protocol/tech/UID |
| `READ_NDEF timeout_ms=5000` | Read NDEF from Type 1/2/3/4 or MIFARE tags |
| `T2T_READ block=5` | Read one Type 2 page/block with command `0x30` |
| `MFC_READ block=4 key=FFFFFFFFFFFF` | Authenticate and read one MIFARE Classic block |
| `ISO15693_READ block=8` | Read one ISO15693 / Type 5 block |
| `RELEASE wait_remove=1` | Wait for the active tag to leave, then restart discovery |
| `RESET` | Reinitialize PN7160 and restart discovery |

Example flow:

```text
I2C_SCAN
STATUS
SCAN timeout_ms=10000
READ_NDEF timeout_ms=10000
T2T_READ block=5
RELEASE wait_remove=1
MFC_READ block=4 key=FFFFFFFFFFFF
ISO15693_READ block=8
```

Tag commands keep the current tag active so you can chain basic reads without rebooting or re-presenting the card. A practical NTAG bring-up sequence is:

```text
SCAN timeout_ms=20000
READ_NDEF timeout_ms=20000
T2T_READ block=5
STATUS
RELEASE wait_remove=1
```

After `RELEASE wait_remove=1` reports `reset_ok:true`, present another tag and run `SCAN` again. If `RELEASE wait_remove=0` reports `recovery_failed`, remove the tag from the antenna and run `RELEASE wait_remove=1` or `RESET`.

## Validation Ladder

1. `arduino-cli compile --profile devkitc cypher-pn7160` passes.
2. `arduino-cli board list` shows a real `/dev/cu.usbserial*` DevKitC port.
3. Serial boot JSON reports `nfc_ready:true` and an `active_addr`, usually `0x28` or fallback `0x7C`.
4. `SCAN` works with an NTAG213 and prints UID/protocol/tech.
5. `READ_NDEF` reads a known phone-written or PN532-written NDEF tag.
6. `T2T_READ block=5` returns 4 bytes from a Type 2 tag.
7. `SCAN`, `READ_NDEF`, and `T2T_READ` can be run in sequence on the same presented tag without `nfc_not_ready` or `nfc_reset_failed`.
8. `RELEASE wait_remove=1` restarts discovery after the tag is removed.
9. `MFC_READ block=4 key=FFFFFFFFFFFF` reads a known default-key MIFARE Classic card.
10. `ISO15693_READ block=8` reads a Type 5/ISO15693 tag if one is available.

This v1 is read-focused and lab-safe. It does not implement cloning, SD dumps, web controls, BLE controls, payment flows, or MIFARE Classic card emulation.

## Research

The research brief for this hardware lives at:

- [`research/pn7160-pn7161-v2-esp32-devkitc-v4-research.md`](research/pn7160-pn7161-v2-esp32-devkitc-v4-research.md)
