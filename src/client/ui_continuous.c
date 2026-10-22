/* ============================================================
 * src/client/ui_continuous.c
 * Continuous Terminal UI (TUI) & Non-blocking Keystroke Handler.
 *
 * Task 5.1: Terminal layout (Chat history, Online sidebar, Status bar, Input box)
 * Task 5.2: Asynchronous non-blocking keystrokes (Arrow keys, Tab autocomplete, PgUp/PgDn scroll)
 * Task 5.3: Fallback CLI mode (--raw / --simple-cli) for automated tests & headless environments
 * ============================================================ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strcasecmp */
#include <unistd.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdarg.h>
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>

#include "../../include/client_ui.h"
#include "../../include/protocol.h"

/* ---- Forward declarations from common ---- */
extern void  safe_copy(char *dst, const char *src, size_t dst_size);
extern char* trim_whitespace(char *str);

/* ---- Global state ---- */
bool g_tui_enabled = false;
static struct termios g_orig_termios;
static bool g_raw_mode = false;

/* Terminal dimensions */
static int g_term_rows = 24;
static int g_term_cols = 80;

/* Chat history ring buffer */
typedef struct {
    char text[1024];
} ChatLogItem;

static ChatLogItem g_chat_log[MAX_CHAT_HISTORY];
static int g_chat_count = 0;
static int g_scroll_offset = 0;

/* Online users & joined groups */
static char g_online_users[MAX_ONLINE_USERS][MAX_NAME_LEN];
static int  g_online_count = 0;

static char g_joined_groups[MAX_GROUPS_TRACK][MAX_GROUP_NAME];
static int  g_group_count = 0;

/* Input buffer state */
static char g_input_buf[BUFFER_SIZE] = {0};
static int  g_input_len = 0;
static int  g_cursor_pos = 0;

/* Status bar message override */
static char g_status_msg[256] = "";

/* Command list for Tab autocomplete */
static const char *AVAILABLE_COMMANDS[] = {
    "/help", "/login", "/register", "/logout", "/online", "/users",
    "/msg", "/chat", "/endchat", "/join", "/leave", "/group",
    "/groups", "/members", "/history", "/sendfile", "/sendimg", "/quit", NULL
};

/* ---- Forward static declarations ---- */
static void update_terminal_size(void);
static void sigwinch_handler(int sig);
static void ui_redraw_header(void);
static void ui_redraw_chat(void);
static void ui_redraw_sidebar(void);
static void ui_redraw_separators(void);
static void ui_redraw_status(void);
static void ui_redraw_input(void);
static void handle_tab_completion(void);

/* ============================================================
 * Terminal Dimension & Signal Handling
 * ============================================================ */
static void update_terminal_size(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0) {
        g_term_rows = ws.ws_row;
        g_term_cols = ws.ws_col;
    } else {
        g_term_rows = 24;
        g_term_cols = 80;
    }
    if (g_term_rows < 16) g_term_rows = 16;
    if (g_term_cols < 60) g_term_cols = 60;
}

static void sigwinch_handler(int sig) {
    (void)sig;
    if (g_tui_enabled) {
        update_terminal_size();
        ui_redraw_all();
    }
}

/* ============================================================
 * UI Initialization & Cleanup (Task 5.1 & 5.3)
 * ============================================================ */
void ui_init(bool force_tui) {
    if (!force_tui && !isatty(STDIN_FILENO)) {
        g_tui_enabled = false;
        return;
    }

    if (tcgetattr(STDIN_FILENO, &g_orig_termios) < 0) {
        g_tui_enabled = false;
        return;
    }

    struct termios raw = g_orig_termios;
    /* Disable canonical mode, echo, extended input processing */
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0) {
        g_tui_enabled = false;
        return;
    }

    g_raw_mode = true;
    g_tui_enabled = true;

    update_terminal_size();
    signal(SIGWINCH, sigwinch_handler);
    atexit(ui_cleanup);

    /* Switch to alternate screen buffer or clear full screen */
    printf("\033[?25h\033[2J\033[H");
    fflush(stdout);

    ui_redraw_all();
}

void ui_cleanup(void) {
    if (g_raw_mode) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);
        g_raw_mode = false;
    }
    if (g_tui_enabled) {
        printf("\033[?25h\033[%d;1H\n", g_term_rows);
        fflush(stdout);
    }
}

/* ============================================================
 * Drawing Helpers – Pure ANSI VT100
 * ============================================================ */
static int get_chat_width(void) {
    int w = (g_term_cols * 3) / 4;
    return (w > 40) ? w : 40;
}

static int get_chat_height(void) {
    /* Top 1 line header, bottom 4 lines for separators, status and input */
    int h = g_term_rows - 5;
    return (h > 5) ? h : 5;
}

static void ui_redraw_header(void) {
    printf("\033[1;1H\033[7m\033[1;36m");
    char title[128];
    if (g_logged_in) {
        snprintf(title, sizeof(title), "  💬 C CHAT APP v2.0  │  Người dùng: %s  │  Trạng thái: ĐÃ ĐĂNG NHẬP", g_username);
    } else {
        snprintf(title, sizeof(title), "  💬 C CHAT APP v2.0  │  (Chưa đăng nhập - gõ /login <user> <pass>)");
    }

    int len = (int)strlen(title);
    printf("%s", title);
    for (int i = len; i < g_term_cols; i++) {
        putchar(' ');
    }
    printf("\033[0m");
}

static void ui_redraw_chat(void) {
    int chat_w = get_chat_width();
    int chat_h = get_chat_height();
    int start_row = 2;

    int total = g_chat_count;
    int start_idx = total - chat_h - g_scroll_offset;
    if (start_idx < 0) start_idx = 0;
    int end_idx = total - g_scroll_offset;
    if (end_idx > total) end_idx = total;

    for (int i = 0; i < chat_h; i++) {
        int row = start_row + i;
        printf("\033[%d;1H", row);

        int log_idx = start_idx + i;
        if (log_idx < end_idx && log_idx >= 0 && log_idx < total) {
            /* Print chat message line */
            printf("%s", g_chat_log[log_idx].text);
        }

        /* Clear line up to separator column without touching the separator */
        printf("\033[%d;%dH", row, chat_w);
    }

    /* Scroll indicator if viewing old messages */
    if (g_scroll_offset > 0) {
        printf("\033[2;2H\033[1;33m\033[7m[▲ ĐANG XEM TIN CŨ (+%d) - Bấm PgDn hoặc Mũi tên Xuống để về mới nhất]\033[0m", g_scroll_offset);
    }
}

