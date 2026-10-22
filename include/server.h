#ifndef SERVER_H
#define SERVER_H

#include "protocol.h"
#include <stdbool.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>

/* ============================================================
 * Account representation (flat-file based, to be replaced by
 * SQLite3 in Milestone 2)
 * ============================================================ */
typedef struct {
    char username[MAX_NAME_LEN];
    char password[MAX_PASS_LEN];
} Account;

#define MAX_ACCOUNTS 256

/* ============================================================
 * Client session – one per connected TCP socket
 * ============================================================ */
typedef struct {
    int         sockfd;
    struct sockaddr_in addr;
    bool        is_auth;
    char        username[MAX_NAME_LEN];
    char        active_partner[MAX_NAME_LEN];   /* 1-on-1 chat partner */
    char        recv_buf[BUFFER_SIZE];
    int         recv_len;

    /* Heartbeat / Keep-alive fields (Task 1.4) */
    time_t      last_active;        /* Timestamp of last received data         */
    bool        ping_pending;       /* True if a PING was sent, awaiting PONG  */
} ClientSession;

/* ============================================================
 * Group representation (runtime, to be backed by DB later)
 * ============================================================ */
typedef struct {
    char  name[MAX_GROUP_NAME];
    bool  in_use;
    char  members[MAX_GROUP_MEMBERS][MAX_NAME_LEN];
    int   member_count;
} Group;

/* ============================================================
 * Global state (extern declarations)
 * ============================================================ */
extern Account       g_accounts[MAX_ACCOUNTS];
extern int           g_account_count;
extern ClientSession g_clients[MAX_CLIENTS];
extern Group         g_groups[MAX_GROUPS];
extern volatile sig_atomic_t g_running;   /* Graceful shutdown flag */

/* ============================================================
 * session.c – Session & connection management
 * ============================================================ */
void   init_sessions(void);
ClientSession* find_client_by_name(const char *username);
void   send_to_fd(int fd, const char *fmt, ...);
void   broadcast_presence(const char *username, bool online, int exclude_fd);
void   disconnect_client(int index);

/* ============================================================
 * router.c – Command routing & chat logic
 * ============================================================ */
void   handle_command(ClientSession *session, char *cmd_line);

/* ============================================================
 * Account helpers (defined in session.c for now)
 * ============================================================ */
void   load_accounts(void);
void   save_account(const char *user, const char *pass);
bool   user_exists(const char *user);
bool   authenticate_user(const char *user, const char *pass);

/* ============================================================
 * Group helpers (defined in router.c)
 * ============================================================ */
Group* find_group(const char *name);
Group* find_or_create_group(const char *name);
bool   is_group_member(Group *grp, const char *username);
bool   add_to_group(Group *grp, const char *username);
bool   remove_from_group(Group *grp, const char *username);
void   remove_client_from_all_groups(const char *username);

/* ============================================================
 * Utility helpers
 * ============================================================ */
void   safe_copy(char *dst, const char *src, size_t dst_size);
char*  trim_whitespace(char *str);

#endif /* SERVER_H */
