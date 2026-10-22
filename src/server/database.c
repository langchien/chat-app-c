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

    /* Load persistent groups into runtime memory (Task 3.3) */
    db_load_groups_to_memory();

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

/* ============================================================
 * Message & History Management (Task 3.2, 3.4)
 * ============================================================ */

/* Save a text or media message into SQLite with Prepared Statements */
int db_save_message(const char *sender, const char *receiver, const char *group_name,
                    const char *content, const char *msg_type, int is_delivered) {
    if (!g_db || !sender || !content) return -1;

    const char *sql = "INSERT INTO messages (sender, receiver, group_name, content, msg_type, is_delivered, timestamp) "
                      "VALUES (?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP);";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "[Database] Prepare insert message error: %s\n", sqlite3_errmsg(g_db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, sender, -1, SQLITE_STATIC);
    if (receiver && strlen(receiver) > 0) {
        sqlite3_bind_text(stmt, 2, receiver, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 2);
    }

    if (group_name && strlen(group_name) > 0) {
        sqlite3_bind_text(stmt, 3, group_name, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 3);
    }

    sqlite3_bind_text(stmt, 4, content, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, msg_type ? msg_type : "TEXT", -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 6, is_delivered);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return (rc == SQLITE_DONE) ? 0 : -1;
}

/* Retrieve 1-on-1 chat history between user1 and user2 in chronological order */
int db_get_history_1on1(const char *user1, const char *user2, int limit,
                        MessageRecord *records, int max_records) {
    if (!g_db || !user1 || !user2 || !records || max_records <= 0) return 0;
    if (limit <= 0 || limit > max_records) limit = max_records;

    const char *sql = "SELECT id, sender, receiver, content, timestamp FROM messages "
                      "WHERE ((sender = ? AND receiver = ?) OR (sender = ? AND receiver = ?)) "
                      "ORDER BY id DESC LIMIT ?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return 0;
    }

    sqlite3_bind_text(stmt, 1, user1, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, user2, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, user2, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, user1, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 5, limit);

    int count = 0;
    MessageRecord *temp = malloc(sizeof(MessageRecord) * max_records);
    if (!temp) {
        sqlite3_finalize(stmt);
        return 0;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW && count < max_records) {
        temp[count].id = sqlite3_column_int(stmt, 0);
        const char *s = (const char*)sqlite3_column_text(stmt, 1);
        const char *r = (const char*)sqlite3_column_text(stmt, 2);
        const char *c = (const char*)sqlite3_column_text(stmt, 3);
        const char *t = (const char*)sqlite3_column_text(stmt, 4);

        safe_copy(temp[count].sender, s ? s : "", sizeof(temp[count].sender));
        safe_copy(temp[count].receiver, r ? r : "", sizeof(temp[count].receiver));
        temp[count].group_name[0] = '\0';
        safe_copy(temp[count].content, c ? c : "", sizeof(temp[count].content));
        safe_copy(temp[count].timestamp, t ? t : "", sizeof(temp[count].timestamp));
        count++;
    }
    sqlite3_finalize(stmt);

    /* Reverse from descending (newest first) to ascending (oldest first) */
    for (int i = 0; i < count; i++) {
        records[i] = temp[count - 1 - i];
    }
    free(temp);
    return count;
}

