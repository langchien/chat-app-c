/* ============================================================
 * src/client/net_handler.c
 * Client-side network I/O helpers.
 * ============================================================ */
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include "../../include/client_ui.h"

/* Send a protocol command string terminated by \n to the server */
void send_to_server(const char *cmd) {
    if (g_sockfd < 0) return;
    char buf[BUFFER_SIZE + 64];
    snprintf(buf, sizeof(buf), "%s\n", cmd);
    send(g_sockfd, buf, strlen(buf), 0);
}
