#include "core.h"

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <string.h>

static void bytes_to_hex(const unsigned char *bytes, size_t len, char *out) {
    static const char hex_chars[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i * 2] = hex_chars[(bytes[i] >> 4) & 0xF];
        out[i * 2 + 1] = hex_chars[bytes[i] & 0xF];
    }
    out[len * 2] = '\0';
}

void core_compute_sha256_hex(const unsigned char *data, size_t len, char *hex_out) {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (ctx == NULL) {
        hex_out[0] = '\0';
        return;
    }
    unsigned int out_len = 0;
    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(ctx, data, len);
    EVP_DigestFinal_ex(ctx, digest, &out_len);
    EVP_MD_CTX_free(ctx);
    bytes_to_hex(digest, SHA256_DIGEST_LENGTH, hex_out);
}

void core_compute_hmac_sha256_hex(const unsigned char *data, size_t len, const char *key, char *hex_out) {
    if (key == NULL || key[0] == '\0') {
        hex_out[0] = '\0';
        return;
    }
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    HMAC(EVP_sha256(), key, (int)strlen(key), data, len, digest, &digest_len);
    bytes_to_hex(digest, digest_len, hex_out);
}
