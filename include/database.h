#ifndef DATABASE_H
#define DATABASE_H

#include <stdbool.h>
#include <sqlite3.h>
#include "protocol.h"

#define DB_DEFAULT_PATH "storage/chat_server.db"

/* ============================================================
 * SQLite3 Database API (Task 2 & Task 3)
 * ============================================================ */

/* Global database handle */
extern sqlite3 *g_db;

/* Database lifecycle */
int  db_init(const char *db_path);
void db_close(void);

/* User & Authentication management (Task 2.1, 2.2, 2.3) */
bool db_user_exists(const char *username);
int  db_register_user(const char *username, const char *password);
bool db_authenticate_user(const char *username, const char *password);
void db_update_last_seen(const char *username);

/* Seed sample accounts (alice, bob, charlie, david / 123456) */
void db_seed_sample_users(void);

/* ============================================================
 * Message & History Management (Task 3.2, 3.4)
 * ============================================================ */
typedef struct {
    int  id;
    char sender[MAX_NAME_LEN];
    char receiver[MAX_NAME_LEN];
    char group_name[MAX_GROUP_NAME];
    char content[MAX_MSG_LEN];
    char timestamp[32];
} MessageRecord;

int  db_save_message(const char *sender, const char *receiver, const char *group_name,
                     const char *content, const char *msg_type, int is_delivered);
int  db_get_history_1on1(const char *user1, const char *user2, int limit,
                         MessageRecord *records, int max_records);
int  db_get_history_group(const char *group_name, int limit,
                          MessageRecord *records, int max_records);
int  db_get_offline_messages(const char *username, MessageRecord *records, int max_records);
int  db_mark_messages_delivered(const char *username);

/* ============================================================
 * Group & Membership Persistence (Task 3.3)
 * ============================================================ */
int  db_create_group(const char *group_name);
bool db_group_exists(const char *group_name);
int  db_add_group_member(const char *group_name, const char *username);
int  db_remove_group_member(const char *group_name, const char *username);
bool db_is_group_member(const char *group_name, const char *username);
int  db_get_groups(char *out_buf, size_t buf_size);
int  db_get_group_members(const char *group_name, char *out_buf, size_t buf_size);
int  db_load_groups_to_memory(void);

#endif /* DATABASE_H */