/* Retrieve group chat history in chronological order */
int db_get_history_group(const char *group_name, int limit,
                         MessageRecord *records, int max_records) {
    if (!g_db || !group_name || !records || max_records <= 0) return 0;
    if (limit <= 0 || limit > max_records) limit = max_records;

    const char *sql = "SELECT id, sender, group_name, content, timestamp FROM messages "
                      "WHERE group_name = ? "
                      "ORDER BY id DESC LIMIT ?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return 0;
    }

    sqlite3_bind_text(stmt, 1, group_name, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, limit);

    int count = 0;
    MessageRecord *temp = malloc(sizeof(MessageRecord) * max_records);
    if (!temp) {
        sqlite3_finalize(stmt);
        return 0;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW && count < max_records) {
        temp[count].id = sqlite3_column_int(stmt, 0);
        const char *s = (const char*)sqlite3_column_text(stmt, 1);
        const char *g = (const char*)sqlite3_column_text(stmt, 2);
        const char *c = (const char*)sqlite3_column_text(stmt, 3);
        const char *t = (const char*)sqlite3_column_text(stmt, 4);

        safe_copy(temp[count].sender, s ? s : "", sizeof(temp[count].sender));
        temp[count].receiver[0] = '\0';
        safe_copy(temp[count].group_name, g ? g : "", sizeof(temp[count].group_name));
        safe_copy(temp[count].content, c ? c : "", sizeof(temp[count].content));
        safe_copy(temp[count].timestamp, t ? t : "", sizeof(temp[count].timestamp));
        count++;
    }
    sqlite3_finalize(stmt);

    for (int i = 0; i < count; i++) {
        records[i] = temp[count - 1 - i];
    }
    free(temp);
    return count;
}

/* Retrieve offline messages for a user (is_delivered = 0) */
int db_get_offline_messages(const char *username, MessageRecord *records, int max_records) {
    if (!g_db || !username || !records || max_records <= 0) return 0;

    const char *sql = "SELECT id, sender, content, timestamp FROM messages "
                      "WHERE receiver = ? AND is_delivered = 0 "
                      "ORDER BY id ASC LIMIT ?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return 0;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, max_records);

    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && count < max_records) {
        records[count].id = sqlite3_column_int(stmt, 0);
        const char *s = (const char*)sqlite3_column_text(stmt, 1);
        const char *c = (const char*)sqlite3_column_text(stmt, 2);
        const char *t = (const char*)sqlite3_column_text(stmt, 3);

        safe_copy(records[count].sender, s ? s : "", sizeof(records[count].sender));
        safe_copy(records[count].receiver, username, sizeof(records[count].receiver));
        records[count].group_name[0] = '\0';
        safe_copy(records[count].content, c ? c : "", sizeof(records[count].content));
        safe_copy(records[count].timestamp, t ? t : "", sizeof(records[count].timestamp));
        count++;
    }
    sqlite3_finalize(stmt);
    return count;
}

/* Mark all pending messages for a user as delivered */
int db_mark_messages_delivered(const char *username) {
    if (!g_db || !username) return -1;
    const char *sql = "UPDATE messages SET is_delivered = 1 WHERE receiver = ? AND is_delivered = 0;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return 0;
}

/* ============================================================
 * Group & Membership Persistence (Task 3.3)
 * ============================================================ */

/* Create group if not already exists */
int db_create_group(const char *group_name) {
    if (!g_db || !group_name) return -1;
    char clean_gname[MAX_GROUP_NAME];
    safe_copy(clean_gname, group_name, sizeof(clean_gname));
    char *trimmed = trim_whitespace(clean_gname);
    if (strlen(trimmed) == 0) return -1;

    const char *sql = "INSERT OR IGNORE INTO groups (name) VALUES (?);";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, trimmed, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return 0;
}

/* Check if group exists in database */
bool db_group_exists(const char *group_name) {
    if (!g_db || !group_name) return false;
    char clean_gname[MAX_GROUP_NAME];
    safe_copy(clean_gname, group_name, sizeof(clean_gname));
    char *trimmed = trim_whitespace(clean_gname);
    if (strlen(trimmed) == 0) return false;

    const char *sql = "SELECT 1 FROM groups WHERE name = ? LIMIT 1;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, trimmed, -1, SQLITE_STATIC);
    bool exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return exists;
}

