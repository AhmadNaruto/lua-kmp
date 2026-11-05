#ifndef SHA256_H
#define SHA256_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
} SHA256_CTX;

void SHA256_Init(SHA256_CTX *ctx);
void SHA256_Update(SHA256_CTX *ctx, const uint8_t *data, size_t len);
void SHA256_Final(uint8_t hash[32], SHA256_CTX *ctx);

/* Computes 64-character lowercase hex SHA-256 string of input */
void sha256_hex(const uint8_t *input, size_t len, char hex_out[65]);

#ifdef __cplusplus
}
#endif

#endif /* SHA256_H */
