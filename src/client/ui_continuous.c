/* ============================================================
 * src/client/ui_continuous.c
 * Client CLI display & user input handling.
 * Currently a basic ANSI-colored line-mode interface.
 * Will be upgraded to ncurses TUI in Milestone 5.
 * ============================================================ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strcasecmp */
#include <unistd.h>
#include <stdbool.h>
#include <ctype.h>

#include "../../include/client_ui.h"
#include "../../include/protocol.h"

/* ---- Forward declaration of safe_copy & trim from common ---- */
extern void  safe_copy(char *dst, const char *src, size_t dst_size);
extern char* trim_whitespace(char *str);

/* ============================================================
 * Prompt display
 * ============================================================ */
void print_prompt(void) {
    if (!g_logged_in) {
        printf("%s[chat (chưa đăng nhập)]> %s", COLOR_WHITE, COLOR_RESET);
    } else if (strlen(g_chat_partner) > 0) {
        printf("%s[%s -> %s%s%s]> %s", COLOR_CYAN, g_username, COLOR_BOLD, g_chat_partner, COLOR_CYAN, COLOR_RESET);
    } else {
        printf("%s[%s]> %s", COLOR_GREEN, g_username, COLOR_RESET);
    }
    fflush(stdout);
}

/* ============================================================
 * Help display
 * ============================================================ */
void show_help(void) {
    printf("\n%s=========== HƯỚNG DẪN SỬ DỤNG LỆNH CHAT ===========%s\n", COLOR_CYAN, COLOR_RESET);
    if (!g_logged_in) {
        printf("  %s/login <user> <pass>%s    : Đăng nhập vào hệ thống\n", COLOR_BOLD, COLOR_RESET);
        printf("  %s/register <user> <pass>%s : Đăng ký tài khoản mới\n", COLOR_BOLD, COLOR_RESET);
        printf("  %s/help%s                   : Hiển thị hướng dẫn này\n", COLOR_BOLD, COLOR_RESET);
        printf("  %s/quit%s                   : Thoát chương trình\n", COLOR_BOLD, COLOR_RESET);
    } else {
        printf("  %s/online%s                 : Xem danh sách người dùng đang online\n", COLOR_BOLD, COLOR_RESET);
        printf("  %s/msg <user> <tin_nhắn>%s  : Gửi tin nhắn 1-1 cho người khác\n", COLOR_BOLD, COLOR_RESET);
        printf("  %s/chat <user>%s            : Mở phiên chat 1-1 (sau đó chỉ cần gõ text để gửi)\n", COLOR_BOLD, COLOR_RESET);
        printf("  %s/endchat%s                : Thoát khỏi phiên chat 1-1\n", COLOR_BOLD, COLOR_RESET);
        printf("  %s/join <nhóm>%s            : Tham gia vào nhóm chat\n", COLOR_BOLD, COLOR_RESET);
        printf("  %s/leave <nhóm>%s           : Rời khỏi nhóm chat\n", COLOR_BOLD, COLOR_RESET);
        printf("  %s/group <nhóm> <tin_nhắn>%s: Gửi tin nhắn vào một nhóm\n", COLOR_BOLD, COLOR_RESET);
        printf("  %s/groups%s                 : Xem danh sách các nhóm đang hoạt động\n", COLOR_BOLD, COLOR_RESET);
        printf("  %s/members <nhóm>%s         : Xem danh sách thành viên trong nhóm\n", COLOR_BOLD, COLOR_RESET);
        printf("  %s/history <user/nhóm> [n]%s : Xem lịch sử tin nhắn lưu trữ từ SQLite3\n", COLOR_BOLD, COLOR_RESET);
        printf("  %s/logout%s                 : Đăng xuất tài khoản\n", COLOR_BOLD, COLOR_RESET);
        printf("  %s/help%s                   : Hiển thị lại hướng dẫn này\n", COLOR_BOLD, COLOR_RESET);
        printf("  %s/quit%s                   : Thoát chương trình\n", COLOR_BOLD, COLOR_RESET);
    }
    printf("%s==================================================%s\n\n", COLOR_CYAN, COLOR_RESET);
}

/* ============================================================
 * handle_server_line – Process a single line received from server
 * ============================================================ */
