# CYPHER NFC
## A tiny ESP32 device that interacts with RFID + NFC devices using the PN532 chip.

This project leverages the ESP32-C3 Super Mini microcontroller, featuring a robust setup with an SSD1306 128x64 OLED screen, an SD card module, three input buttons, and a PN532 RFID/NFC module. 

## Current Features

- **RFID/NFC Functionality**: Read and potentially write data using the PN532 module.
- **SD Card Operations**: Seamlessly read, load, and delete files on the SD card.
- **User Interface**: Navigate options with three buttons connected to the OLED screen.

## Future Features
- **Save scans to SD card** : After reading RFID/NFC, save data to SD Card
- **Write from SD card** : Enter write menu, select SD Card, & choose which data to write. 


More updates will be added soon!

## Wiring

### SD Card Module

- **CS**: Pin 10
- **MOSI**: Pin 6
- **MISO**: Pin 5
- **SCK**: Pin 4

### Buttons

- **Up**: Pin 3
- **Down**: Pin 1
- **Select**: Pin 2

## Development and Updates

The code is under active development, with regular updates planned to enhance functionality and stability. Keep an eye on this repository for the latest improvements and feature additions.

<img src="img/img.jpg" alt="RFID/NFC Module" width="500" height="600">
<img src="img/img2.jpg" alt="RFID/NFC Module" width="500" height="600">
<img src="img/img3.jpg" alt="RFID/NFC Module" width="500" height="600">
