// ============================================================
//  aes_key.h
// ============================================================

#ifndef AES_KEY_H
#define AES_KEY_H


//#define LORA_ENCRYPTION_ENABLED

#define AES_KEY_LENGTH 16

//Eigenen Key einfügen
static const uint8_t AES_KEY[AES_KEY_LENGTH] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};


#endif // AES_KEY_H