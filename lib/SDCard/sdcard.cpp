#include "sdcard.h"

// ============================================================
//  SD-Karte
// ============================================================

bool InitSD(uint8_t sd_clk, uint8_t sd_miso,uint8_t sd_mosi,uint8_t sd_cs) {
    SPI.begin(sd_clk, sd_miso, sd_mosi, sd_cs);
    if (!SD.begin(sd_cs)) {
        Serial.println("❌ Fehler: SD-Karte konnte nicht initialisiert werden!");
        return false; 
    }
    Serial.println("✅ SD-Karte erfolgreich initialisiert!");
    return true;
}