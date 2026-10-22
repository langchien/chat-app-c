/* ============================================================
 * src/server/file_transfer.c
 * Server-side non-blocking chunked file & image transfer routing.
 *
 * Task 4.1: Hybrid Chunk-based framing, transfer session management,
 *           size check, Base64 chunk relay, SHA-256 verification,
 *           and SQLite3 file_transfers record persistence.
 * ============================================================ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#include "../../include/server.h"
#include "../../include/protocol.h"
#include "../../include/crypto_utils.h"

/* Global file transfer sessions pool */
FileTransferSession g_file_transfers[MAX_FILE_TRANSFERS];

/* Counter for unique transfer_id generation */
static unsigned long g_transfer_counter = 1000;

void init_file_transfers(void) {
    memset(g_file_transfers, 0, sizeof(g_file_transfers));
}

FileTransferSession* find_file_transfer(const char *transfer_id) {
    if (!transfer_id || strlen(transfer_id) == 0) return NULL;
    for (int i = 0; i < MAX_FILE_TRANSFERS; i++) {
        if (g_file_transfers[i].in_use && strcmp(g_file_transfers[i].transfer_id, transfer_id) == 0) {
            return &g_file_transfers[i];
        }
    }
    return NULL;
}

FileTransferSession* create_file_transfer(const char *sender, const char *target, bool is_group,
                                         const char *filename, long filesize,
                                         const char *sha256, bool is_image) {
    for (int i = 0; i < MAX_FILE_TRANSFERS; i++) {
        if (!g_file_transfers[i].in_use) {
            FileTransferSession *s = &g_file_transfers[i];
            memset(s, 0, sizeof(FileTransferSession));
            s->in_use = true;
            snprintf(s->transfer_id, sizeof(s->transfer_id), "TR_%lu_%d", ++g_transfer_counter, rand() % 9000 + 1000);
            safe_copy(s->sender, sender, sizeof(s->sender));
            safe_copy(s->target, target, sizeof(s->target));
            s->is_group = is_group;
            safe_copy(s->filename, filename, sizeof(s->filename));
            s->filesize = filesize;
            safe_copy(s->sha256, sha256 ? sha256 : "", sizeof(s->sha256));
            s->is_image = is_image;
            s->total_chunks = (int)((filesize + MAX_CHUNK_SIZE - 1) / MAX_CHUNK_SIZE);
            s->received_chunks = 0;
            s->started_at = time(NULL);
            return s;
        }
    }
    return NULL;
}

void remove_file_transfer(const char *transfer_id) {
    FileTransferSession *s = find_file_transfer(transfer_id);
    if (s) {
        s->in_use = false;
        memset(s, 0, sizeof(FileTransferSession));
    }
}

/* ============================================================
 * cleanup_user_file_transfers – Abort & cleanup all transfers
 * associated with a user when they disconnect or logout.
 * Task 6.2: Resource & FD leak prevention.
 * ============================================================ */
void cleanup_user_file_transfers(const char *username) {
    if (!username || strlen(username) == 0) return;
    for (int i = 0; i < MAX_FILE_TRANSFERS; i++) {
        if (g_file_transfers[i].in_use) {
            if (strcmp(g_file_transfers[i].sender, username) == 0) {
                if (!g_file_transfers[i].is_group) {
                    ClientSession *rec = find_client_by_name(g_file_transfers[i].target);
                    if (rec) {
                        send_to_fd(rec->sockfd, "%s Transfer '%s' aborted: sender '%s' disconnected",
                                   RESP_ERR, g_file_transfers[i].transfer_id, username);
                    }
                }
                g_file_transfers[i].in_use = false;
                memset(&g_file_transfers[i], 0, sizeof(FileTransferSession));
            } else if (!g_file_transfers[i].is_group && strcmp(g_file_transfers[i].target, username) == 0) {
                ClientSession *snd = find_client_by_name(g_file_transfers[i].sender);
                if (snd) {
                    send_to_fd(snd->sockfd, "%s Transfer '%s' aborted: recipient '%s' disconnected",
                               RESP_ERR, g_file_transfers[i].transfer_id, username);
                }
                g_file_transfers[i].in_use = false;
                memset(&g_file_transfers[i], 0, sizeof(FileTransferSession));
            }
        }
    }
}

/* ============================================================
 * handle_file_init
 * Syntax: FILE_INIT <target> <filename> <filesize> <sha256> <is_image>
 * ============================================================ */
