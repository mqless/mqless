#ifndef HMAC_SHA256_H_INCLUDED
#define HMAC_SHA256_H_INCLUDED

#include "sha256.h"

#define BLOCK_SIZE 64

void hmac_sha256(const BYTE* key, size_t key_len, const BYTE* data, size_t data_len, BYTE* output) {
    BYTE temp_key[SHA256_DIGEST_SIZE];
    BYTE o_key_pad[BLOCK_SIZE];
    BYTE i_key_pad[BLOCK_SIZE];
    SHA256_CTX ctx;

    if (key_len > BLOCK_SIZE) {
        sha256_init (&ctx);
        sha256_update (&ctx, key, key_len);
        sha256_final (&ctx, temp_key);

        key = temp_key;
        key_len = SHA256_DIGEST_SIZE;
    }

    for (size_t i = 0; i < key_len; i++) {
        o_key_pad[i] = key[i] ^ 0x5c;
        i_key_pad[i] = key[i] ^ 0x36;
    }

    for (size_t i = key_len; i < BLOCK_SIZE; i++) {
        o_key_pad[i] = 0 ^ 0x5c;
        i_key_pad[i] = 0 ^ 0x36;
    }

    sha256_init (&ctx);
    sha256_update (&ctx, i_key_pad, BLOCK_SIZE);
    sha256_update (&ctx, data, data_len);
    sha256_final (&ctx, output);

    sha256_init (&ctx);
    sha256_update (&ctx, o_key_pad, BLOCK_SIZE);
    sha256_update (&ctx, output, SHA256_DIGEST_SIZE);
    sha256_final (&ctx, output);
}

#endif