static void ui_redraw_sidebar(void) {
    int chat_w = get_chat_width();
    int sep_col = chat_w;
    int sidebar_col = sep_col + 2;
    int sidebar_w = g_term_cols - sidebar_col;
    if (sidebar_w < 5) sidebar_w = 5;

    int chat_h = get_chat_height();
    int start_row = 2;

    /* Draw vertical separator */
    for (int i = 0; i < chat_h; i++) {
        int row = start_row + i;
        printf("\033[%d;%dH\033[1;34m│\033[0m", row, sep_col);
        /* Clear right area */
        printf("\033[%d;%dH\033[K", row, sidebar_col);
    }

    int cur_r = start_row;

    /* Online Users Section */
    printf("\033[%d;%dH\033[1;32m👥 TRỰC TUYẾN (%d)\033[0m", cur_r++, sidebar_col, g_online_count);
    for (int i = 0; i < g_online_count && cur_r < start_row + (chat_h / 2); i++) {
        printf("\033[%d;%dH", cur_r++, sidebar_col);
        if (strcmp(g_online_users[i], g_username) == 0) {
            printf("\033[32m● \033[1m%s \033[0;32m(bạn)\033[0m", g_online_users[i]);
        } else if (strlen(g_chat_partner) > 0 && strcmp(g_online_users[i], g_chat_partner) == 0) {
            printf("\033[33m● \033[1;33m%s \033[0;33m[Chat]\033[0m", g_online_users[i]);
        } else {
            printf("\033[36m● %s\033[0m", g_online_users[i]);
        }
    }

    cur_r++;
    if (cur_r < start_row + chat_h - 1) {
        /* Joined Groups Section */
        printf("\033[%d;%dH\033[1;35m💬 NHÓM CỦA BẠN (%d)\033[0m", cur_r++, sidebar_col, g_group_count);
        for (int i = 0; i < g_group_count && cur_r < start_row + chat_h; i++) {
            printf("\033[%d;%dH\033[35m# %s\033[0m", cur_r++, sidebar_col, g_joined_groups[i]);
        }
    }
}

static void ui_redraw_separators(void) {
    int chat_w = get_chat_width();

    /* Separator above status bar */
    printf("\033[%d;1H\033[1;34m", g_term_rows - 3);
    for (int c = 1; c <= g_term_cols; c++) {
        if (c == chat_w) {
            printf("┴");
        } else {
            printf("─");
        }
    }
    printf("\033[0m");

    /* Separator above input box */
    printf("\033[%d;1H\033[1;34m", g_term_rows - 1);
    for (int c = 1; c <= g_term_cols; c++) {
        printf("─");
    }
    printf("\033[0m");
}

static void ui_redraw_status(void) {
    printf("\033[%d;1H\033[2K", g_term_rows - 2);

    char left_status[256] = "";
    if (g_send_job.active) {
        int pct = g_send_job.filesize > 0 ? (int)(g_send_job.bytes_sent * 100 / g_send_job.filesize) : 0;
        snprintf(left_status, sizeof(left_status),
                 "\033[1;33m[📤 GỬI: %s (%d%%) %ld/%ld B]\033[0m",
                 g_send_job.filename, pct, g_send_job.bytes_sent, g_send_job.filesize);
    } else if (g_recv_job.active) {
        int pct = g_recv_job.expected_size > 0 ? (int)(g_recv_job.received_bytes * 100 / g_recv_job.expected_size) : 0;
        snprintf(left_status, sizeof(left_status),
                 "\033[1;32m[📥 NHẬN TỪ %s: %s (%d%%)]\033[0m",
                 g_recv_job.sender, g_recv_job.filename, pct);
    } else if (strlen(g_chat_partner) > 0) {
        snprintf(left_status, sizeof(left_status),
                 "\033[1;36m[Phòng Chat 1-1 với: %s%s%s]\033[0m",
                 COLOR_BOLD, g_chat_partner, COLOR_CYAN);
    } else if (strlen(g_status_msg) > 0) {
        snprintf(left_status, sizeof(left_status), "\033[33m%s\033[0m", g_status_msg);
    } else {
        snprintf(left_status, sizeof(left_status), "\033[32m[Sẵn sàng nhắn tin]\033[0m");
    }

    const char *shortcuts = "\033[37m[Tab: Gợi ý lệnh/user] [PgUp/PgDn: Cuộn tin] [/help: Trợ giúp]\033[0m";
    printf(" %s  %s", left_status, shortcuts);
}

static void ui_redraw_input(void) {
    printf("\033[%d;1H\033[2K", g_term_rows);

    char prompt[128];
    if (!g_logged_in) {
        snprintf(prompt, sizeof(prompt), "%s[chưa đăng nhập]> %s", COLOR_WHITE, COLOR_RESET);
    } else if (strlen(g_chat_partner) > 0) {
        snprintf(prompt, sizeof(prompt), "%s[%s -> %s%s%s]> %s",
                 COLOR_CYAN, g_username, COLOR_BOLD, g_chat_partner, COLOR_CYAN, COLOR_RESET);
    } else {
        snprintf(prompt, sizeof(prompt), "%s[%s]> %s", COLOR_GREEN, g_username, COLOR_RESET);
    }

    printf("%s%s", prompt, g_input_buf);

    /* Move terminal cursor to current editing position */
    int prompt_len = 0;
    if (!g_logged_in) prompt_len = 19;
    else if (strlen(g_chat_partner) > 0) prompt_len = (int)strlen(g_username) + (int)strlen(g_chat_partner) + 7;
    else prompt_len = (int)strlen(g_username) + 4;

    int cursor_col = prompt_len + g_cursor_pos + 1;
    if (cursor_col > g_term_cols) cursor_col = g_term_cols;
    printf("\033[%d;%dH", g_term_rows, cursor_col);
    fflush(stdout);
}

void ui_redraw_all(void) {
    if (!g_tui_enabled) return;

    printf("\033[2J"); /* Clear screen */
    ui_redraw_header();
    ui_redraw_chat();
    ui_redraw_sidebar();
    ui_redraw_separators();
    ui_redraw_status();
    ui_redraw_input();
    fflush(stdout);
}

void ui_update_status(void) {
    if (!g_tui_enabled) return;
    printf("\033[s");
    ui_redraw_status();
    printf("\033[u");
    fflush(stdout);
}

/* ============================================================
 * Adding Messages to History & Display
 * ============================================================ */
