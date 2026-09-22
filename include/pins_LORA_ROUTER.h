#pragma once

#if HW_VERSION == 1

    #define BME_680_SCL 6
    #define BME_680_SDA 5


    #define LED_BLUE 2
    #define LED_ORANGE 21

    #define BATTERY_VOLTAGE 1


    #define SD_CLK 13
    #define SD_MISO 12
    #define SD_MOSI 11
    #define SD_CS 44




    #define LORA_DIO1   39
    #define LORA_RESET  42
    #define LORA_BUSY   40

    #define LORA_SCK     7
    #define LORA_MISO    8
    #define LORA_MOSI    9
    #define LORA_NSS     41


#elif HW_VERSION == 2

    #define BME_680_SCL 6
    #define BME_680_SDA 5  

    #define LED_BLUE 42
    #define LED_ORANGE 21


    #define BATTERY_VOLTAGE 1
    #define BATTERY_CHARGING 38

    #define SD_CLK 13
    #define SD_MISO 12
    #define SD_MOSI 11
    #define SD_CS 44

    #define LORA_ENABLE 41


    #define LORA_DIO1   39
    #define LORA_RESET  4
    #define LORA_BUSY   3

    #define LORA_SCK     7
    #define LORA_MISO    8
    #define LORA_MOSI    9
    #define LORA_NSS     40

    #define ON_BUTTON 2

#endif
