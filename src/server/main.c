/* ============================================================
 * src/server/main.c
 * Server entry point – TCP listener, select() event loop,
 * heartbeat keepalive, and graceful shutdown.
 *
 * Task 1.1: Project structure & build system
 * Task 1.2: Core TCP server, SO_REUSEADDR, SIGPIPE, select()
 * Task 1.3: Per-session recv_buf with \n delimiter parsing
 * Task 1.4: Heartbeat PING/PONG + SIGINT graceful shutdown
 * ============================================================ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <time.h>

#include "../../include/server.h"

/* ============================================================
 * Graceful shutdown flag (Task 1.4)
 * ============================================================ */
volatile sig_atomic_t g_running = 1;

static void sigint_handler(int sig) {
    (void)sig;
    g_running = 0;
}

/* ============================================================
 * Heartbeat check – called after each select() timeout.
 * Sends PING to idle clients and disconnects timed-out ones.
 * ============================================================ */
static void check_heartbeats(void) {
    time_t now = time(NULL);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].sockfd <= 0) continue;

        double idle_sec = difftime(now, g_clients[i].last_active);

        /* Disconnect if idle beyond HEARTBEAT_TIMEOUT_SEC */
        if (idle_sec >= HEARTBEAT_TIMEOUT_SEC) {
            printf("[Heartbeat] Client fd=%d (%s) timed out (%.0fs idle). Disconnecting.\n",
                   g_clients[i].sockfd,
                   g_clients[i].is_auth ? g_clients[i].username : "unauthenticated",
                   idle_sec);
            disconnect_client(i);
            continue;
        }

        /* Send PING if idle beyond half the timeout and no PING pending */
        if (idle_sec >= HEARTBEAT_INTERVAL_SEC && !g_clients[i].ping_pending) {
            send_to_fd(g_clients[i].sockfd, "%s", CMD_PING);
            g_clients[i].ping_pending = true;
        }
    }
}

/* ============================================================
 * Graceful shutdown – notify all connected clients, close
 * all sockets, and free resources.
 * ============================================================ */
static void graceful_shutdown(int listen_fd) {
    printf("\n[Server] Caught SIGINT – initiating graceful shutdown...\n");

    /* Notify all connected clients */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].sockfd > 0) {
            send_to_fd(g_clients[i].sockfd, "%s Server is shutting down. Goodbye!", RESP_SERVER_SHUTDOWN);
            disconnect_client(i);
        }
    }

    close(listen_fd);
    db_close();
    printf("[Server] All connections closed. Server stopped.\n");
}

/* ============================================================
 * main – Entry point
 * ============================================================ */