void ui_add_message(const char *msg) {
    if (!msg) return;

    if (!g_tui_enabled) {
        printf("%s\n", msg);
        fflush(stdout);
        return;
    }

    /* Store in chat history ring buffer */
    if (g_chat_count < MAX_CHAT_HISTORY) {
        safe_copy(g_chat_log[g_chat_count].text, msg, sizeof(g_chat_log[g_chat_count].text));
        g_chat_count++;
    } else {
        memmove(&g_chat_log[0], &g_chat_log[1], sizeof(ChatLogItem) * (MAX_CHAT_HISTORY - 1));
        safe_copy(g_chat_log[MAX_CHAT_HISTORY - 1].text, msg, sizeof(g_chat_log[MAX_CHAT_HISTORY - 1].text));
    }

    /* Only automatically scroll down if user is at the bottom */
    if (g_scroll_offset == 0) {
        printf("\033[s"); /* Save cursor */
        ui_redraw_chat();
        printf("\033[u"); /* Restore cursor to input box */
        fflush(stdout);
    }
}

void ui_add_message_fmt(const char *fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    ui_add_message(buf);
}

/* ============================================================
 * Sidebar State Updating (Online Users & Groups)
 * ============================================================ */
void ui_update_online_users(const char *user_list_str) {
    g_online_count = 0;
    if (user_list_str && strlen(user_list_str) > 0) {
        char temp[BUFFER_SIZE];
        safe_copy(temp, user_list_str, sizeof(temp));
        char *token = strtok(temp, " \t\r\n");
        while (token && g_online_count < MAX_ONLINE_USERS) {
            safe_copy(g_online_users[g_online_count], token, sizeof(g_online_users[g_online_count]));
            g_online_count++;
            token = strtok(NULL, " \t\r\n");
        }
    }

    if (g_tui_enabled) {
        printf("\033[s");
        ui_redraw_sidebar();
        printf("\033[u");
        fflush(stdout);
    }
}

void ui_add_online_user(const char *username) {
    if (!username || strlen(username) == 0) return;
    for (int i = 0; i < g_online_count; i++) {
        if (strcmp(g_online_users[i], username) == 0) return;
    }
    if (g_online_count < MAX_ONLINE_USERS) {
        safe_copy(g_online_users[g_online_count], username, sizeof(g_online_users[g_online_count]));
        g_online_count++;
    }
    if (g_tui_enabled) {
        printf("\033[s");
        ui_redraw_sidebar();
        printf("\033[u");
        fflush(stdout);
    }
}

void ui_remove_online_user(const char *username) {
    if (!username || strlen(username) == 0) return;
    for (int i = 0; i < g_online_count; i++) {
        if (strcmp(g_online_users[i], username) == 0) {
            for (int j = i; j < g_online_count - 1; j++) {
                safe_copy(g_online_users[j], g_online_users[j + 1], sizeof(g_online_users[j]));
            }
            g_online_count--;
            break;
        }
    }
    if (g_tui_enabled) {
        printf("\033[s");
        ui_redraw_sidebar();
        printf("\033[u");
        fflush(stdout);
    }
}

void ui_update_groups(const char *group_list_str) {
    g_group_count = 0;
    if (group_list_str && strlen(group_list_str) > 0) {
        char temp[BUFFER_SIZE];
        safe_copy(temp, group_list_str, sizeof(temp));
        char *token = strtok(temp, " \t\r\n");
        while (token && g_group_count < MAX_GROUPS_TRACK) {
            safe_copy(g_joined_groups[g_group_count], token, sizeof(g_joined_groups[g_group_count]));
            g_group_count++;
            token = strtok(NULL, " \t\r\n");
        }
    }
    if (g_tui_enabled) {
        printf("\033[s");
        ui_redraw_sidebar();
        printf("\033[u");
        fflush(stdout);
    }
}

void ui_add_group(const char *groupname) {
    if (!groupname || strlen(groupname) == 0) return;
    for (int i = 0; i < g_group_count; i++) {
        if (strcmp(g_joined_groups[i], groupname) == 0) return;
    }
    if (g_group_count < MAX_GROUPS_TRACK) {
        safe_copy(g_joined_groups[g_group_count], groupname, sizeof(g_joined_groups[g_group_count]));
        g_group_count++;
    }
    if (g_tui_enabled) {
        printf("\033[s");
        ui_redraw_sidebar();
        printf("\033[u");
        fflush(stdout);
    }
}

void ui_remove_group(const char *groupname) {
    if (!groupname || strlen(groupname) == 0) return;
    for (int i = 0; i < g_group_count; i++) {
        if (strcmp(g_joined_groups[i], groupname) == 0) {
            for (int j = i; j < g_group_count - 1; j++) {
                safe_copy(g_joined_groups[j], g_joined_groups[j + 1], sizeof(g_joined_groups[j]));
            }
            g_group_count--;
            break;
        }
    }
    if (g_tui_enabled) {
        printf("\033[s");
        ui_redraw_sidebar();
        printf("\033[u");
        fflush(stdout);
    }
}

/* ============================================================
 * Scrolling History (Task 5.2)
 * ============================================================ */
void ui_scroll_up(int lines) {
    if (!g_tui_enabled) return;
    int max_scroll = g_chat_count - get_chat_height();
    if (max_scroll < 0) max_scroll = 0;

    g_scroll_offset += lines;
    if (g_scroll_offset > max_scroll) g_scroll_offset = max_scroll;

    printf("\033[s");
    ui_redraw_chat();
    printf("\033[u");
    fflush(stdout);
}

void ui_scroll_down(int lines) {
    if (!g_tui_enabled) return;
    g_scroll_offset -= lines;
    if (g_scroll_offset < 0) g_scroll_offset = 0;

    printf("\033[s");
    ui_redraw_chat();
    printf("\033[u");
    fflush(stdout);
}

/* ============================================================
 * Tab Autocomplete Handler (Task 5.2)
 * ============================================================ */
