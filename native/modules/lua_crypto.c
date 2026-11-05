#include "lua_crypto.h"
#include "base64.h"
#include "md5.h"
#include "sha256.h"
#include "aes.h"
#include "lauxlib.h"
#include <stdlib.h>
#include <string.h>

static int l_base64_encode(lua_State *L) {
    size_t len = 0;
    const char *src = luaL_checklstring(L, 1, &len);
    if (!src) {
        lua_pushnil(L);
        return 1;
    }

    size_t out_len = 0;
    char *encoded = base64_encode((const unsigned char *)src, len, &out_len);
    if (encoded) {
        lua_pushlstring(L, encoded, out_len);
        free(encoded);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int l_base64_decode(lua_State *L) {
    size_t len = 0;
    const char *src = luaL_checklstring(L, 1, &len);
    if (!src) {
        lua_pushnil(L);
        return 1;
    }

    size_t out_len = 0;
    unsigned char *decoded = base64_decode(src, len, &out_len);
    if (decoded) {
        lua_pushlstring(L, (const char *)decoded, out_len);
        free(decoded);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int l_md5(lua_State *L) {
    size_t len = 0;
    const char *src = luaL_checklstring(L, 1, &len);
    if (!src) {
        lua_pushnil(L);
        return 1;
    }

    char hex[33];
    md5_hex((const uint8_t *)src, len, hex);
    lua_pushlstring(L, hex, 32);
    return 1;
}

static int l_sha256(lua_State *L) {
    size_t len = 0;
    const char *src = luaL_checklstring(L, 1, &len);
    if (!src) {
        lua_pushnil(L);
        return 1;
    }

    char hex[65];
    sha256_hex((const uint8_t *)src, len, hex);
    lua_pushlstring(L, hex, 64);
    return 1;
}

/*
** aes_decrypt(data, key, iv)
** - data can be Base64-encoded string or raw binary ciphertext.
** - key must be 16 bytes (AES-128).
** - iv must be 16 bytes.
** - Uses AES-CBC with PKCS7 unpadding.
*/
static int l_aes_decrypt(lua_State *L) {
    size_t data_len = 0, key_len = 0, iv_len = 0;
    const char *raw_data = luaL_checklstring(L, 1, &data_len);
    const char *key = luaL_checklstring(L, 2, &key_len);
    const char *iv = luaL_checklstring(L, 3, &iv_len);

    if (!raw_data || !key || !iv || key_len != 16 || iv_len != 16) {
        lua_pushnil(L);
        return 1;
    }

    /* Try Base64 decode if length is multiple of 4 or doesn't match AES block size */
    size_t ct_len = 0;
    unsigned char *ct = NULL;

    unsigned char *b64_res = base64_decode(raw_data, data_len, &ct_len);
    if (b64_res && ct_len > 0 && ct_len % 16 == 0) {
        ct = b64_res;
    } else {
        if (b64_res) free(b64_res);
        if (data_len % 16 != 0) {
            /* Ciphertext must be multiple of 16 */
            lua_pushnil(L);
            return 1;
        }
        ct_len = data_len;
        ct = (unsigned char *)malloc(ct_len);
        if (!ct) {
            lua_pushnil(L);
            return 1;
        }
        memcpy(ct, raw_data, ct_len);
    }

    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, (const uint8_t *)key, (const uint8_t *)iv);
    AES_CBC_decrypt_buffer(&ctx, ct, (size_t)ct_len);

    /* PKCS7 Unpadding */
    size_t pad_len = ct[ct_len - 1];
    if (pad_len > 0 && pad_len <= 16 && pad_len <= ct_len) {
        int valid_padding = 1;
        for (size_t i = 0; i < pad_len; i++) {
            if (ct[ct_len - 1 - i] != pad_len) {
                valid_padding = 0;
                break;
            }
        }
        if (valid_padding) {
            ct_len -= pad_len;
        }
    }

    lua_pushlstring(L, (const char *)ct, ct_len);
    free(ct);
    return 1;
}

int luaopen_crypto(lua_State *L) {
    lua_pushcfunction(L, l_base64_encode);
    lua_setglobal(L, "base64_encode");

    lua_pushcfunction(L, l_base64_decode);
    lua_setglobal(L, "base64_decode");

    lua_pushcfunction(L, l_md5);
    lua_setglobal(L, "md5");

    lua_pushcfunction(L, l_sha256);
    lua_setglobal(L, "sha256");

    lua_pushcfunction(L, l_aes_decrypt);
    lua_setglobal(L, "aes_decrypt");

    return 0;
}
