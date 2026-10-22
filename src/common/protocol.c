/* ============================================================
 * src/common/protocol.c
 * Parser & utility functions for the chat protocol.
 *
 * Task 1.3: Delimiter-based framing with per-session recv_buf,
 * memmove()-based sliding window, and buffer overflow guard.
 * ============================================================ */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "../../include/protocol.h"

/* Safe string copy – always NUL-terminated */
void safe_copy(char *dst, const char *src, size_t dst_size) {
    if (!dst || dst_size == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_size, "%s", src);
}

/* Trim leading and trailing whitespace in-place.
 * Returns pointer into the same buffer (may skip leading spaces). */
char* trim_whitespace(char *str) {
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}
