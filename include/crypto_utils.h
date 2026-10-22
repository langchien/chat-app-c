#ifndef CRYPTO_UTILS_H
#define CRYPTO_UTILS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* ============================================================
 * Cryptographic constants (Task 2.2)
 * SHA-256 and Salt definitions
 * ============================================================ */
#define SHA256_BLOCK_SIZE  64
#define SHA256_DIGEST_SIZE 32
#define SHA256_HEX_LEN     64   /* 64 hex characters */
#define SALT_BYTE_LEN      16   /* 16 random bytes   */
#define SALT_HEX_LEN       32   /* 32 hex characters */

/* SHA-256 context structure */
typedef struct {
    uint32_t state[8];
    uint64_t count;                       /* Total bytes hashed */
    uint8_t  buffer[SHA256_BLOCK_SIZE];   /* 64-byte input chunk */
} SHA256_CTX;

/* ---- Low-level SHA-256 streaming API ---- */
void sha256_init(SHA256_CTX *ctx);
void sha256_update(SHA256_CTX *ctx, const uint8_t *data, size_t len);
void sha256_final(SHA256_CTX *ctx, uint8_t digest[SHA256_DIGEST_SIZE]);

/* ---- High-level SHA-256 convenience functions ---- */
void sha256_hash_bytes(const uint8_t *data, size_t len, uint8_t digest[SHA256_DIGEST_SIZE]);
void sha256_hash_string(const char *str, char hex_out[SHA256_HEX_LEN + 1]);
void sha256_digest_to_hex(const uint8_t digest[SHA256_DIGEST_SIZE], char hex_out[SHA256_HEX_LEN + 1]);

/* ---- Password & Salt helpers (Task 2.2) ---- */
void generate_salt_hex(char salt_hex[SALT_HEX_LEN + 1]);
void hash_password(const char *password, const char *salt_hex, char hash_hex[SHA256_HEX_LEN + 1]);
bool verify_password(const char *password, const char *salt_hex, const char *expected_hash);

/* ---- File Checksum helper (Task 4.3) ---- */
int sha256_file(const char *filepath, char hex_out[SHA256_HEX_LEN + 1]);

/* ---- Base64 Encoding / Decoding helpers (Task 4.1) ---- */
size_t base64_encode(const unsigned char *src, size_t len, char *out, size_t out_size);
size_t base64_decode(const char *src, size_t len, unsigned char *out, size_t out_size);

#endif /* CRYPTO_UTILS_H */