void handle_file_init(ClientSession *session, char *args) {
    char target[MAX_NAME_LEN] = {0};
    char filename[MAX_FILENAME_LEN] = {0};
    long filesize = 0;
    char sha256[SHA256_HEX_LEN + 1] = {0};
    int is_image = 0;

    if (sscanf(args, "%31s %127s %ld %64s %d", target, filename, &filesize, sha256, &is_image) < 4) {
        send_to_fd(session->sockfd, "%s Usage: FILE_INIT <target> <filename> <filesize> <sha256> [is_image]", RESP_ERR);
        return;
    }

    /* Check file size limit (50MB) */
    if (filesize <= 0 || filesize > MAX_FILE_SIZE) {
        send_to_fd(session->sockfd, "%s File size exceeds maximum allowed limit (50MB)", RESP_ERR);
        return;
    }

    bool is_group = false;
    ClientSession *recipient = NULL;
    Group *grp = NULL;

    /* Check if target is a group */
    grp = find_group(target);
    if (grp || db_group_exists(target)) {
        is_group = true;
        if (!is_group_member(grp, session->username) && !db_is_group_member(target, session->username)) {
            send_to_fd(session->sockfd, "%s You must join group '%s' first", RESP_ERR, target);
            return;
        }
    } else {
        /* 1-on-1 target check */
        if (strcmp(target, session->username) == 0) {
            send_to_fd(session->sockfd, "%s Cannot send file to yourself", RESP_ERR);
            return;
        }
        recipient = find_client_by_name(target);
        if (!recipient) {
            send_to_fd(session->sockfd, "%s User '%s' is not online to receive file", RESP_ERR, target);
            return;
        }
    }

    /* Sanitize filename: strip directory path */
    char *clean_name = strrchr(filename, '/');
    if (!clean_name) clean_name = strrchr(filename, '\\');
    if (clean_name) clean_name++;
    else clean_name = filename;

    FileTransferSession *transfer = create_file_transfer(session->username, target, is_group,
                                                         clean_name, filesize, sha256, is_image != 0);
    if (!transfer) {
        send_to_fd(session->sockfd, "%s Server file transfer limit reached", RESP_ERR);
        return;
    }

    /* Save pending transfer in SQLite database */
    if (g_db) {
        const char *sql = "INSERT OR REPLACE INTO file_transfers "
                          "(transfer_id, sender, receiver, filename, filesize, sha256, status, created_at) "
                          "VALUES (?, ?, ?, ?, ?, ?, 'PENDING', CURRENT_TIMESTAMP);";
        sqlite3_stmt *stmt = NULL;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, transfer->transfer_id, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, session->username, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 3, target, -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 4, clean_name, -1, SQLITE_STATIC);
            sqlite3_bind_int64(stmt, 5, filesize);
            sqlite3_bind_text(stmt, 6, sha256, -1, SQLITE_STATIC);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    /* Notify sender: ready to accept chunks */
    send_to_fd(session->sockfd, "%s %s %s %d", RESP_OK, RESP_FILE_READY, transfer->transfer_id, MAX_CHUNK_SIZE);

    /* Notify receiver(s) */
    if (!is_group && recipient) {
        send_to_fd(recipient->sockfd, "%s %s %s %s %ld %s %d",
                   RESP_FILE_INCOMING, transfer->transfer_id, session->username,
                   clean_name, filesize, sha256, is_image);
    } else if (is_group && grp) {
        for (int i = 0; i < grp->member_count; i++) {
            if (strcmp(grp->members[i], session->username) != 0) {
                ClientSession *mem = find_client_by_name(grp->members[i]);
                if (mem) {
                    send_to_fd(mem->sockfd, "%s %s %s %s %ld %s %d",
                               RESP_FILE_INCOMING, transfer->transfer_id, session->username,
                               clean_name, filesize, sha256, is_image);
                }
            }
        }
    }

    printf("[Server] File transfer initiated: id=%s (%s -> %s, %s, %ld bytes, %s)\n",
           transfer->transfer_id, session->username, target, clean_name, filesize, is_image ? "IMAGE" : "FILE");
}

/* ============================================================
 * handle_file_chunk
 * Syntax: FILE_CHUNK <transfer_id> <seq_num> <base64_data>
 * ============================================================ */
