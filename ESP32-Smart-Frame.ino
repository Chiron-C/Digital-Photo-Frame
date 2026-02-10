#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>

// Your specific pins
#define SD_CS 5
#define SD_SCK 14
#define SD_MISO 4
#define SD_MOSI 13

SPIClass sdSPI(HSPI);

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(100);
  delay(1000);
  
  Serial.println("\n\n--- SD CARD ISOLATION TEST ---");
  Serial.print("Initializing SPI on: ");
  Serial.print("SCK="); Serial.print(SD_SCK);
  Serial.print(", MISO="); Serial.print(SD_MISO);
  Serial.print(", MOSI="); Serial.print(SD_MOSI);
  Serial.print(", CS="); Serial.println(SD_CS);

  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  // Try to mount
  if (!SD.begin(SD_CS, sdSPI)) {
    Serial.println("❌ FAILURE: SD Card mount failed.");
    Serial.println("Possible causes:");
    Serial.println("1. Wiring is loose or swapped (MISO/MOSI).");
    Serial.println("2. SD Card is not FAT32.");
    Serial.println("3. SD Module needs 5V but got 3.3V.");
  } else {
    Serial.println("✅ SUCCESS: SD Card mounted!");
    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("Card Size: %llu MB\n", cardSize);
    
    File root = SD.open("/");
    File file = root.openNextFile();
    while(file){
        Serial.print("  FILE: ");
        Serial.println(file.name());
        file = root.openNextFile();
    }
  }
}

void loop() {}