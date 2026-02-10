# ESP32 Smart Photo Frame & Weather Station 🖼️🌦️

A multitasking smart frame powered by an ESP32. Features 6 interactive animated faces, real-time weather, a digital clock, and a web portal for uploading photos.

## ✨ Features
* **6 Animated Faces:** Cat, Cyber, Mystic, Toxic, Iron, and Heart Eyes.
* **Smart Animations:** Eyes move randomly and blink without graphical glitches.
* **Weather Station:** Live temperature and conditions via Open-Meteo.
* **Digital Clock:** Auto-synced via WiFi (NTP).
* **Photo Gallery:** Displays images from the SD card.
* **Web Portal:** Upload photos and configure settings wirelessly.

## 🛠️ Hardware Required
* ESP32 Dev Module
* 2.8" ILI9341 TFT Display (SPI)
* XPT2046 Touch Controller
* Micro SD Card Module


## 📦 Installation
1.  **Install Libraries:**
    * `TFT_eSPI`
    * `lvgl`
    * `TJpg_Decoder`
    * `ArduinoJson`
2.  **Configure Display:**
    * Go to `Documents/Arduino/libraries/TFT_eSPI`.
    * Open `User_Setup.h`.
    * Replace its contents with the code provided in `TFT_eSPI_Setup.h` from this repository.
3.  **Include Font:**
    * Ensure `cifre125.c` is in the same folder as the `.ino` file.
4.  **Upload:**
    * Open `ESP32_Smart_Frame.ino` in Arduino IDE and upload.

