/* ============================================================
 * src/server/session.c
 * Client session management, presence broadcasting, account I/O.
 *
 * Task 1.2: Connection management, disconnect_client(), FD cleanup.
 * Task 1.4: Heartbeat timeout checking.
 * ============================================================ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <time.h>

#include "../../include/server.h"

/* ---- Global state (defined here, declared extern in server.h) ---- */
ClientSession g_clients[MAX_CLIENTS];
Group         g_groups[MAX_GROUPS];

/* ============================================================
 * Session helpers
 * ============================================================ */

void init_sessions(void) {
    memset(g_clients, 0, sizeof(g_clients));
    memset(g_groups, 0, sizeof(g_groups));
}

ClientSession* find_client_by_name(const char *username) {
    if (!username || strlen(username) == 0) return NULL;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].sockfd > 0 && g_clients[i].is_auth &&
            strcmp(g_clients[i].username, username) == 0) {
            return &g_clients[i];
        }
    }
    return NULL;
}

/* ============================================================
 * send_to_fd – Send a formatted string terminated by \n
 * ============================================================ */
void send_to_fd(int fd, const char *fmt, ...) {
    if (fd <= 0) return;
    char buf[BUFFER_SIZE];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf) - 2, fmt, args);
    va_end(args);

    size_t len = strlen(buf);
    if (len == 0 || buf[len - 1] != '\n') {
        buf[len++] = '\n';
        buf[len] = '\0';
    }
    send(fd, buf, len, 0);
}

/* ============================================================
 * Broadcast presence (online / offline) to all authenticated
 * clients, excluding the given fd.
 * ============================================================ */
void broadcast_presence(const char *username, bool online, int exclude_fd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].sockfd > 0 && g_clients[i].is_auth &&
            g_clients[i].sockfd != exclude_fd) {
            if (online) {
                send_to_fd(g_clients[i].sockfd, "%s %s", EVENT_ONLINE, username);
            } else {
                send_to_fd(g_clients[i].sockfd, "%s %s", EVENT_OFFLINE, username);
            }
        }
    }
}

/* ============================================================
 * disconnect_client – Centralized cleanup for a client slot.
 * Handles: 1-on-1 partner notification, group removal,
 * offline broadcast, and FD release.
 * ============================================================ */
void disconnect_client(int index) {
    ClientSession *session = &g_clients[index];
    if (session->sockfd <= 0) return;

    printf("[Server] Client fd=%d disconnected (%s)\n",
           session->sockfd, session->is_auth ? session->username : "unauthenticated");

    if (session->is_auth) {
        /* Update last_seen in SQLite database (Task 2.3) */
        db_update_last_seen(session->username);

        /* 1. Notify active 1-on-1 partner */
        if (strlen(session->active_partner) > 0) {
            ClientSession *partner = find_client_by_name(session->active_partner);
            if (partner) {
                send_to_fd(partner->sockfd, "%s User '%s' has disconnected.", RESP_NOTICE, session->username);
                if (strcmp(partner->active_partner, session->username) == 0) {
                    partner->active_partner[0] = '\0';
                }
            }
            session->active_partner[0] = '\0';
        }

        /* Check if any other client had this user as active_partner */
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (i != index && g_clients[i].sockfd > 0 && g_clients[i].is_auth) {
                if (strcmp(g_clients[i].active_partner, session->username) == 0) {
                    send_to_fd(g_clients[i].sockfd, "%s User '%s' has disconnected.", RESP_NOTICE, session->username);
                    g_clients[i].active_partner[0] = '\0';
                }
            }
        }

        /* 2. Remove from all groups and notify group members */
        remove_client_from_all_groups(session->username);

        /* 3. Abort & cleanup any ongoing file transfer sessions (Task 6.2) */
        cleanup_user_file_transfers(session->username);

        /* 4. Broadcast offline event to all active sessions */
        broadcast_presence(session->username, false, session->sockfd);
    }

    close(session->sockfd);
    session->sockfd = 0;
    session->is_auth = false;
    session->username[0] = '\0';
    session->active_partner[0] = '\0';
    session->recv_len = 0;
    session->last_active = 0;
    session->ping_pending = false;
}