void handle_server_line(char *line) {
    if (strlen(line) == 0) return;

    /* ---- Server shutdown notification ---- */
    if (strncmp(line, RESP_SERVER_SHUTDOWN " ", strlen(RESP_SERVER_SHUTDOWN) + 1) == 0) {
        char *msg = line + strlen(RESP_SERVER_SHUTDOWN) + 1;
        printf("\r\033[K%s[MÁY CHỦ] %s%s\n", COLOR_RED, msg, COLOR_RESET);
        print_prompt();
        return;
    }

    /* ---- Heartbeat: respond to server PING automatically ---- */
    if (strcmp(line, CMD_PING) == 0) {
        send_to_server(CMD_PONG);
        return;   /* Don't print PING to user */
    }

    /* ---- Parse message prefix ---- */
    if (strncmp(line, EVENT_ONLINE " ", strlen(EVENT_ONLINE) + 1) == 0) {
        char *user = line + strlen(EVENT_ONLINE) + 1;
        printf("\r\033[K%s[TRỰC TUYẾN] Người dùng '%s' vừa đăng nhập.%s\n", COLOR_GREEN, user, COLOR_RESET);
    } else if (strncmp(line, EVENT_OFFLINE " ", strlen(EVENT_OFFLINE) + 1) == 0) {
        char *user = line + strlen(EVENT_OFFLINE) + 1;
        printf("\r\033[K%s[NGOẠI TUYẾN] Người dùng '%s' đã ngắt kết nối/rời mạng.%s\n", COLOR_YELLOW, user, COLOR_RESET);
        if (strcmp(g_chat_partner, user) == 0) {
            printf("%s[THÔNG BÁO] Bạn đã thoát phiên chat 1-1 vì đối phương đã offline.%s\n", COLOR_YELLOW, COLOR_RESET);
            g_chat_partner[0] = '\0';
        }
    } else if (strncmp(line, RESP_ONLINE_LIST " ", strlen(RESP_ONLINE_LIST) + 1) == 0) {
        char *users = line + strlen(RESP_ONLINE_LIST) + 1;
        printf("\r\033[K%s[DANH SÁCH ONLINE]: %s%s%s\n", COLOR_CYAN, COLOR_BOLD, strlen(users) > 0 ? users : "(không có ai khác)", COLOR_RESET);
    } else if (strncmp(line, RESP_FROM " ", strlen(RESP_FROM) + 1) == 0) {
        char *payload = line + strlen(RESP_FROM) + 1;
        char sender[MAX_NAME_LEN] = {0};
        char *msg = strchr(payload, ' ');
        if (msg) {
            size_t slen = (size_t)(msg - payload);
            if (slen >= sizeof(sender)) slen = sizeof(sender) - 1;
            strncpy(sender, payload, slen);
            sender[slen] = '\0';
            msg = trim_whitespace(msg + 1);
            printf("\r\033[K%s[1-1 TỪ %s]:%s %s\n", COLOR_GREEN, sender, COLOR_RESET, msg);
        }
    } else if (strncmp(line, RESP_GROUP_FROM " ", strlen(RESP_GROUP_FROM) + 1) == 0) {
        char *payload = line + strlen(RESP_GROUP_FROM) + 1;
        char gname[MAX_GROUP_NAME] = {0};
        char sender[MAX_NAME_LEN] = {0};
        char *p1 = strchr(payload, ' ');
        if (p1) {
            size_t glen = (size_t)(p1 - payload);
            if (glen >= sizeof(gname)) glen = sizeof(gname) - 1;
            strncpy(gname, payload, glen);
            gname[glen] = '\0';
            char *p2 = strchr(p1 + 1, ' ');
            if (p2) {
                size_t slen = (size_t)(p2 - (p1 + 1));
                if (slen >= sizeof(sender)) slen = sizeof(sender) - 1;
                strncpy(sender, p1 + 1, slen);
                sender[slen] = '\0';
                char *msg = trim_whitespace(p2 + 1);
                printf("\r\033[K%s[NHÓM %s] %s%s:%s %s\n", COLOR_MAGENTA, gname, COLOR_BOLD, sender, COLOR_RESET, msg);
            }
        }
    } else if (strncmp(line, EVENT_GROUP " ", strlen(EVENT_GROUP) + 1) == 0) {
        char ev[16] = {0};
        char gname[MAX_GROUP_NAME] = {0};
        char user[MAX_NAME_LEN] = {0};
        sscanf(line + strlen(EVENT_GROUP) + 1, "%15s %31s %31s", ev, gname, user);
        if (strcmp(ev, "JOIN") == 0) {
            printf("\r\033[K%s[NHÓM %s] '%s' vừa tham gia nhóm.%s\n", COLOR_CYAN, gname, user, COLOR_RESET);
        } else {
            printf("\r\033[K%s[NHÓM %s] '%s' vừa rời khỏi nhóm.%s\n", COLOR_YELLOW, gname, user, COLOR_RESET);
        }
    } else if (strncmp(line, RESP_GROUPS_LIST " ", strlen(RESP_GROUPS_LIST) + 1) == 0) {
        char *groups = line + strlen(RESP_GROUPS_LIST) + 1;
        printf("\r\033[K%s[DANH SÁCH NHÓM]: %s%s%s\n", COLOR_CYAN, COLOR_BOLD, strlen(groups) > 0 ? groups : "(chưa có nhóm nào)", COLOR_RESET);
    } else if (strncmp(line, RESP_MEMBERS_LIST " ", strlen(RESP_MEMBERS_LIST) + 1) == 0) {
        char *payload = line + strlen(RESP_MEMBERS_LIST) + 1;
        char gname[MAX_GROUP_NAME] = {0};
        char *p1 = strchr(payload, ' ');
        if (p1) {
            size_t glen = (size_t)(p1 - payload);
            if (glen >= sizeof(gname)) glen = sizeof(gname) - 1;
            strncpy(gname, payload, glen);
            gname[glen] = '\0';
            printf("\r\033[K%s[THÀNH VIÊN NHÓM %s]: %s%s%s\n", COLOR_CYAN, gname, COLOR_BOLD, p1 + 1, COLOR_RESET);
        } else {
            printf("\r\033[K%s[THÀNH VIÊN NHÓM %s]: (trống)%s\n", COLOR_CYAN, payload, COLOR_RESET);
        }
    } else if (strncmp(line, RESP_NOTICE " ", strlen(RESP_NOTICE) + 1) == 0) {
        char *notice = line + strlen(RESP_NOTICE) + 1;
        printf("\r\033[K%s[THÔNG BÁO] %s%s\n", COLOR_YELLOW, notice, COLOR_RESET);
        if (strstr(notice, "ended the 1-on-1 chat") || strstr(notice, "has disconnected")) {
            if (strlen(g_chat_partner) > 0) {
                g_chat_partner[0] = '\0';
            }
        }
    } else if (strncmp(line, RESP_OK " LOGIN_OK ", strlen(RESP_OK " LOGIN_OK ")) == 0) {
        char user[MAX_NAME_LEN] = {0};
        sscanf(line + strlen(RESP_OK " LOGIN_OK "), "%31s", user);
        safe_copy(g_username, user, sizeof(g_username));
        g_logged_in = true;
        printf("\r\033[K%s[THÀNH CÔNG] Đăng nhập thành công với tài khoản: %s%s%s\n", COLOR_GREEN, COLOR_BOLD, g_username, COLOR_RESET);
        printf("%sNhập /help để xem hướng dẫn các lệnh khả dụng.%s\n", COLOR_CYAN, COLOR_RESET);
    } else if (strncmp(line, RESP_OK " START_CHAT_OK ", strlen(RESP_OK " START_CHAT_OK ")) == 0) {
        char target[MAX_NAME_LEN] = {0};
        sscanf(line + strlen(RESP_OK " START_CHAT_OK "), "%31s", target);
        safe_copy(g_chat_partner, target, sizeof(g_chat_partner));
        printf("\r\033[K%s[CHẾ ĐỘ CHAT 1-1] Đã kết nối tới '%s'. Từ bây giờ, mọi tin nhắn gõ sẽ được gửi trực tiếp. Dùng /endchat để thoát.%s\n", COLOR_GREEN, target, COLOR_RESET);
    } else if (strncmp(line, RESP_OK " END_CHAT_OK", strlen(RESP_OK " END_CHAT_OK")) == 0) {
        g_chat_partner[0] = '\0';
        printf("\r\033[K%s[THÀNH CÔNG] Đã kết thúc phiên chat 1-1.%s\n", COLOR_GREEN, COLOR_RESET);
    } else if (strncmp(line, RESP_OK " Logged out", strlen(RESP_OK " Logged out")) == 0) {
        g_logged_in = false;
        g_username[0] = '\0';
        g_chat_partner[0] = '\0';
        printf("\r\033[K%s[THÀNH CÔNG] Đã đăng xuất khỏi hệ thống.%s\n", COLOR_GREEN, COLOR_RESET);
    } else if (strncmp(line, RESP_HISTORY_START " ", strlen(RESP_HISTORY_START) + 1) == 0) {
        char *target = line + strlen(RESP_HISTORY_START) + 1;
        printf("\r\033[K%s==== LỊCH SỬ TIN NHẮN VỚI '%s' ====%s\n", COLOR_CYAN, target, COLOR_RESET);
    } else if (strncmp(line, RESP_HISTORY_ITEM " ", strlen(RESP_HISTORY_ITEM) + 1) == 0) {
        char *payload = line + strlen(RESP_HISTORY_ITEM) + 1;
        /* format: <timestamp> <sender> <content> */
        char date[16] = {0}, time_str[16] = {0}, sender[MAX_NAME_LEN] = {0};
        int offset = 0;
        if (sscanf(payload, "%15s %15s %31s%n", date, time_str, sender, &offset) >= 3) {
            char *msg = trim_whitespace(payload + offset);
            printf("\r\033[K%s[%s %s] %s%s:%s %s\n", COLOR_BLUE, date, time_str, COLOR_BOLD, sender, COLOR_RESET, msg);
        } else {
            printf("\r\033[K%s[LỊCH SỬ] %s%s\n", COLOR_BLUE, payload, COLOR_RESET);
        }
    } else if (strncmp(line, RESP_HISTORY_END " ", strlen(RESP_HISTORY_END) + 1) == 0) {
        char target[MAX_NAME_LEN] = {0};
        int count = 0;
        sscanf(line + strlen(RESP_HISTORY_END) + 1, "%31s %d", target, &count);
        printf("\r\033[K%s==== Đã tải %d tin nhắn lịch sử của '%s' ====%s\n", COLOR_CYAN, count, target, COLOR_RESET);
    } else if (strncmp(line, RESP_OFFLINE_START " ", strlen(RESP_OFFLINE_START) + 1) == 0) {
        int count = atoi(line + strlen(RESP_OFFLINE_START) + 1);
        printf("\r\033[K%s[HỘP THƯ NGOẠI TUYẾN] Bạn có %d tin nhắn mới khi vắng mặt:%s\n", COLOR_YELLOW, count, COLOR_RESET);
    } else if (strncmp(line, RESP_OFFLINE_MSG " ", strlen(RESP_OFFLINE_MSG) + 1) == 0) {
        char *payload = line + strlen(RESP_OFFLINE_MSG) + 1;
        char sender[MAX_NAME_LEN] = {0}, date[16] = {0}, time_str[16] = {0};
        int offset = 0;
        if (sscanf(payload, "%31s %15s %15s%n", sender, date, time_str, &offset) >= 3) {
            char *msg = trim_whitespace(payload + offset);
            printf("\r\033[K%s[NGOẠI TUYẾN - %s %s] %s%s:%s %s\n", COLOR_YELLOW, date, time_str, COLOR_BOLD, sender, COLOR_RESET, msg);
        } else {
            printf("\r\033[K%s[NGOẠI TUYẾN] %s%s\n", COLOR_YELLOW, payload, COLOR_RESET);
        }
    } else if (strcmp(line, RESP_OFFLINE_END) == 0) {
        printf("\r\033[K%s--- Đã nạp xong toàn bộ tin nhắn ngoại tuyến ---%s\n", COLOR_YELLOW, COLOR_RESET);
    } else if (strncmp(line, EVENT_TYPING " ", strlen(EVENT_TYPING) + 1) == 0) {
        char *sender = line + strlen(EVENT_TYPING) + 1;
        printf("\r\033[K%s... '%s' đang soạn tin nhắn ...%s\n", COLOR_BLUE, sender, COLOR_RESET);
    } else if (strncmp(line, RESP_OK, strlen(RESP_OK)) == 0) {
        char *detail = line + strlen(RESP_OK);
        if (*detail == ' ') detail++;
        printf("\r\033[K%s[THÀNH CÔNG] %s%s\n", COLOR_GREEN, detail, COLOR_RESET);
    } else if (strncmp(line, RESP_ERR, strlen(RESP_ERR)) == 0) {
        char *detail = line + strlen(RESP_ERR);
        if (*detail == ' ') detail++;
        printf("\r\033[K%s[LỖI] %s%s\n", COLOR_RED, detail, COLOR_RESET);
    } else {
        printf("\r\033[K[Server]: %s\n", line);
    }

    print_prompt();
}

