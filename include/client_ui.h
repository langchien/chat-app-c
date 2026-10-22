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

/* ============================================================
 * Client-side File & Image Transfer (Task 4.2, 4.3)
 * ============================================================ */
typedef struct {
    bool    active;
    char    transfer_id[MAX_TRANSFER_ID_LEN];
    char    target[MAX_NAME_LEN];
    char    filepath[256];
    char    filename[MAX_FILENAME_LEN];
    FILE   *fp;
    long    filesize;
    long    bytes_sent;
    int     seq_num;
    int     chunk_size;
    char    sha256[65];
    bool    is_image;
} FileSendJob;

typedef struct {
    bool    active;
    char    transfer_id[MAX_TRANSFER_ID_LEN];
    char    sender[MAX_NAME_LEN];
    char    filename[MAX_FILENAME_LEN];
    char    filepath_part[256];
    char    filepath_final[256];
    FILE   *fp;
    long    expected_size;
    long    received_bytes;
    int     seq_num;
    char    expected_sha256[65];
    bool    is_image;
} FileRecvJob;

extern FileSendJob g_send_job;
extern FileRecvJob g_recv_job;

void start_file_send(const char *target, const char *filepath, bool is_image);
void send_next_file_chunk(void);
void on_file_ready(const char *transfer_id, int chunk_size);
void on_file_incoming(const char *transfer_id, const char *sender, const char *filename,
                      long filesize, const char *sha256, bool is_image);
void on_file_chunk(const char *transfer_id, int seq_num, const char *b64_data);
void on_file_done(const char *transfer_id, const char *sha256);
void render_terminal_image(const char *filepath);

#endif /* CLIENT_UI_H */