/* Add member to group in database */
int db_add_group_member(const char *group_name, const char *username) {
    if (!g_db || !group_name || !username) return -1;
    db_create_group(group_name);

    const char *sql = "INSERT OR IGNORE INTO group_members (group_name, username) VALUES (?, ?);";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, group_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, username, -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

/* Remove member from group in database */
int db_remove_group_member(const char *group_name, const char *username) {
    if (!g_db || !group_name || !username) return -1;
    const char *sql = "DELETE FROM group_members WHERE group_name = ? AND username = ?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, group_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, username, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return 0;
}

/* Check if user is a member of group in database */
bool db_is_group_member(const char *group_name, const char *username) {
    if (!g_db || !group_name || !username) return false;
    const char *sql = "SELECT 1 FROM group_members WHERE group_name = ? AND username = ? LIMIT 1;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, group_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, username, -1, SQLITE_STATIC);
    bool is_member = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return is_member;
}

/* Retrieve space-separated list of all persistent groups */
int db_get_groups(char *out_buf, size_t buf_size) {
    if (!g_db || !out_buf || buf_size == 0) return 0;
    out_buf[0] = '\0';
    const char *sql = "SELECT name FROM groups ORDER BY id ASC;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;

    size_t cur_len = 0;
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *name = (const char*)sqlite3_column_text(stmt, 0);
        if (name) {
            int written = snprintf(out_buf + cur_len, buf_size - cur_len,
                                   "%s%s", (cur_len > 0 ? " " : ""), name);
            if (written > 0 && cur_len + (size_t)written < buf_size) {
                cur_len += (size_t)written;
            }
            count++;
        }
    }
    sqlite3_finalize(stmt);
    return count;
}

/* Retrieve space-separated list of members for a group */
int db_get_group_members(const char *group_name, char *out_buf, size_t buf_size) {
    if (!g_db || !group_name || !out_buf || buf_size == 0) return 0;
    out_buf[0] = '\0';
    const char *sql = "SELECT username FROM group_members WHERE group_name = ? ORDER BY id ASC;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, group_name, -1, SQLITE_STATIC);

    size_t cur_len = 0;
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *user = (const char*)sqlite3_column_text(stmt, 0);
        if (user) {
            int written = snprintf(out_buf + cur_len, buf_size - cur_len,
                                   "%s%s", (cur_len > 0 ? " " : ""), user);
            if (written > 0 && cur_len + (size_t)written < buf_size) {
                cur_len += (size_t)written;
            }
            count++;
        }
    }
    sqlite3_finalize(stmt);
    return count;
}

/* Load groups & members from database to runtime memory on server start */
int db_load_groups_to_memory(void) {
    if (!g_db) return 0;
    const char *sql = "SELECT name FROM groups ORDER BY id ASC LIMIT ?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, MAX_GROUPS);

    int loaded_groups = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && loaded_groups < MAX_GROUPS) {
        const char *name = (const char*)sqlite3_column_text(stmt, 0);
        if (name) {
            g_groups[loaded_groups].in_use = true;
            safe_copy(g_groups[loaded_groups].name, name, sizeof(g_groups[loaded_groups].name));
            g_groups[loaded_groups].member_count = 0;

            /* Load members for this group */
            const char *m_sql = "SELECT username FROM group_members WHERE group_name = ? ORDER BY id ASC LIMIT ?;";
            sqlite3_stmt *m_stmt = NULL;
            if (sqlite3_prepare_v2(g_db, m_sql, -1, &m_stmt, NULL) == SQLITE_OK) {
                sqlite3_bind_text(m_stmt, 1, name, -1, SQLITE_STATIC);
                sqlite3_bind_int(m_stmt, 2, MAX_GROUP_MEMBERS);
                while (sqlite3_step(m_stmt) == SQLITE_ROW &&
                       g_groups[loaded_groups].member_count < MAX_GROUP_MEMBERS) {
                    const char *u = (const char*)sqlite3_column_text(m_stmt, 0);
                    if (u) {
                        safe_copy(g_groups[loaded_groups].members[g_groups[loaded_groups].member_count],
                                  u, sizeof(g_groups[loaded_groups].members[0]));
                        g_groups[loaded_groups].member_count++;
                    }
                }
                sqlite3_finalize(m_stmt);
            }
            loaded_groups++;
        }
    }
    sqlite3_finalize(stmt);
    printf("[Database] Loaded %d group(s) from SQLite into runtime memory.\n", loaded_groups);
    return loaded_groups;
}

