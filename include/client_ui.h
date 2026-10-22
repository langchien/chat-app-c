#ifndef CLIENT_UI_H
#define CLIENT_UI_H

#include "protocol.h"
#include <stdbool.h>

/* ANSI Color codes for terminal display */
#define COLOR_RESET   "\033[0m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_WHITE   "\033[37m"

/* Client-side global state */
extern int   g_sockfd;
extern char  g_username[MAX_NAME_LEN];
extern char  g_chat_partner[MAX_NAME_LEN];
extern bool  g_logged_in;

/* UI functions (ui_continuous.c – currently basic CLI) */
void   print_prompt(void);
void   show_help(void);
void   handle_server_line(char *line);
void   handle_user_input(char *raw_input);

/* Network helpers (net_handler.c) */
void   send_to_server(const char *cmd);

#endif /* CLIENT_UI_H */
