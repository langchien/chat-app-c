#ifndef DATABASE_H
#define DATABASE_H

#include <stdbool.h>
#include <sqlite3.h>

#define DB_DEFAULT_PATH "storage/chat_server.db"

/* ============================================================
 * SQLite3 Database API (Task 2.1)
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

#endif /* DATABASE_H */
