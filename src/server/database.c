/* ============================================================
 * src/server/database.c
 * SQLite3 Database integration for Chat Application.
 *
 * Task 2.1: Schema design (users, groups, group_members,
 *           messages, file_transfers), WAL journal mode,
 *           sample accounts initialization.
 * Task 2.2: 100% Prepared Statements (sqlite3_prepare_v2,
 *           sqlite3_bind_text) to eliminate SQL Injection.
 * Task 2.3: Session authentication, duplicate check support,
 *           and last_seen updating.
 * ============================================================ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "../../include/database.h"
#include "../../include/crypto_utils.h"
#include "../../include/protocol.h"
#include "../../include/server.h"

/* Global SQLite3 database handle */
sqlite3 *g_db = NULL;

/* Ensure directory exists before creating/opening the DB */
static void ensure_parent_dir(const char *path) {
    char dir[256];
    safe_copy(dir, path, sizeof(dir));
    char *last_slash = strrchr(dir, '/');
    if (!last_slash) {
        last_slash = strrchr(dir, '\\');
    }
    if (last_slash) {
        *last_slash = '\0';
#if defined(_WIN32)
        mkdir(dir);
#else
        mkdir(dir, 0755);
#endif
    }
}

/* ============================================================
 * db_init – Initialize database connection, pragmas, and schema
 * ============================================================ */
int db_init(const char *db_path) {
    if (!db_path) db_path = DB_DEFAULT_PATH;

    ensure_parent_dir(db_path);

    int rc = sqlite3_open(db_path, &g_db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[Database] Cannot open database '%s': %s\n",
                db_path, sqlite3_errmsg(g_db));
        if (g_db) {
            sqlite3_close(g_db);
            g_db = NULL;
        }
        return -1;
    }

    char *err_msg = NULL;

    /* Task 2.1: Enable WAL mode for high concurrent performance */
    rc = sqlite3_exec(g_db, "PRAGMA journal_mode=WAL;", NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[Database] Failed to set WAL mode: %s\n", err_msg);
        sqlite3_free(err_msg);
    }

    sqlite3_exec(g_db, "PRAGMA synchronous=NORMAL;", NULL, NULL, NULL);
    sqlite3_exec(g_db, "PRAGMA foreign_keys=ON;", NULL, NULL, NULL);

    /* Schema definition (Task 2.1) */
    const char *schema_sql =
        "CREATE TABLE IF NOT EXISTS users ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    username TEXT UNIQUE NOT NULL COLLATE NOCASE,"
        "    password_hash TEXT NOT NULL,"
        "    salt TEXT NOT NULL,"
        "    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "    last_seen DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");"
        "CREATE TABLE IF NOT EXISTS groups ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    name TEXT UNIQUE NOT NULL COLLATE NOCASE,"
        "    created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");"
        "CREATE TABLE IF NOT EXISTS group_members ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    group_name TEXT NOT NULL,"
        "    username TEXT NOT NULL,"
        "    joined_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "    UNIQUE(group_name, username)"
        ");"
        "CREATE TABLE IF NOT EXISTS messages ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    sender TEXT NOT NULL,"
        "    receiver TEXT,"
        "    group_name TEXT,"
        "    content TEXT NOT NULL,"
        "    msg_type TEXT DEFAULT 'TEXT',"
        "    is_delivered INTEGER DEFAULT 1,"
        "    timestamp DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");"
        "CREATE TABLE IF NOT EXISTS file_transfers ("
        "    transfer_id TEXT PRIMARY KEY,"
        "    sender TEXT NOT NULL,"
        "    receiver TEXT,"
        "    filename TEXT NOT NULL,"
        "    filesize INTEGER NOT NULL,"
        "    sha256 TEXT,"
        "    status TEXT DEFAULT 'PENDING',"
        "    created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_users_username ON users(username);"
        "CREATE INDEX IF NOT EXISTS idx_group_members ON group_members(group_name, username);"
        "CREATE INDEX IF NOT EXISTS idx_messages_sender ON messages(sender);"
        "CREATE INDEX IF NOT EXISTS idx_messages_receiver ON messages(receiver);"
        "CREATE INDEX IF NOT EXISTS idx_messages_group ON messages(group_name);";

    rc = sqlite3_exec(g_db, schema_sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[Database] Failed to create tables: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(g_db);
        g_db = NULL;
        return -1;
    }

    printf("[Database] SQLite3 initialized successfully (%s, WAL mode)\n", db_path);

    /* Seed default sample accounts (alice, bob, charlie, david) */
    db_seed_sample_users();

    return 0;
}