static void handle_tab_completion(void) {
    if (g_input_len == 0) return;

    /* Case 1: Slash command autocomplete */
    if (g_input_buf[0] == '/' && strchr(g_input_buf, ' ') == NULL) {
        int match_count = 0;
        const char *best_match = NULL;
        for (int i = 0; AVAILABLE_COMMANDS[i] != NULL; i++) {
            if (strncmp(AVAILABLE_COMMANDS[i], g_input_buf, g_input_len) == 0) {
                match_count++;
                best_match = AVAILABLE_COMMANDS[i];
            }
        }

        if (match_count == 1 && best_match) {
            safe_copy(g_input_buf, best_match, sizeof(g_input_buf));
            g_input_len = (int)strlen(g_input_buf);
            if (g_input_len < (int)sizeof(g_input_buf) - 2) {
                g_input_buf[g_input_len++] = ' ';
                g_input_buf[g_input_len] = '\0';
            }
            g_cursor_pos = g_input_len;
            ui_redraw_input();
        } else if (match_count > 1) {
            /* Show possible matches in chat window */
            char hints[512] = "Gợi ý lệnh: ";
            for (int i = 0; AVAILABLE_COMMANDS[i] != NULL; i++) {
                if (strncmp(AVAILABLE_COMMANDS[i], g_input_buf, g_input_len) == 0) {
                    strncat(hints, AVAILABLE_COMMANDS[i], sizeof(hints) - strlen(hints) - 2);
                    strncat(hints, "  ", sizeof(hints) - strlen(hints) - 2);
                }
            }
            ui_add_message_fmt("%s%s%s", COLOR_CYAN, hints, COLOR_RESET);
        }
        return;
    }

    /* Case 2: Target username autocomplete after command (/msg, /chat, /sendfile, etc.) */
    char *last_space = strrchr(g_input_buf, ' ');
    if (last_space && last_space[1] != '\0') {
        char *prefix = last_space + 1;
        int plen = (int)strlen(prefix);

        int match_count = 0;
        const char *best_match = NULL;
        for (int i = 0; i < g_online_count; i++) {
            if (strncmp(g_online_users[i], prefix, plen) == 0) {
                match_count++;
                best_match = g_online_users[i];
            }
        }

        if (match_count == 1 && best_match) {
            *last_space = '\0';
            snprintf(g_input_buf, sizeof(g_input_buf), "%s %s ", g_input_buf, best_match);
            g_input_len = (int)strlen(g_input_buf);
            g_cursor_pos = g_input_len;
            ui_redraw_input();
        }
    }
}

/* ============================================================
 * Asynchronous Keystroke Processing (Task 5.2)
 * ============================================================ */
void ui_process_key(void) {
    char c;
    if (read(STDIN_FILENO, &c, 1) <= 0) return;

    /* ---- Escape sequence handling (Arrow keys, PageUp/Down, Delete) ---- */
    if (c == '\033') {
        char seq[8] = {0};
        int n = 0;
        struct timeval tv = {0, 30000}; /* 30ms timeout */
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);

        while (n < (int)sizeof(seq) - 1 && select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
            if (read(STDIN_FILENO, &seq[n], 1) <= 0) break;
            n++;
            FD_ZERO(&fds);
            FD_SET(STDIN_FILENO, &fds);
        }

        if (n >= 1 && seq[0] == '[') {
            if (seq[1] == 'A') { /* Up Arrow -> Scroll up history */
                ui_scroll_up(1);
            } else if (seq[1] == 'B') { /* Down Arrow -> Scroll down history */
                ui_scroll_down(1);
            } else if (seq[1] == 'C') { /* Right Arrow -> Move cursor right */
                if (g_cursor_pos < g_input_len) {
                    g_cursor_pos++;
                    ui_redraw_input();
                }
            } else if (seq[1] == 'D') { /* Left Arrow -> Move cursor left */
                if (g_cursor_pos > 0) {
                    g_cursor_pos--;
                    ui_redraw_input();
                }
            } else if (seq[1] == '5' && seq[2] == '~') { /* Page Up */
                ui_scroll_up(get_chat_height() / 2);
            } else if (seq[1] == '6' && seq[2] == '~') { /* Page Down */
                ui_scroll_down(get_chat_height() / 2);
            } else if (seq[1] == '3' && seq[2] == '~') { /* Delete key */
                if (g_cursor_pos < g_input_len) {
                    memmove(&g_input_buf[g_cursor_pos], &g_input_buf[g_cursor_pos + 1], g_input_len - g_cursor_pos);
                    g_input_len--;
                    ui_redraw_input();
                }
            } else if (seq[1] == 'H') { /* Home */
                g_cursor_pos = 0;
                ui_redraw_input();
            } else if (seq[1] == 'F') { /* End */
                g_cursor_pos = g_input_len;
                ui_redraw_input();
            }
        }
        return;
    }

    /* ---- Enter: Send current input ---- */
    if (c == '\r' || c == '\n') {
        if (g_input_len > 0) {
            char line[BUFFER_SIZE];
            safe_copy(line, g_input_buf, sizeof(line));
            g_input_buf[0] = '\0';
            g_input_len = 0;
            g_cursor_pos = 0;
            g_scroll_offset = 0; /* Reset scroll to bottom */
            ui_redraw_input();
            handle_user_input(line);
        }
        return;
    }

    /* ---- Backspace: Delete character before cursor ---- */
    if (c == 127 || c == '\b') {
        if (g_cursor_pos > 0) {
            memmove(&g_input_buf[g_cursor_pos - 1], &g_input_buf[g_cursor_pos], g_input_len - g_cursor_pos + 1);
            g_cursor_pos--;
            g_input_len--;
            ui_redraw_input();
        }
        return;
    }

    /* ---- Tab: Auto-complete ---- */
    if (c == '\t') {
        handle_tab_completion();
        return;
    }

    /* ---- Ctrl+C: Safe exit ---- */
    if (c == 3) {
        ui_cleanup();
        printf("\nĐang thoát...\n");
        send_to_server(CMD_QUIT);
        close(g_sockfd);
        exit(EXIT_SUCCESS);
    }

    /* ---- Ctrl+L: Redraw full screen ---- */
    if (c == 12) {
        ui_redraw_all();
        return;
    }

    /* ---- Printable Character Insertion ---- */
    if ((unsigned char)c >= 32 && g_input_len < (int)sizeof(g_input_buf) - 2) {
        memmove(&g_input_buf[g_cursor_pos + 1], &g_input_buf[g_cursor_pos], g_input_len - g_cursor_pos + 1);
        g_input_buf[g_cursor_pos] = c;
        g_cursor_pos++;
        g_input_len++;
        ui_redraw_input();
    }
}

/* ============================================================
 * Legacy CLI Fallback & Help Messages
 * ============================================================ */
void print_prompt(void) {
    if (g_tui_enabled) {
        ui_redraw_input();
        return;
    }

    if (!g_logged_in) {
        printf("%s[chat (chưa đăng nhập)]> %s", COLOR_WHITE, COLOR_RESET);
    } else if (strlen(g_chat_partner) > 0) {
        printf("%s[%s -> %s%s%s]> %s", COLOR_CYAN, g_username, COLOR_BOLD, g_chat_partner, COLOR_CYAN, COLOR_RESET);
    } else {
        printf("%s[%s]> %s", COLOR_GREEN, g_username, COLOR_RESET);
    }
    fflush(stdout);
}

