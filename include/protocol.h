#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <time.h>

/* ============================================================
 * Common limits & constants
 * ============================================================ */
#define MAX_CLIENTS             128
#define BUFFER_SIZE             4096
#define MAX_NAME_LEN            32
#define MAX_PASS_LEN            64
#define MAX_GROUP_NAME          32
#define MAX_GROUPS              32
#define MAX_GROUP_MEMBERS       64
#define MAX_MSG_LEN             2048
#define DEFAULT_PORT            8080
#define DB_PATH                 "storage/chat_server.db"

/* Heartbeat / Keep-alive settings (Task 1.4) */
#define HEARTBEAT_INTERVAL_SEC  15   /* Server sends PING every 15 seconds     */
#define HEARTBEAT_TIMEOUT_SEC   45   /* Disconnect if no data for 45 seconds   */
#define SELECT_TIMEOUT_SEC      5    /* select() wakes up every 5 seconds      */

/* ============================================================
 * Command identifiers (Client -> Server)
 * ============================================================ */
#define CMD_REGISTER            "REGISTER"
#define CMD_LOGIN               "LOGIN"
#define CMD_LOGOUT              "LOGOUT"
#define CMD_USERS               "USERS"
#define CMD_MSG                 "MSG"
#define CMD_START_CHAT          "START_CHAT"
#define CMD_END_CHAT            "END_CHAT"
#define CMD_JOIN                "JOIN"
#define CMD_LEAVE               "LEAVE"
#define CMD_GROUPS              "GROUPS"
#define CMD_MEMBERS             "MEMBERS"
#define CMD_GROUP_MSG           "GROUP_MSG"
#define CMD_QUIT                "QUIT"

/* Heartbeat commands (Task 1.4) */
#define CMD_PING                "PING"
#define CMD_PONG                "PONG"

/* ============================================================
 * Response & Event identifiers (Server -> Client)
 * ============================================================ */
#define RESP_OK                 "OK"
#define RESP_ERR                "ERROR"
#define EVENT_ONLINE            "EVENT_ONLINE"
#define EVENT_OFFLINE           "EVENT_OFFLINE"
#define RESP_ONLINE_LIST        "ONLINE_LIST"
#define RESP_FROM               "FROM"
#define RESP_GROUP_FROM         "GROUP_FROM"
#define EVENT_GROUP             "GROUP_EVENT"
#define RESP_GROUPS_LIST        "GROUPS_LIST"
#define RESP_MEMBERS_LIST       "MEMBERS_LIST"
#define RESP_NOTICE             "NOTICE"
#define RESP_SERVER_SHUTDOWN    "SERVER_SHUTDOWN"

#endif /* PROTOCOL_H */
