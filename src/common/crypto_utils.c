/* ============================================================
 * src/common/crypto_utils.c
 * Cryptographic utility functions.
 *
 * Task 2.2: SHA-256 implementation, salt generation (16 bytes),
 * password hashing (SHA-256(password + salt)), and constant-time
 * password verification.
 * ============================================================ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../../include/crypto_utils.h"

/* ============================================================
 * SHA-256 Constants and Macros (FIPS 180-2 compliant)
 * ============================================================ */
static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define ROTR(x, n)   (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SIGMA0(x)    (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define SIGMA1(x)    (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define sigma0(x)    (ROTR(x, 7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define sigma1(x)    (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

static void sha256_transform(SHA256_CTX *ctx, const uint8_t data[64]) {
    uint32_t W[64];
    uint32_t a, b, c, d, e, f, g, h;
    int i;

    for (i = 0; i < 16; i++) {
        W[i] = ((uint32_t)data[i * 4] << 24) |
               ((uint32_t)data[i * 4 + 1] << 16) |
               ((uint32_t)data[i * 4 + 2] << 8) |
               ((uint32_t)data[i * 4 + 3]);
    }
    for (i = 16; i < 64; i++) {
        W[i] = sigma1(W[i - 2]) + W[i - 7] + sigma0(W[i - 15]) + W[i - 16];
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (i = 0; i < 64; i++) {
        uint32_t T1 = h + SIGMA1(e) + CH(e, f, g) + K[i] + W[i];
        uint32_t T2 = SIGMA0(a) + MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + T1;
        d = c;
        c = b;
        b = a;
        a = T1 + T2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

void sha256_init(SHA256_CTX *ctx) {
    if (!ctx) return;
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
    ctx->count = 0;
}

void sha256_update(SHA256_CTX *ctx, const uint8_t *data, size_t len) {
    if (!ctx || !data || len == 0) return;
    size_t buffer_idx = (size_t)(ctx->count % SHA256_BLOCK_SIZE);
    ctx->count += len;

    size_t space = SHA256_BLOCK_SIZE - buffer_idx;
    if (len >= space) {
        memcpy(ctx->buffer + buffer_idx, data, space);
        sha256_transform(ctx, ctx->buffer);
        data += space;
        len -= space;
        while (len >= SHA256_BLOCK_SIZE) {
            sha256_transform(ctx, data);
            data += SHA256_BLOCK_SIZE;
            len -= SHA256_BLOCK_SIZE;
        }
        buffer_idx = 0;
    }
    if (len > 0) {
        memcpy(ctx->buffer + buffer_idx, data, len);
    }
}

void sha256_final(SHA256_CTX *ctx, uint8_t digest[SHA256_DIGEST_SIZE]) {
    if (!ctx || !digest) return;
    uint64_t total_bits = ctx->count * 8;
    size_t buffer_idx = (size_t)(ctx->count % SHA256_BLOCK_SIZE);

    /* Append the 0x80 bit */
    ctx->buffer[buffer_idx++] = 0x80;

    if (buffer_idx > 56) {
        memset(ctx->buffer + buffer_idx, 0, SHA256_BLOCK_SIZE - buffer_idx);
        sha256_transform(ctx, ctx->buffer);
        buffer_idx = 0;
    }
    memset(ctx->buffer + buffer_idx, 0, 56 - buffer_idx);

    /* Append total bit count as big-endian 64-bit integer */
    for (int i = 7; i >= 0; i--) {
        ctx->buffer[56 + (7 - i)] = (uint8_t)((total_bits >> (i * 8)) & 0xFF);
    }
    sha256_transform(ctx, ctx->buffer);

    for (int i = 0; i < 8; i++) {
        digest[i * 4]     = (uint8_t)((ctx->state[i] >> 24) & 0xFF);
        digest[i * 4 + 1] = (uint8_t)((ctx->state[i] >> 16) & 0xFF);
        digest[i * 4 + 2] = (uint8_t)((ctx->state[i] >> 8) & 0xFF);
        digest[i * 4 + 3] = (uint8_t)(ctx->state[i] & 0xFF);
    }
}

void sha256_digest_to_hex(const uint8_t digest[SHA256_DIGEST_SIZE], char hex_out[SHA256_HEX_LEN + 1]) {
    if (!digest || !hex_out) return;
    static const char hex_digits[] = "0123456789abcdef";
    for (int i = 0; i < SHA256_DIGEST_SIZE; i++) {
        hex_out[i * 2]     = hex_digits[(digest[i] >> 4) & 0x0F];
        hex_out[i * 2 + 1] = hex_digits[digest[i] & 0x0F];
    }
    hex_out[SHA256_HEX_LEN] = '\0';
}

void sha256_hash_bytes(const uint8_t *data, size_t len, uint8_t digest[SHA256_DIGEST_SIZE]) {
    SHA256_CTX ctx;
    sha256_init(&ctx);
    if (data && len > 0) {
        sha256_update(&ctx, data, len);
    }
    sha256_final(&ctx, digest);
}

void sha256_hash_string(const char *str, char hex_out[SHA256_HEX_LEN + 1]) {
    if (!hex_out) return;
    uint8_t digest[SHA256_DIGEST_SIZE];
    size_t len = str ? strlen(str) : 0;
    sha256_hash_bytes((const uint8_t*)str, len, digest);
    sha256_digest_to_hex(digest, hex_out);
}

/* ============================================================
 * Salt generation & password hashing (Task 2.2)
 * ============================================================ */

void generate_salt_hex(char salt_hex[SALT_HEX_LEN + 1]) {
    if (!salt_hex) return;
    uint8_t raw_salt[SALT_BYTE_LEN];
    bool got_urandom = false;

    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        if (fread(raw_salt, 1, sizeof(raw_salt), f) == sizeof(raw_salt)) {
            got_urandom = true;
        }
        fclose(f);
    }

    if (!got_urandom) {
        /* Fallback pseudo-random for environments without /dev/urandom */
        static bool seeded = false;
        if (!seeded) {
            srand((unsigned int)(time(NULL) ^ (clock() << 16) ^ (uintptr_t)&f));
            seeded = true;
        }
        for (size_t i = 0; i < sizeof(raw_salt); i++) {
            raw_salt[i] = (uint8_t)(rand() & 0xFF);
        }
    }

    static const char hex_digits[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(raw_salt); i++) {
        salt_hex[i * 2]     = hex_digits[(raw_salt[i] >> 4) & 0x0F];
        salt_hex[i * 2 + 1] = hex_digits[raw_salt[i] & 0x0F];
    }
    salt_hex[SALT_HEX_LEN] = '\0';
}

void hash_password(const char *password, const char *salt_hex, char hash_hex[SHA256_HEX_LEN + 1]) {
    if (!hash_hex) return;
    SHA256_CTX ctx;
    uint8_t digest[SHA256_DIGEST_SIZE];
    sha256_init(&ctx);
    if (password) {
        sha256_update(&ctx, (const uint8_t*)password, strlen(password));
    }
    if (salt_hex) {
        sha256_update(&ctx, (const uint8_t*)salt_hex, strlen(salt_hex));
    }
    sha256_final(&ctx, digest);
    sha256_digest_to_hex(digest, hash_hex);
}

bool verify_password(const char *password, const char *salt_hex, const char *expected_hash) {
    if (!password || !salt_hex || !expected_hash) return false;
    char computed[SHA256_HEX_LEN + 1];
    hash_password(password, salt_hex, computed);

    /* Constant-time comparison to protect against timing attacks */
    size_t len1 = strlen(computed);
    size_t len2 = strlen(expected_hash);
    if (len1 != len2) return false;

    unsigned char diff = 0;
    for (size_t i = 0; i < len1; i++) {
        diff |= (unsigned char)(computed[i] ^ expected_hash[i]);
    }
    return (diff == 0);
}

/* ============================================================
 * File Checksum (SHA-256) – Task 4.3
 * ============================================================ */
int sha256_file(const char *filepath, char hex_out[SHA256_HEX_LEN + 1]) {
    if (!filepath || !hex_out) return -1;
    FILE *fp = fopen(filepath, "rb");
    if (!fp) return -1;

    SHA256_CTX ctx;
    sha256_init(&ctx);

    uint8_t buf[4096];
    size_t bytes_read;
    while ((bytes_read = fread(buf, 1, sizeof(buf), fp)) > 0) {
        sha256_update(&ctx, buf, bytes_read);
    }
    fclose(fp);

    uint8_t digest[SHA256_DIGEST_SIZE];
    sha256_final(&ctx, digest);
    sha256_digest_to_hex(digest, hex_out);
    return 0;
}

/* ============================================================
 * Base64 Encoding & Decoding – Task 4.1
 * ============================================================ */
static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t base64_encode(const unsigned char *src, size_t len, char *out, size_t out_size) {
    if (!src || !out || out_size == 0) return 0;
    size_t out_len = 4 * ((len + 2) / 3);
    if (out_size < out_len + 1) return 0;

    size_t i = 0, j = 0;
    while (i < len) {
        uint32_t octet_a = i < len ? src[i++] : 0;
        uint32_t octet_b = i < len ? src[i++] : 0;
        uint32_t octet_c = i < len ? src[i++] : 0;

        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        out[j++] = b64_table[(triple >> 18) & 0x3F];
        out[j++] = b64_table[(triple >> 12) & 0x3F];
        out[j++] = (i > len + 1) ? '=' : b64_table[(triple >> 6) & 0x3F];
        out[j++] = (i > len)     ? '=' : b64_table[triple & 0x3F];
    }
    out[j] = '\0';
    return j;
}

static const int b64_index[256] = {
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,62, -1,-1,-1,63,
    52,53,54,55, 56,57,58,59, 60,61,-1,-1, -1,-1,-1,-1,
    -1, 0, 1, 2,  3, 4, 5, 6,  7, 8, 9,10, 11,12,13,14,
    15,16,17,18, 19,20,21,22, 23,24,25,-1, -1,-1,-1,-1,
    -1,26,27,28, 29,30,31,32, 33,34,35,36, 37,38,39,40,
    41,42,43,44, 45,46,47,48, 49,50,51,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1
};

size_t base64_decode(const char *src, size_t len, unsigned char *out, size_t out_size) {
    if (!src || !out || out_size == 0) return 0;
    size_t i = 0, j = 0;

    while (i < len) {
        while (i < len && (src[i] == ' ' || src[i] == '\r' || src[i] == '\n' || src[i] == '\t')) i++;
        if (i >= len || src[i] == '=') break;

        int b1 = b64_index[(unsigned char)src[i++]];
        if (b1 < 0) break;
        if (i >= len) break;
        int b2 = b64_index[(unsigned char)src[i++]];
        if (b2 < 0) break;

        if (j >= out_size) break;
        out[j++] = (unsigned char)((b1 << 2) | (b2 >> 4));

        if (i >= len || src[i] == '=') break;
        int b3 = b64_index[(unsigned char)src[i++]];
        if (b3 < 0) break;

        if (j >= out_size) break;
        out[j++] = (unsigned char)(((b2 & 0x0F) << 4) | (b3 >> 2));

        if (i >= len || src[i] == '=') break;
        int b4 = b64_index[(unsigned char)src[i++]];
        if (b4 < 0) break;

        if (j >= out_size) break;
        out[j++] = (unsigned char)(((b3 & 0x03) << 6) | b4);
    }
    return j;
}