void show_help(void) {
    ui_add_message_fmt("%s=========== HƯỚNG DẪN SỬ DỤNG LỆNH CHAT ===========%s", COLOR_CYAN, COLOR_RESET);
    if (!g_logged_in) {
        ui_add_message_fmt("  %s/login <user> <pass>%s    : Đăng nhập vào hệ thống", COLOR_BOLD, COLOR_RESET);
        ui_add_message_fmt("  %s/register <user> <pass>%s : Đăng ký tài khoản mới", COLOR_BOLD, COLOR_RESET);
        ui_add_message_fmt("  %s/help%s                   : Hiển thị hướng dẫn này", COLOR_BOLD, COLOR_RESET);
        ui_add_message_fmt("  %s/quit%s                   : Thoát chương trình", COLOR_BOLD, COLOR_RESET);
    } else {
        ui_add_message_fmt("  %s/online%s                 : Xem danh sách người dùng đang online", COLOR_BOLD, COLOR_RESET);
        ui_add_message_fmt("  %s/msg <user> <tin_nhắn>%s  : Gửi tin nhắn 1-1 cho người khác", COLOR_BOLD, COLOR_RESET);
        ui_add_message_fmt("  %s/chat <user>%s            : Mở phiên chat 1-1 (sau đó chỉ cần gõ text để gửi)", COLOR_BOLD, COLOR_RESET);
        ui_add_message_fmt("  %s/endchat%s                : Thoát khỏi phiên chat 1-1", COLOR_BOLD, COLOR_RESET);
        ui_add_message_fmt("  %s/join <nhóm>%s            : Tham gia vào nhóm chat", COLOR_BOLD, COLOR_RESET);
        ui_add_message_fmt("  %s/leave <nhóm>%s           : Rời khỏi nhóm chat", COLOR_BOLD, COLOR_RESET);
        ui_add_message_fmt("  %s/group <nhóm> <tin_nhắn>%s: Gửi tin nhắn vào một nhóm", COLOR_BOLD, COLOR_RESET);
        ui_add_message_fmt("  %s/groups%s                 : Xem danh sách các nhóm đang hoạt động", COLOR_BOLD, COLOR_RESET);
        ui_add_message_fmt("  %s/members <nhóm>%s         : Xem danh sách thành viên trong nhóm", COLOR_BOLD, COLOR_RESET);
        ui_add_message_fmt("  %s/history <user/nhóm> [n]%s : Xem lịch sử tin nhắn lưu trữ từ SQLite3", COLOR_BOLD, COLOR_RESET);
        ui_add_message_fmt("  %s/sendfile <target> <file>%s: Gửi file đa nhiệm không chặn tới user hoặc nhóm", COLOR_BOLD, COLOR_RESET);
        ui_add_message_fmt("  %s/sendimg <target> <file>%s : Gửi ảnh nhị phân & hiển thị thumbnail trên terminal", COLOR_BOLD, COLOR_RESET);
        ui_add_message_fmt("  %s/logout%s                 : Đăng xuất tài khoản", COLOR_BOLD, COLOR_RESET);
        ui_add_message_fmt("  %s/help%s                   : Hiển thị lại hướng dẫn này", COLOR_BOLD, COLOR_RESET);
        ui_add_message_fmt("  %s/quit%s                   : Thoát chương trình", COLOR_BOLD, COLOR_RESET);
    }
    ui_add_message_fmt("%s==================================================%s", COLOR_CYAN, COLOR_RESET);
}

/* ============================================================
 * handle_server_line – Dispatch Server Events & Messages
 * ============================================================ */
