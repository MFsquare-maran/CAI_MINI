#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
 

// ============================================================
//  SD-Karte
// ============================================================



bool InitSD(uint8_t sd_clk, uint8_t sd_miso,uint8_t sd_mosi,uint8_t sd_cs);