int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    if (argc >= 2) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Error: Invalid port number %s\n", argv[1]);
            return EXIT_FAILURE;
        }
    }

    /* Ignore SIGPIPE to avoid crash on broken pipe (Task 1.2) */
    signal(SIGPIPE, SIG_IGN);

    /* Install SIGINT handler for graceful shutdown (Task 1.4) */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Initialize global state */
    init_sessions();

    /* Task 2.1: Initialize SQLite3 database */
    if (db_init(DB_PATH) != 0) {
        fprintf(stderr, "[Server] Fatal: Could not initialize database %s\n", DB_PATH);
        return EXIT_FAILURE;
    }

    /* Create TCP listening socket (Task 1.2) */
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    /* SO_REUSEADDR to avoid EADDRINUSE on quick restart */
    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    if (bind(listen_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    if (listen(listen_fd, 10) < 0) {
        perror("listen");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    printf("===================================================\n");
    printf("   CHAT APPLICATION SERVER v2.0 (POSIX C - select)\n");
    printf("   Port:                   %d\n", port);
    printf("   Max concurrent clients: %d\n", MAX_CLIENTS);
    printf("   Heartbeat interval:     %ds\n", HEARTBEAT_INTERVAL_SEC);
    printf("   Heartbeat timeout:      %ds\n", HEARTBEAT_TIMEOUT_SEC);
    printf("   Press Ctrl+C for graceful shutdown.\n");
    printf("===================================================\n");

    /* ---- Main event loop ---- */
    while (g_running) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(listen_fd, &read_fds);
        int max_fd = listen_fd;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (g_clients[i].sockfd > 0) {
                FD_SET(g_clients[i].sockfd, &read_fds);
                if (g_clients[i].sockfd > max_fd) {
                    max_fd = g_clients[i].sockfd;
                }
            }
        }

        /* select() with timeout for periodic heartbeat checks (Task 1.4) */
        struct timeval timeout;
        timeout.tv_sec = SELECT_TIMEOUT_SEC;
        timeout.tv_usec = 0;

        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        if (activity < 0) {
            if (errno == EINTR) continue;   /* Interrupted by signal */
            perror("select");
            break;
        }

        /* Periodic heartbeat check on every wakeup (Task 1.4) */
        if (activity == 0) {
            check_heartbeats();
            continue;
        }

        /* Also check heartbeats even when there is activity */
        check_heartbeats();

        /* 1. New client connection */
        if (FD_ISSET(listen_fd, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int new_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
            if (new_fd >= 0) {
                int slot = -1;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (g_clients[i].sockfd == 0) {
                        slot = i;
                        break;
                    }
                }
                if (slot == -1) {
                    char msg[] = "ERROR Server is full\n";
                    send(new_fd, msg, strlen(msg), 0);
                    close(new_fd);
                    printf("[Server] Rejected incoming connection: MAX_CLIENTS reached\n");
                } else {
                    g_clients[slot].sockfd = new_fd;
                    g_clients[slot].addr = client_addr;
                    g_clients[slot].is_auth = false;
                    g_clients[slot].username[0] = '\0';
                    g_clients[slot].active_partner[0] = '\0';
                    g_clients[slot].recv_len = 0;
                    g_clients[slot].last_active = time(NULL);
                    g_clients[slot].ping_pending = false;
                    printf("[Server] New connection fd=%d from %s:%d (slot %d)\n",
                           new_fd, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), slot);
                    send_to_fd(new_fd, "%s Welcome to Chat Server! Please REGISTER or LOGIN.", RESP_NOTICE);
                }
            }
        }

        /* 2. Check activity on existing client sockets */
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (g_clients[i].sockfd > 0 && FD_ISSET(g_clients[i].sockfd, &read_fds)) {
                ClientSession *session = &g_clients[i];
                int space_left = BUFFER_SIZE - 1 - session->recv_len;

                /* Buffer overflow guard (Task 1.3) */
                if (space_left <= 0) {
                    printf("[Server] Buffer overflow for fd=%d. Resetting buffer.\n", session->sockfd);
                    send_to_fd(session->sockfd, "%s Message too long", RESP_ERR);
                    session->recv_len = 0;
                    space_left = BUFFER_SIZE - 1;
                }

                ssize_t bytes_read = recv(session->sockfd, session->recv_buf + session->recv_len, space_left, 0);
                if (bytes_read <= 0) {
                    disconnect_client(i);
                } else {
                    /* Update last_active timestamp for heartbeat (Task 1.4) */
                    session->last_active = time(NULL);
                    session->ping_pending = false;

                    session->recv_len += bytes_read;
                    session->recv_buf[session->recv_len] = '\0';

                    /* Parse complete lines ending in \n (Task 1.3 – anti sticky-packet) */
                    char *newline_pos;
                    while ((newline_pos = memchr(session->recv_buf, '\n', session->recv_len)) != NULL) {
                        size_t line_len = (size_t)(newline_pos - session->recv_buf);
                        char line[BUFFER_SIZE];
                        if (line_len >= sizeof(line)) line_len = sizeof(line) - 1;
                        memcpy(line, session->recv_buf, line_len);
                        line[line_len] = '\0';

                        /* Remove trailing \r if any */
                        if (line_len > 0 && line[line_len - 1] == '\r') {
                            line[line_len - 1] = '\0';
                        }

                        /* Shift remaining bytes in buffer (memmove) */
                        int consumed = (int)line_len + 1;
                        memmove(session->recv_buf, session->recv_buf + consumed, session->recv_len - consumed);
                        session->recv_len -= consumed;
                        session->recv_buf[session->recv_len] = '\0';

                        /* Process parsed command */
                        handle_command(session, line);

                        /* Check if client was disconnected inside handle_command */
                        if (session->sockfd == 0) break;
                    }
                }
            }
        }
    }

    /* Graceful shutdown (Task 1.4) */
    graceful_shutdown(listen_fd);
    return EXIT_SUCCESS;
}