/* ============================================================
 * db_close – Safely close SQLite3 database connection
 * ============================================================ */
void db_close(void) {
    if (g_db) {
        sqlite3_close(g_db);
        g_db = NULL;
        printf("[Database] SQLite3 database connection closed.\n");
    }
}

/* ============================================================
 * db_user_exists – Check if username exists (Prepared Statement)
 * ============================================================ */
bool db_user_exists(const char *username) {
    if (!g_db || !username) return false;

    char clean_user[MAX_NAME_LEN];
    safe_copy(clean_user, username, sizeof(clean_user));
    char *trimmed_user = trim_whitespace(clean_user);
    if (strlen(trimmed_user) == 0) return false;

    const char *sql = "SELECT 1 FROM users WHERE username = ? LIMIT 1;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, trimmed_user, -1, SQLITE_STATIC);
    bool exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);

    return exists;
}

/* ============================================================
 * db_register_user – Register user with Salt + SHA-256
 * Uses Prepared Statements to eliminate SQL injection.
 * Returns 0 on success, 1 if user exists, -1 on error.
 * ============================================================ */
int db_register_user(const char *username, const char *password) {
    if (!g_db || !username || !password) return -1;

    char clean_user[MAX_NAME_LEN];
    safe_copy(clean_user, username, sizeof(clean_user));
    char *trimmed_user = trim_whitespace(clean_user);

    if (strlen(trimmed_user) == 0 || strlen(password) == 0) return -1;
    if (db_user_exists(trimmed_user)) return 1;

    char salt[SALT_HEX_LEN + 1];
    char hash[SHA256_HEX_LEN + 1];
    generate_salt_hex(salt);
    hash_password(password, salt, hash);

    const char *sql = "INSERT INTO users (username, password_hash, salt, created_at, last_seen) "
                      "VALUES (?, ?, ?, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP);";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "[Database] Prepare insert error: %s\n", sqlite3_errmsg(g_db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, trimmed_user, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, hash, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, salt, -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return (rc == SQLITE_DONE) ? 0 : -1;
}

/* ============================================================
 * db_authenticate_user – Verify user credentials
 * Uses Prepared Statements and constant-time SHA-256 verification.
 * ============================================================ */
bool db_authenticate_user(const char *username, const char *password) {
    if (!g_db || !username || !password) return false;

    char clean_user[MAX_NAME_LEN];
    safe_copy(clean_user, username, sizeof(clean_user));
    char *trimmed_user = trim_whitespace(clean_user);
    if (strlen(trimmed_user) == 0) return false;

    const char *sql = "SELECT password_hash, salt FROM users WHERE username = ? LIMIT 1;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "[Database] Prepare select error: %s\n", sqlite3_errmsg(g_db));
        return false;
    }

    sqlite3_bind_text(stmt, 1, trimmed_user, -1, SQLITE_STATIC);

    bool authenticated = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *db_hash = (const char*)sqlite3_column_text(stmt, 0);
        const char *salt = (const char*)sqlite3_column_text(stmt, 1);
        if (db_hash && salt) {
            authenticated = verify_password(password, salt, db_hash);
        }
    }
    sqlite3_finalize(stmt);

    if (authenticated) {
        db_update_last_seen(trimmed_user);
    }

    return authenticated;
}

/* ============================================================
 * db_update_last_seen – Update last_seen timestamp
 * ============================================================ */
void db_update_last_seen(const char *username) {
    if (!g_db || !username) return;

    char clean_user[MAX_NAME_LEN];
    safe_copy(clean_user, username, sizeof(clean_user));
    char *trimmed_user = trim_whitespace(clean_user);
    if (strlen(trimmed_user) == 0) return;

    const char *sql = "UPDATE users SET last_seen = CURRENT_TIMESTAMP WHERE username = ?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return;
    }

    sqlite3_bind_text(stmt, 1, trimmed_user, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

/* ============================================================
 * db_seed_sample_users – Seed sample accounts if not existing
 * ============================================================ */
void db_seed_sample_users(void) {
    const char *sample_users[] = {"alice", "bob", "charlie", "david"};
    const char *default_pass = "123456";
    int count = (int)(sizeof(sample_users) / sizeof(sample_users[0]));

    for (int i = 0; i < count; i++) {
        if (!db_user_exists(sample_users[i])) {
            if (db_register_user(sample_users[i], default_pass) == 0) {
                printf("[Database] Seeded default account: %s / %s (SHA-256 + Salt)\n",
                       sample_users[i], default_pass);
            }
        }
    }
}
