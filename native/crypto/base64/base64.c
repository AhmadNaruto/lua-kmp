#include "base64.h"
#include <stdlib.h>
#include <stdint.h>

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char *base64_encode(const unsigned char *src, size_t len, size_t *out_len) {
    if (src == NULL) return NULL;

    size_t olen = 4 * ((len + 2) / 3);
    char *out = (char *)malloc(olen + 1);
    if (out == NULL) return NULL;

    size_t i = 0, j = 0;
    while (i < len) {
        uint32_t octet_a = i < len ? src[i++] : 0;
        uint32_t octet_b = i < len ? src[i++] : 0;
        uint32_t octet_c = i < len ? src[i++] : 0;

        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;

        out[j++] = b64_table[(triple >> 18) & 0x3F];
        out[j++] = b64_table[(triple >> 12) & 0x3F];
        out[j++] = (i > len + 1) ? '=' : b64_table[(triple >> 6) & 0x3F];
        out[j++] = (i > len) ? '=' : b64_table[triple & 0x3F];
    }

    out[olen] = '\0';
    if (out_len) *out_len = olen;
    return out;
}

unsigned char *base64_decode(const char *src, size_t len, size_t *out_len) {
    if (src == NULL) return NULL;

    /* Build inverted lookup table on stack */
    int8_t dtable[256];
    for (int k = 0; k < 256; k++) dtable[k] = -1;
    for (int k = 0; k < 64; k++) dtable[(unsigned char)b64_table[k]] = (int8_t)k;

    /* Count valid chars */
    size_t valid_len = 0;
    for (size_t k = 0; k < len; k++) {
        unsigned char c = (unsigned char)src[k];
        if (dtable[c] >= 0 || c == '=') valid_len++;
    }

    if (valid_len % 4 != 0) return NULL;

    size_t max_olen = (valid_len / 4) * 3;
    unsigned char *out = (unsigned char *)malloc(max_olen + 1);
    if (out == NULL) return NULL;

    size_t i = 0, j = 0;
    while (i < len) {
        int8_t sextets[4] = {-1, -1, -1, -1};
        int s_idx = 0;
        while (i < len && s_idx < 4) {
            unsigned char c = (unsigned char)src[i++];
            if (c == '=') {
                sextets[s_idx++] = -2; /* padding marker */
            } else if (dtable[c] >= 0) {
                sextets[s_idx++] = dtable[c];
            }
        }
        if (s_idx < 4) break;

        uint32_t triple = 0;
        if (sextets[0] >= 0) triple |= ((uint32_t)sextets[0]) << 18;
        if (sextets[1] >= 0) triple |= ((uint32_t)sextets[1]) << 12;
        if (sextets[2] >= 0) triple |= ((uint32_t)sextets[2]) << 6;
        if (sextets[3] >= 0) triple |= ((uint32_t)sextets[3]);

        if (sextets[0] >= 0 && sextets[1] >= 0) out[j++] = (triple >> 16) & 0xFF;
        if (sextets[2] >= 0) out[j++] = (triple >> 8) & 0xFF;
        if (sextets[3] >= 0) out[j++] = triple & 0xFF;
    }

    out[j] = '\0';
    if (out_len) *out_len = j;
    return out;
}