void handle_server_line(char *line) {
    if (strlen(line) == 0) return;

    /* ---- Server shutdown notification ---- */
    if (strncmp(line, RESP_SERVER_SHUTDOWN " ", strlen(RESP_SERVER_SHUTDOWN) + 1) == 0) {
        char *msg = line + strlen(RESP_SERVER_SHUTDOWN) + 1;
        ui_add_message_fmt("%s[MÁY CHỦ] %s%s", COLOR_RED, msg, COLOR_RESET);
        return;
    }

    /* ---- Heartbeat: respond to server PING automatically ---- */
    if (strcmp(line, CMD_PING) == 0) {
        send_to_server(CMD_PONG);
        return;
    }

    /* ---- Parse message prefix ---- */
    if (strncmp(line, EVENT_ONLINE " ", strlen(EVENT_ONLINE) + 1) == 0) {
        char *user = line + strlen(EVENT_ONLINE) + 1;
        ui_add_online_user(user);
        ui_add_message_fmt("%s[TRỰC TUYẾN] Người dùng '%s' vừa đăng nhập.%s", COLOR_GREEN, user, COLOR_RESET);
    } else if (strncmp(line, EVENT_OFFLINE " ", strlen(EVENT_OFFLINE) + 1) == 0) {
        char *user = line + strlen(EVENT_OFFLINE) + 1;
        ui_remove_online_user(user);
        ui_add_message_fmt("%s[NGOẠI TUYẾN] Người dùng '%s' đã ngắt kết nối/rời mạng.%s", COLOR_YELLOW, user, COLOR_RESET);
        if (strcmp(g_chat_partner, user) == 0) {
            ui_add_message_fmt("%s[THÔNG BÁO] Bạn đã thoát phiên chat 1-1 vì đối phương đã offline.%s", COLOR_YELLOW, COLOR_RESET);
            g_chat_partner[0] = '\0';
            ui_update_status();
        }
    } else if (strncmp(line, RESP_ONLINE_LIST " ", strlen(RESP_ONLINE_LIST) + 1) == 0) {
        char *users = line + strlen(RESP_ONLINE_LIST) + 1;
        ui_update_online_users(users);
        ui_add_message_fmt("%s[DANH SÁCH ONLINE]: %s%s%s", COLOR_CYAN, COLOR_BOLD, strlen(users) > 0 ? users : "(không có ai khác)", COLOR_RESET);
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
            ui_add_message_fmt("%s[1-1 TỪ %s]:%s %s", COLOR_GREEN, sender, COLOR_RESET, msg);
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
                ui_add_message_fmt("%s[NHÓM %s] %s%s:%s %s", COLOR_MAGENTA, gname, COLOR_BOLD, sender, COLOR_RESET, msg);
            }
        }
    } else if (strncmp(line, EVENT_GROUP " ", strlen(EVENT_GROUP) + 1) == 0) {
        char ev[16] = {0};
        char gname[MAX_GROUP_NAME] = {0};
        char user[MAX_NAME_LEN] = {0};
        sscanf(line + strlen(EVENT_GROUP) + 1, "%15s %31s %31s", ev, gname, user);
        if (strcmp(ev, "JOIN") == 0) {
            ui_add_message_fmt("%s[NHÓM %s] '%s' vừa tham gia nhóm.%s", COLOR_CYAN, gname, user, COLOR_RESET);
            if (strcmp(user, g_username) == 0) {
                ui_add_group(gname);
            }
        } else {
            ui_add_message_fmt("%s[NHÓM %s] '%s' vừa rời khỏi nhóm.%s", COLOR_YELLOW, gname, user, COLOR_RESET);
            if (strcmp(user, g_username) == 0) {
                ui_remove_group(gname);
            }
        }
    } else if (strncmp(line, RESP_GROUPS_LIST " ", strlen(RESP_GROUPS_LIST) + 1) == 0) {
        char *groups = line + strlen(RESP_GROUPS_LIST) + 1;
        ui_update_groups(groups);
        ui_add_message_fmt("%s[DANH SÁCH NHÓM]: %s%s%s", COLOR_CYAN, COLOR_BOLD, strlen(groups) > 0 ? groups : "(chưa có nhóm nào)", COLOR_RESET);
    } else if (strncmp(line, RESP_MEMBERS_LIST " ", strlen(RESP_MEMBERS_LIST) + 1) == 0) {
        char *payload = line + strlen(RESP_MEMBERS_LIST) + 1;
        char gname[MAX_GROUP_NAME] = {0};
        char *p1 = strchr(payload, ' ');
        if (p1) {
            size_t glen = (size_t)(p1 - payload);
            if (glen >= sizeof(gname)) glen = sizeof(gname) - 1;
            strncpy(gname, payload, glen);
            gname[glen] = '\0';
            ui_add_message_fmt("%s[THÀNH VIÊN NHÓM %s]: %s%s%s", COLOR_CYAN, gname, COLOR_BOLD, p1 + 1, COLOR_RESET);
        } else {
            ui_add_message_fmt("%s[THÀNH VIÊN NHÓM %s]: (trống)%s", COLOR_CYAN, payload, COLOR_RESET);
        }
    } else if (strncmp(line, RESP_NOTICE " ", strlen(RESP_NOTICE) + 1) == 0) {
        char *notice = line + strlen(RESP_NOTICE) + 1;
        ui_add_message_fmt("%s[THÔNG BÁO] %s%s", COLOR_YELLOW, notice, COLOR_RESET);
        if (strstr(notice, "ended the 1-on-1 chat") || strstr(notice, "has disconnected")) {
            if (strlen(g_chat_partner) > 0) {
                g_chat_partner[0] = '\0';
                ui_update_status();
            }
        }
    } else if (strncmp(line, RESP_OK " LOGIN_OK ", strlen(RESP_OK " LOGIN_OK ")) == 0) {
        char user[MAX_NAME_LEN] = {0};
        sscanf(line + strlen(RESP_OK " LOGIN_OK "), "%31s", user);
        safe_copy(g_username, user, sizeof(g_username));
        g_logged_in = true;
        ui_add_online_user(g_username);
        ui_add_message_fmt("%s[THÀNH CÔNG] Đăng nhập thành công với tài khoản: %s%s%s", COLOR_GREEN, COLOR_BOLD, g_username, COLOR_RESET);
        ui_add_message_fmt("%sNhập /help để xem hướng dẫn các lệnh khả dụng.%s", COLOR_CYAN, COLOR_RESET);
        if (g_tui_enabled) {
            ui_redraw_header();
            ui_update_status();
            ui_redraw_input();
        }
    } else if (strncmp(line, RESP_OK " START_CHAT_OK ", strlen(RESP_OK " START_CHAT_OK ")) == 0) {
        char target[MAX_NAME_LEN] = {0};
        sscanf(line + strlen(RESP_OK " START_CHAT_OK "), "%31s", target);
        safe_copy(g_chat_partner, target, sizeof(g_chat_partner));
        ui_add_message_fmt("%s[CHẾ ĐỘ CHAT 1-1] Đã kết nối tới '%s'. Từ bây giờ, mọi tin nhắn gõ sẽ được gửi trực tiếp. Dùng /endchat để thoát.%s", COLOR_GREEN, target, COLOR_RESET);
        ui_update_status();
        if (g_tui_enabled) ui_redraw_sidebar();
    } else if (strncmp(line, RESP_OK " END_CHAT_OK", strlen(RESP_OK " END_CHAT_OK")) == 0) {
        g_chat_partner[0] = '\0';
        ui_add_message_fmt("%s[THÀNH CÔNG] Đã kết thúc phiên chat 1-1.%s", COLOR_GREEN, COLOR_RESET);
        ui_update_status();
        if (g_tui_enabled) ui_redraw_sidebar();
    } else if (strncmp(line, RESP_OK " Logged out", strlen(RESP_OK " Logged out")) == 0) {
        g_logged_in = false;
        g_username[0] = '\0';
        g_chat_partner[0] = '\0';
        ui_add_message_fmt("%s[THÀNH CÔNG] Đã đăng xuất khỏi hệ thống.%s", COLOR_GREEN, COLOR_RESET);
        if (g_tui_enabled) {
            ui_redraw_header();
            ui_update_status();
            ui_redraw_input();
        }
    } else if (strncmp(line, RESP_HISTORY_START " ", strlen(RESP_HISTORY_START) + 1) == 0) {
        char *target = line + strlen(RESP_HISTORY_START) + 1;
        ui_add_message_fmt("%s==== LỊCH SỬ TIN NHẮN VỚI '%s' ====%s", COLOR_CYAN, target, COLOR_RESET);
    } else if (strncmp(line, RESP_HISTORY_ITEM " ", strlen(RESP_HISTORY_ITEM) + 1) == 0) {
        char *payload = line + strlen(RESP_HISTORY_ITEM) + 1;
        char date[16] = {0}, time_str[16] = {0}, sender[MAX_NAME_LEN] = {0};
        int offset = 0;
        if (sscanf(payload, "%15s %15s %31s%n", date, time_str, sender, &offset) >= 3) {
            char *msg = trim_whitespace(payload + offset);
            ui_add_message_fmt("%s[%s %s] %s%s:%s %s", COLOR_BLUE, date, time_str, COLOR_BOLD, sender, COLOR_RESET, msg);
        } else {
            ui_add_message_fmt("%s[LỊCH SỬ] %s%s", COLOR_BLUE, payload, COLOR_RESET);
        }
    } else if (strncmp(line, RESP_HISTORY_END " ", strlen(RESP_HISTORY_END) + 1) == 0) {
        char target[MAX_NAME_LEN] = {0};
        int count = 0;
        sscanf(line + strlen(RESP_HISTORY_END) + 1, "%31s %d", target, &count);
        ui_add_message_fmt("%s==== Đã tải %d tin nhắn lịch sử của '%s' ====%s", COLOR_CYAN, count, target, COLOR_RESET);
    } else if (strncmp(line, RESP_OFFLINE_START " ", strlen(RESP_OFFLINE_START) + 1) == 0) {
        int count = atoi(line + strlen(RESP_OFFLINE_START) + 1);
        ui_add_message_fmt("%s[HỘP THƯ NGOẠI TUYẾN] Bạn có %d tin nhắn mới khi vắng mặt:%s", COLOR_YELLOW, count, COLOR_RESET);
    } else if (strncmp(line, RESP_OFFLINE_MSG " ", strlen(RESP_OFFLINE_MSG) + 1) == 0) {
        char *payload = line + strlen(RESP_OFFLINE_MSG) + 1;
        char sender[MAX_NAME_LEN] = {0}, date[16] = {0}, time_str[16] = {0};
        int offset = 0;
        if (sscanf(payload, "%31s %15s %15s%n", sender, date, time_str, &offset) >= 3) {
            char *msg = trim_whitespace(payload + offset);
            ui_add_message_fmt("%s[NGOẠI TUYẾN - %s %s] %s%s:%s %s", COLOR_YELLOW, date, time_str, COLOR_BOLD, sender, COLOR_RESET, msg);
        } else {
            ui_add_message_fmt("%s[NGOẠI TUYẾN] %s%s", COLOR_YELLOW, payload, COLOR_RESET);
        }
    } else if (strcmp(line, RESP_OFFLINE_END) == 0) {
        ui_add_message_fmt("%s--- Đã nạp xong toàn bộ tin nhắn ngoại tuyến ---%s", COLOR_YELLOW, COLOR_RESET);
    } else if (strncmp(line, EVENT_TYPING " ", strlen(EVENT_TYPING) + 1) == 0) {
        char *sender = line + strlen(EVENT_TYPING) + 1;
        ui_add_message_fmt("%s... '%s' đang soạn tin nhắn ...%s", COLOR_BLUE, sender, COLOR_RESET);
    } else if (strncmp(line, RESP_OK " " RESP_FILE_READY " ", strlen(RESP_OK " " RESP_FILE_READY " ")) == 0) {
        char transfer_id[MAX_TRANSFER_ID_LEN] = {0};
        int chunk_size = MAX_CHUNK_SIZE;
        sscanf(line + strlen(RESP_OK " " RESP_FILE_READY " "), "%31s %d", transfer_id, &chunk_size);
        on_file_ready(transfer_id, chunk_size);
    } else if (strncmp(line, RESP_FILE_INCOMING " ", strlen(RESP_FILE_INCOMING) + 1) == 0) {
        char transfer_id[MAX_TRANSFER_ID_LEN] = {0};
        char sender[MAX_NAME_LEN] = {0};
        char filename[MAX_FILENAME_LEN] = {0};
        long filesize = 0;
        char sha256[SHA256_HEX_LEN + 1] = {0};
        int is_image = 0;
        sscanf(line + strlen(RESP_FILE_INCOMING) + 1, "%31s %31s %127s %ld %64s %d",
               transfer_id, sender, filename, &filesize, sha256, &is_image);
        on_file_incoming(transfer_id, sender, filename, filesize, sha256, is_image != 0);
    } else if (strncmp(line, RESP_FILE_CHUNK " ", strlen(RESP_FILE_CHUNK) + 1) == 0) {
        char transfer_id[MAX_TRANSFER_ID_LEN] = {0};
        int seq_num = 0;
        int offset = 0;
        char *payload = line + strlen(RESP_FILE_CHUNK) + 1;
        if (sscanf(payload, "%31s %d%n", transfer_id, &seq_num, &offset) >= 2) {
            char *b64_data = trim_whitespace(payload + offset);
            on_file_chunk(transfer_id, seq_num, b64_data);
        }
    } else if (strncmp(line, RESP_FILE_DONE " ", strlen(RESP_FILE_DONE) + 1) == 0) {
        char transfer_id[MAX_TRANSFER_ID_LEN] = {0};
        char sha256[SHA256_HEX_LEN + 1] = {0};
        sscanf(line + strlen(RESP_FILE_DONE) + 1, "%31s %64s", transfer_id, sha256);
        on_file_done(transfer_id, sha256);
    } else if (strncmp(line, RESP_OK, strlen(RESP_OK)) == 0) {
        char *detail = line + strlen(RESP_OK);
        if (*detail == ' ') detail++;
        ui_add_message_fmt("%s[THÀNH CÔNG] %s%s", COLOR_GREEN, detail, COLOR_RESET);
    } else if (strncmp(line, RESP_ERR, strlen(RESP_ERR)) == 0) {
        char *detail = line + strlen(RESP_ERR);
        if (*detail == ' ') detail++;
        ui_add_message_fmt("%s[LỖI] %s%s", COLOR_RED, detail, COLOR_RESET);
    } else {
        ui_add_message_fmt("[Server]: %s", line);
    }
}