/* ============================================================
 * handle_user_input – Process a line typed by the user
 * ============================================================ */
void handle_user_input(char *raw_input) {
    char *input = trim_whitespace(raw_input);
    if (strlen(input) == 0) {
        print_prompt();
        return;
    }

    /* Command starting with slash */
    if (input[0] == '/') {
        char cmd[64] = {0};
        char *rest = NULL;
        char *space = strchr(input, ' ');
        if (space) {
            size_t clen = (size_t)(space - input);
            if (clen >= sizeof(cmd)) clen = sizeof(cmd) - 1;
            strncpy(cmd, input, clen);
            cmd[clen] = '\0';
            rest = trim_whitespace(space + 1);
        } else {
            strncpy(cmd, input, sizeof(cmd) - 1);
            cmd[sizeof(cmd) - 1] = '\0';
            rest = "";
        }

        if (strcasecmp(cmd, "/help") == 0) {
            show_help();
            print_prompt();
            return;
        }

        if (strcasecmp(cmd, "/quit") == 0 || strcasecmp(cmd, "/exit") == 0) {
            printf("%sTạm biệt! Đang ngắt kết nối...%s\n", COLOR_YELLOW, COLOR_RESET);
            send_to_server(CMD_QUIT);
            close(g_sockfd);
            exit(EXIT_SUCCESS);
        }

        if (strcasecmp(cmd, "/login") == 0) {
            char user[MAX_NAME_LEN] = {0};
            char pass[MAX_PASS_LEN] = {0};
            if (sscanf(rest, "%31s %63s", user, pass) != 2) {
                printf("%s[LỖI] Cú pháp: /login <username> <password>%s\n", COLOR_RED, COLOR_RESET);
                print_prompt();
                return;
            }
            char buf[128];
            snprintf(buf, sizeof(buf), "%s %s %s", CMD_LOGIN, user, pass);
            send_to_server(buf);
            return;
        }

        if (strcasecmp(cmd, "/register") == 0) {
            char user[MAX_NAME_LEN] = {0};
            char pass[MAX_PASS_LEN] = {0};
            if (sscanf(rest, "%31s %63s", user, pass) != 2) {
                printf("%s[LỖI] Cú pháp: /register <username> <password>%s\n", COLOR_RED, COLOR_RESET);
                print_prompt();
                return;
            }
            char buf[128];
            snprintf(buf, sizeof(buf), "%s %s %s", CMD_REGISTER, user, pass);
            send_to_server(buf);
            return;
        }

        if (strcasecmp(cmd, "/logout") == 0) {
            send_to_server(CMD_LOGOUT);
            return;
        }

        if (strcasecmp(cmd, "/online") == 0 || strcasecmp(cmd, "/users") == 0) {
            send_to_server(CMD_USERS);
            return;
        }

        if (strcasecmp(cmd, "/msg") == 0) {
            char target[MAX_NAME_LEN] = {0};
            char *msg = NULL;
            char *space_pos = strchr(rest, ' ');
            if (!space_pos) {
                printf("%s[LỖI] Cú pháp: /msg <username> <tin_nhắn>%s\n", COLOR_RED, COLOR_RESET);
                print_prompt();
                return;
            }
            size_t tlen = (size_t)(space_pos - rest);
            if (tlen >= sizeof(target)) tlen = sizeof(target) - 1;
            strncpy(target, rest, tlen);
            target[tlen] = '\0';
            msg = trim_whitespace(space_pos + 1);

            char buf[BUFFER_SIZE];
            snprintf(buf, sizeof(buf), "%s %s %s", CMD_MSG, target, msg);
            send_to_server(buf);
            return;
        }

        if (strcasecmp(cmd, "/chat") == 0) {
            char target[MAX_NAME_LEN] = {0};
            if (sscanf(rest, "%31s", target) != 1) {
                printf("%s[LỖI] Cú pháp: /chat <username>%s\n", COLOR_RED, COLOR_RESET);
                print_prompt();
                return;
            }
            char buf[128];
            snprintf(buf, sizeof(buf), "%s %s", CMD_START_CHAT, target);
            send_to_server(buf);
            return;
        }

        if (strcasecmp(cmd, "/endchat") == 0) {
            send_to_server(CMD_END_CHAT);
            return;
        }

        if (strcasecmp(cmd, "/join") == 0) {
            char gname[MAX_GROUP_NAME] = {0};
            if (sscanf(rest, "%31s", gname) != 1) {
                printf("%s[LỖI] Cú pháp: /join <tên_nhóm>%s\n", COLOR_RED, COLOR_RESET);
                print_prompt();
                return;
            }
            char buf[128];
            snprintf(buf, sizeof(buf), "%s %s", CMD_JOIN, gname);
            send_to_server(buf);
            return;
        }

        if (strcasecmp(cmd, "/leave") == 0) {
            char gname[MAX_GROUP_NAME] = {0};
            if (sscanf(rest, "%31s", gname) != 1) {
                printf("%s[LỖI] Cú pháp: /leave <tên_nhóm>%s\n", COLOR_RED, COLOR_RESET);
                print_prompt();
                return;
            }
            char buf[128];
            snprintf(buf, sizeof(buf), "%s %s", CMD_LEAVE, gname);
            send_to_server(buf);
            return;
        }

        if (strcasecmp(cmd, "/group") == 0) {
            char gname[MAX_GROUP_NAME] = {0};
            char *msg = NULL;
            char *space_pos = strchr(rest, ' ');
            if (!space_pos) {
                printf("%s[LỖI] Cú pháp: /group <tên_nhóm> <tin_nhắn>%s\n", COLOR_RED, COLOR_RESET);
                print_prompt();
                return;
            }
            size_t glen = (size_t)(space_pos - rest);
            if (glen >= sizeof(gname)) glen = sizeof(gname) - 1;
            strncpy(gname, rest, glen);
            gname[glen] = '\0';
            msg = trim_whitespace(space_pos + 1);

            char buf[BUFFER_SIZE];
            snprintf(buf, sizeof(buf), "%s %s %s", CMD_GROUP_MSG, gname, msg);
            send_to_server(buf);
            return;
        }

        if (strcasecmp(cmd, "/groups") == 0) {
            send_to_server(CMD_GROUPS);
            return;
        }

        if (strcasecmp(cmd, "/members") == 0) {
            char gname[MAX_GROUP_NAME] = {0};
            if (sscanf(rest, "%31s", gname) != 1) {
                printf("%s[LỖI] Cú pháp: /members <tên_nhóm>%s\n", COLOR_RED, COLOR_RESET);
                print_prompt();
                return;
            }
            char buf[128];
            snprintf(buf, sizeof(buf), "%s %s", CMD_MEMBERS, gname);
            send_to_server(buf);
            return;
        }

        if (strcasecmp(cmd, "/history") == 0) {
            char target[MAX_NAME_LEN] = {0};
            int limit = DEFAULT_HISTORY_LIMIT;
            int args = sscanf(rest, "%31s %d", target, &limit);
            if (args < 1) {
                if (strlen(g_chat_partner) > 0) {
                    safe_copy(target, g_chat_partner, sizeof(target));
                } else {
                    printf("%s[LỖI] Cú pháp: /history <username/tên_nhóm> [số_lượng]%s\n", COLOR_RED, COLOR_RESET);
                    print_prompt();
                    return;
                }
            }
            char buf[128];
            snprintf(buf, sizeof(buf), "%s %s %d", CMD_HISTORY, target, limit);
            send_to_server(buf);
            return;
        }

        printf("%s[LỖI] Lệnh '%s' không tồn tại. Gõ /help để xem danh sách lệnh.%s\n", COLOR_RED, cmd, COLOR_RESET);
        print_prompt();
        return;
    }

    /* Not starting with slash: regular text message */
    if (strlen(g_chat_partner) > 0) {
        char buf[BUFFER_SIZE];
        snprintf(buf, sizeof(buf), "%s %s %s", CMD_MSG, g_chat_partner, input);
        send_to_server(buf);
    } else {
        printf("%s[HƯỚNG DẪN] Bạn chưa ở trong phiên chat 1-1. Hãy dùng:%s\n", COLOR_YELLOW, COLOR_RESET);
        printf("  - %s/msg <user> <tin nhắn>%s để gửi tin lẻ\n", COLOR_BOLD, COLOR_RESET);
        printf("  - %s/chat <user>%s để vào phòng chat riêng 1-1\n", COLOR_BOLD, COLOR_RESET);
        printf("  - %s/help%s để xem tất cả các lệnh\n", COLOR_BOLD, COLOR_RESET);
        print_prompt();
    }
}
