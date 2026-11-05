#ifndef BASE64_H
#define BASE64_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
** Encodes input data of length len into Base64 format.
** Returns a malloc'd null-terminated string, or NULL on allocation error.
** The caller is responsible for free()-ing the returned buffer.
** If out_len is not NULL, it will be populated with the length of the string.
*/
char *base64_encode(const unsigned char *src, size_t len, size_t *out_len);

/*
** Decodes a Base64-encoded null-terminated or length-bounded string.
** Returns a malloc'd buffer containing the decoded bytes, or NULL on error.
** The caller is responsible for free()-ing the returned buffer.
** out_len receives the number of bytes decoded.
*/
unsigned char *base64_decode(const char *src, size_t len, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* BASE64_H */