/* ============================================================
 * handle_user_input – Process a line typed by user
 * ============================================================ */
void handle_user_input(char *raw_input) {
    char *input = trim_whitespace(raw_input);
    if (strlen(input) == 0) return;

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
            return;
        }

        if (strcasecmp(cmd, "/quit") == 0 || strcasecmp(cmd, "/exit") == 0) {
            ui_cleanup();
            printf("%sTạm biệt! Đang ngắt kết nối...%s\n", COLOR_YELLOW, COLOR_RESET);
            send_to_server(CMD_QUIT);
            close(g_sockfd);
            exit(EXIT_SUCCESS);
        }

        if (strcasecmp(cmd, "/login") == 0) {
            char user[MAX_NAME_LEN] = {0};
            char pass[MAX_PASS_LEN] = {0};
            if (sscanf(rest, "%31s %63s", user, pass) != 2) {
                ui_add_message_fmt("%s[LỖI] Cú pháp: /login <username> <password>%s", COLOR_RED, COLOR_RESET);
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
                ui_add_message_fmt("%s[LỖI] Cú pháp: /register <username> <password>%s", COLOR_RED, COLOR_RESET);
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
                ui_add_message_fmt("%s[LỖI] Cú pháp: /msg <username> <tin_nhắn>%s", COLOR_RED, COLOR_RESET);
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

            /* Local echo in chat window */
            ui_add_message_fmt("%s[1-1 TỚI %s]:%s %s", COLOR_CYAN, target, COLOR_RESET, msg);
            return;
        }

        if (strcasecmp(cmd, "/chat") == 0) {
            char target[MAX_NAME_LEN] = {0};
            if (sscanf(rest, "%31s", target) != 1) {
                ui_add_message_fmt("%s[LỖI] Cú pháp: /chat <username>%s", COLOR_RED, COLOR_RESET);
                return;
            }
            char buf[64];
            snprintf(buf, sizeof(buf), "%s %s", CMD_CHAT, target);
            send_to_server(buf);
            return;
        }

        if (strcasecmp(cmd, "/endchat") == 0) {
            send_to_server(CMD_ENDCHAT);
            return;
        }

        if (strcasecmp(cmd, "/join") == 0) {
            char group[MAX_GROUP_NAME] = {0};
            if (sscanf(rest, "%31s", group) != 1) {
                ui_add_message_fmt("%s[LỖI] Cú pháp: /join <tên_nhóm>%s", COLOR_RED, COLOR_RESET);
                return;
            }
            char buf[64];
            snprintf(buf, sizeof(buf), "%s %s", CMD_JOIN, group);
            send_to_server(buf);
            return;
        }

        if (strcasecmp(cmd, "/leave") == 0) {
            char group[MAX_GROUP_NAME] = {0};
            if (sscanf(rest, "%31s", group) != 1) {
                ui_add_message_fmt("%s[LỖI] Cú pháp: /leave <tên_nhóm>%s", COLOR_RED, COLOR_RESET);
                return;
            }
            char buf[64];
            snprintf(buf, sizeof(buf), "%s %s", CMD_LEAVE, group);
            send_to_server(buf);
            return;
        }

        if (strcasecmp(cmd, "/group") == 0) {
            char group[MAX_GROUP_NAME] = {0};
            char *msg = NULL;
            char *space_pos = strchr(rest, ' ');
            if (!space_pos) {
                ui_add_message_fmt("%s[LỖI] Cú pháp: /group <tên_nhóm> <tin_nhắn>%s", COLOR_RED, COLOR_RESET);
                return;
            }
            size_t glen = (size_t)(space_pos - rest);
            if (glen >= sizeof(group)) glen = sizeof(group) - 1;
            strncpy(group, rest, glen);
            group[glen] = '\0';
            msg = trim_whitespace(space_pos + 1);

            char buf[BUFFER_SIZE];
            snprintf(buf, sizeof(buf), "%s %s %s", CMD_GROUP_MSG, group, msg);
            send_to_server(buf);

            /* Local echo in chat window */
            ui_add_message_fmt("%s[GỬI TỚI NHÓM %s]:%s %s", COLOR_MAGENTA, group, COLOR_RESET, msg);
            return;
        }

        if (strcasecmp(cmd, "/groups") == 0) {
            send_to_server(CMD_GROUPS);
            return;
        }

        if (strcasecmp(cmd, "/members") == 0) {
            char group[MAX_GROUP_NAME] = {0};
            if (sscanf(rest, "%31s", group) != 1) {
                ui_add_message_fmt("%s[LỖI] Cú pháp: /members <tên_nhóm>%s", COLOR_RED, COLOR_RESET);
                return;
            }
            char buf[64];
            snprintf(buf, sizeof(buf), "%s %s", CMD_MEMBERS, group);
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
                    ui_add_message_fmt("%s[LỖI] Cú pháp: /history <username/tên_nhóm> [số_lượng]%s", COLOR_RED, COLOR_RESET);
                    return;
                }
            }
            char buf[128];
            snprintf(buf, sizeof(buf), "%s %s %d", CMD_HISTORY, target, limit);
            send_to_server(buf);
            return;
        }

        if (strcasecmp(cmd, "/sendfile") == 0) {
            char target[MAX_NAME_LEN] = {0};
            char filepath[256] = {0};
            char *space_pos = strchr(rest, ' ');
            if (space_pos) {
                size_t tlen = (size_t)(space_pos - rest);
                if (tlen >= sizeof(target)) tlen = sizeof(target) - 1;
                strncpy(target, rest, tlen);
                target[tlen] = '\0';
                strncpy(filepath, trim_whitespace(space_pos + 1), sizeof(filepath) - 1);
            } else if (strlen(g_chat_partner) > 0) {
                safe_copy(target, g_chat_partner, sizeof(target));
                strncpy(filepath, rest, sizeof(filepath) - 1);
            } else {
                ui_add_message_fmt("%s[LỖI] Cú pháp: /sendfile <user/nhóm> <đường_dẫn_file>%s", COLOR_RED, COLOR_RESET);
                return;
            }
            start_file_send(target, filepath, false);
            return;
        }

        if (strcasecmp(cmd, "/sendimg") == 0) {
            char target[MAX_NAME_LEN] = {0};
            char filepath[256] = {0};
            char *space_pos = strchr(rest, ' ');
            if (space_pos) {
                size_t tlen = (size_t)(space_pos - rest);
                if (tlen >= sizeof(target)) tlen = sizeof(target) - 1;
                strncpy(target, rest, tlen);
                target[tlen] = '\0';
                strncpy(filepath, trim_whitespace(space_pos + 1), sizeof(filepath) - 1);
            } else if (strlen(g_chat_partner) > 0) {
                safe_copy(target, g_chat_partner, sizeof(target));
                strncpy(filepath, rest, sizeof(filepath) - 1);
            } else {
                ui_add_message_fmt("%s[LỖI] Cú pháp: /sendimg <user/nhóm> <đường_dẫn_ảnh>%s", COLOR_RED, COLOR_RESET);
                return;
            }
            start_file_send(target, filepath, true);
            return;
        }

        ui_add_message_fmt("%s[LỖI] Lệnh '%s' không tồn tại. Gõ /help để xem danh sách lệnh.%s", COLOR_RED, cmd, COLOR_RESET);
        return;
    }

    /* Not starting with slash: direct message in active 1-1 chat */
    if (strlen(g_chat_partner) > 0) {
        char buf[BUFFER_SIZE];
        snprintf(buf, sizeof(buf), "%s %s %s", CMD_MSG, g_chat_partner, input);
        send_to_server(buf);
        /* Local echo */
        ui_add_message_fmt("%s[TÔI -> %s]:%s %s", COLOR_GREEN, g_chat_partner, COLOR_RESET, input);
    } else {
        ui_add_message_fmt("%s[HƯỚNG DẪN] Bạn chưa ở trong phiên chat 1-1. Hãy dùng:%s", COLOR_YELLOW, COLOR_RESET);
        ui_add_message_fmt("  - %s/msg <user> <tin nhắn>%s để gửi tin lẻ", COLOR_BOLD, COLOR_RESET);
        ui_add_message_fmt("  - %s/chat <user>%s để vào phòng chat riêng 1-1", COLOR_BOLD, COLOR_RESET);
        ui_add_message_fmt("  - %s/help%s để xem tất cả các lệnh", COLOR_BOLD, COLOR_RESET);
    }
}
