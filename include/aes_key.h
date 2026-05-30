// ============================================================
//  aes_key.h
// ============================================================

#ifndef AES_KEY_H
#define AES_KEY_H


#define LORA_ENCRYPTION_ENABLED


// ── aes_key.h ─────────────────────────────────────────────────
// Schlüssel muss 32 Bytes sein für ChaCha20-256
#define CHACHA_KEY_LENGTH 32
#define CHACHA_IV_LENGTH   8

static const uint8_t CHACHA_KEY[CHACHA_KEY_LENGTH] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};


#endif // AES_KEY_H