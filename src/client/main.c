/* ============================================================
 * src/client/main.c
 * Client entry point – TCP connection, select() event loop
 * with STDIN and socket multiplexing, supporting Continuous TUI.
 *
 * Task 5.1: Window layout & Continuous TUI
 * Task 5.2: Non-blocking keystroke handler
 * Task 5.3: CLI compatibility flag (--raw / --simple-cli)
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

#include "../../include/client_ui.h"
#include "../../include/protocol.h"

/* ---- Global client state ---- */
int   g_sockfd = -1;
char  g_username[MAX_NAME_LEN] = "";
char  g_chat_partner[MAX_NAME_LEN] = "";
bool  g_logged_in = false;

/* ============================================================
 * main – Entry point
 * ============================================================ */
int main(int argc, char *argv[]) {
    char server_ip[64] = "127.0.0.1";
    int port = DEFAULT_PORT;
    bool force_raw = false;

    /* Parse command-line arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--raw") == 0 || strcmp(argv[i], "--simple-cli") == 0 || strcmp(argv[i], "--headless") == 0) {
            force_raw = true;
        } else if (i == 1 && argv[i][0] != '-') {
            strncpy(server_ip, argv[1], sizeof(server_ip) - 1);
        } else if (i == 2 && argv[i][0] != '-') {
            port = atoi(argv[2]);
            if (port <= 0 || port > 65535) {
                fprintf(stderr, "Port không hợp lệ: %s\n", argv[2]);
                return EXIT_FAILURE;
            }
        }
    }

    signal(SIGPIPE, SIG_IGN);

    g_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_sockfd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        fprintf(stderr, "Địa chỉ IP server không hợp lệ: %s\n", server_ip);
        close(g_sockfd);
        return EXIT_FAILURE;
    }

    if (force_raw) {
        printf("Đang kết nối tới server %s:%d (Chế độ CLI thuần)...\n", server_ip, port);
    } else {
        printf("Đang kết nối tới server %s:%d...\n", server_ip, port);
    }

    if (connect(g_sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Lỗi kết nối connect()");
        close(g_sockfd);
        return EXIT_FAILURE;
    }

    /* Initialize UI (TUI or Simple CLI) */
    if (!force_raw && isatty(STDIN_FILENO)) {
        ui_init(true);
        ui_add_message_fmt("%sĐã kết nối thành công tới server %s:%d!%s", COLOR_GREEN, server_ip, port, COLOR_RESET);
        show_help();
    } else {
        ui_init(false);
        printf("%sĐã kết nối thành công tới server!%s\n", COLOR_GREEN, COLOR_RESET);
        show_help();
        print_prompt();
    }

    char recv_buf[BUFFER_SIZE];
    int recv_len = 0;

    while (1) {
        fd_set read_fds;
        fd_set write_fds;
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(g_sockfd, &read_fds);

        if (g_send_job.active) {
            FD_SET(g_sockfd, &write_fds);
        }

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = g_send_job.active ? 10000 : 50000; /* 10ms when transmitting file, 50ms otherwise */

        int max_fd = (g_sockfd > STDIN_FILENO) ? g_sockfd : STDIN_FILENO;
        int activity = select(max_fd + 1, &read_fds, g_send_job.active ? &write_fds : NULL, NULL, &tv);
        if (activity < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        /* 0. Send next file chunk if socket is ready for writing (Task 4.2) */
        if (g_send_job.active && FD_ISSET(g_sockfd, &write_fds)) {
            send_next_file_chunk();
        }

        /* 1. Data received from server */
        if (FD_ISSET(g_sockfd, &read_fds)) {
            int space = BUFFER_SIZE - 1 - recv_len;
            if (space <= 0) {
                recv_len = 0;
                space = BUFFER_SIZE - 1;
            }
            ssize_t bytes_read = recv(g_sockfd, recv_buf + recv_len, space, 0);
            if (bytes_read <= 0) {
                if (g_tui_enabled) {
                    ui_add_message_fmt("%s[MẤT KẾT NỐI] Máy chủ đã ngắt kết nối hoặc dừng hoạt động.%s", COLOR_RED, COLOR_RESET);
                } else {
                    printf("\n%s[MẤT KẾT NỐI] Máy chủ đã ngắt kết nối hoặc dừng hoạt động.%s\n", COLOR_RED, COLOR_RESET);
                }
                break;
            }

            recv_len += bytes_read;
            recv_buf[recv_len] = '\0';

            char *newline_pos;
            while ((newline_pos = memchr(recv_buf, '\n', recv_len)) != NULL) {
                size_t line_len = (size_t)(newline_pos - recv_buf);
                char line[BUFFER_SIZE];
                if (line_len >= sizeof(line)) line_len = sizeof(line) - 1;
                memcpy(line, recv_buf, line_len);
                line[line_len] = '\0';

                if (line_len > 0 && line[line_len - 1] == '\r') {
                    line[line_len - 1] = '\0';
                }

                int consumed = (int)line_len + 1;
                memmove(recv_buf, recv_buf + consumed, recv_len - consumed);
                recv_len -= consumed;
                recv_buf[recv_len] = '\0';

                handle_server_line(line);
            }

            if (!g_tui_enabled) {
                print_prompt();
            }
        }

        /* 2. User input from keyboard */
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            if (g_tui_enabled) {
                /* Non-blocking character/escape-sequence reading */
                ui_process_key();
            } else {
                /* Line-buffered reading for CLI mode */
                char user_line[BUFFER_SIZE];
                if (fgets(user_line, sizeof(user_line), stdin) == NULL) {
                    /* EOF (Ctrl+D) */
                    printf("\nĐang thoát...\n");
                    send_to_server(CMD_QUIT);
                    break;
                }
                user_line[strcspn(user_line, "\r\n")] = 0;
                handle_user_input(user_line);
                print_prompt();
            }
        }
    }

    ui_cleanup();
    close(g_sockfd);
    return EXIT_SUCCESS;
}