void handle_file_chunk(ClientSession *session, char *args) {
    char transfer_id[MAX_TRANSFER_ID_LEN] = {0};
    int seq_num = 0;
    int offset = 0;

    if (sscanf(args, "%31s %d%n", transfer_id, &seq_num, &offset) < 2) {
        send_to_fd(session->sockfd, "%s Usage: FILE_CHUNK <transfer_id> <seq_num> <base64_data>", RESP_ERR);
        return;
    }

    char *b64_data = trim_whitespace(args + offset);
    if (strlen(b64_data) == 0) return;

    FileTransferSession *transfer = find_file_transfer(transfer_id);
    if (!transfer) {
        send_to_fd(session->sockfd, "%s Invalid or expired transfer_id '%s'", RESP_ERR, transfer_id);
        return;
    }

    if (strcmp(transfer->sender, session->username) != 0) {
        send_to_fd(session->sockfd, "%s Permission denied on transfer_id '%s'", RESP_ERR, transfer_id);
        return;
    }

    transfer->received_chunks++;

    /* Relay chunk directly to receiver(s) without buffering entire file in server memory */
    if (!transfer->is_group) {
        ClientSession *recipient = find_client_by_name(transfer->target);
        if (recipient) {
            send_to_fd(recipient->sockfd, "%s %s %d %s",
                       RESP_FILE_CHUNK, transfer_id, seq_num, b64_data);
        }
    } else {
        Group *grp = find_group(transfer->target);
        if (grp) {
            for (int i = 0; i < grp->member_count; i++) {
                if (strcmp(grp->members[i], session->username) != 0) {
                    ClientSession *mem = find_client_by_name(grp->members[i]);
                    if (mem) {
                        send_to_fd(mem->sockfd, "%s %s %d %s",
                                   RESP_FILE_CHUNK, transfer_id, seq_num, b64_data);
                    }
                }
            }
        }
    }
}

/* ============================================================
 * handle_file_end
 * Syntax: FILE_END <transfer_id>
 * ============================================================ */
void handle_file_end(ClientSession *session, char *args) {
    char transfer_id[MAX_TRANSFER_ID_LEN] = {0};
    if (sscanf(args, "%31s", transfer_id) != 1) {
        send_to_fd(session->sockfd, "%s Usage: FILE_END <transfer_id>", RESP_ERR);
        return;
    }

    FileTransferSession *transfer = find_file_transfer(transfer_id);
    if (!transfer) {
        send_to_fd(session->sockfd, "%s Invalid transfer_id '%s'", RESP_ERR, transfer_id);
        return;
    }

    if (strcmp(transfer->sender, session->username) != 0) {
        send_to_fd(session->sockfd, "%s Permission denied on transfer_id '%s'", RESP_ERR, transfer_id);
        return;
    }

    /* Notify receiver(s) of completion with original checksum */
    if (!transfer->is_group) {
        ClientSession *recipient = find_client_by_name(transfer->target);
        if (recipient) {
            send_to_fd(recipient->sockfd, "%s %s %s",
                       RESP_FILE_DONE, transfer_id, transfer->sha256);
        }
    } else {
        Group *grp = find_group(transfer->target);
        if (grp) {
            for (int i = 0; i < grp->member_count; i++) {
                if (strcmp(grp->members[i], session->username) != 0) {
                    ClientSession *mem = find_client_by_name(grp->members[i]);
                    if (mem) {
                        send_to_fd(mem->sockfd, "%s %s %s",
                                   RESP_FILE_DONE, transfer_id, transfer->sha256);
                    }
                }
            }
        }
    }

    /* Update database transfer status and log message */
    if (g_db) {
        const char *sql_up = "UPDATE file_transfers SET status = 'COMPLETED' WHERE transfer_id = ?;";
        sqlite3_stmt *stmt = NULL;
        if (sqlite3_prepare_v2(g_db, sql_up, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, transfer_id, -1, SQLITE_STATIC);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }

        /* Save transfer record in messages table */
        char msg_content[256];
        snprintf(msg_content, sizeof(msg_content), "[%s: %s (%ld bytes)]",
                 transfer->is_image ? "IMAGE" : "FILE", transfer->filename, transfer->filesize);
        db_save_message(transfer->sender,
                        transfer->is_group ? NULL : transfer->target,
                        transfer->is_group ? transfer->target : NULL,
                        msg_content,
                        transfer->is_image ? "IMAGE" : "FILE", 1);
    }

    send_to_fd(session->sockfd, "%s File '%s' transfer completed (%d chunks)",
               RESP_OK, transfer->filename, transfer->received_chunks);

    printf("[Server] File transfer completed: id=%s (%s -> %s, %s)\n",
           transfer->transfer_id, transfer->sender, transfer->target, transfer->filename);

    remove_file_transfer(transfer_id);
}
