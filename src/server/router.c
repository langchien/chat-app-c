/* ============================================================
 * src/server/router.c
 * Command routing – parse and dispatch client commands.
 * Group management helpers.
 *
 * Task 1.2/1.3: Command handling after protocol parsing.
 * Task 1.4:     PING/PONG heartbeat handling.
 * ============================================================ */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strcasecmp */
#include <stdbool.h>
#include <time.h>

#include "../../include/server.h"

/* ============================================================
 * Group management helpers
 * ============================================================ */

Group* find_group(const char *name) {
    for (int i = 0; i < MAX_GROUPS; i++) {
        if (g_groups[i].in_use && strcmp(g_groups[i].name, name) == 0) {
            return &g_groups[i];
        }
    }
    return NULL;
}

Group* find_or_create_group(const char *name) {
    Group *grp = find_group(name);
    if (grp) return grp;
    for (int i = 0; i < MAX_GROUPS; i++) {
        if (!g_groups[i].in_use) {
            g_groups[i].in_use = true;
            safe_copy(g_groups[i].name, name, sizeof(g_groups[i].name));
            g_groups[i].member_count = 0;
            return &g_groups[i];
        }
    }
    return NULL;
}

bool is_group_member(Group *grp, const char *username) {
    if (!grp) return false;
    for (int i = 0; i < grp->member_count; i++) {
        if (strcmp(grp->members[i], username) == 0) return true;
    }
    return false;
}

bool add_to_group(Group *grp, const char *username) {
    if (!grp || is_group_member(grp, username)) return false;
    if (grp->member_count < MAX_GROUP_MEMBERS) {
        safe_copy(grp->members[grp->member_count], username, sizeof(grp->members[grp->member_count]));
        grp->member_count++;
        return true;
    }
    return false;
}

bool remove_from_group(Group *grp, const char *username) {
    if (!grp) return false;
    for (int i = 0; i < grp->member_count; i++) {
        if (strcmp(grp->members[i], username) == 0) {
            for (int j = i; j < grp->member_count - 1; j++) {
                strcpy(grp->members[j], grp->members[j + 1]);
            }
            grp->member_count--;
            if (grp->member_count == 0) {
                grp->in_use = false;
            }
            return true;
        }
    }
    return false;
}

void remove_client_from_all_groups(const char *username) {
    for (int i = 0; i < MAX_GROUPS; i++) {
        if (g_groups[i].in_use && is_group_member(&g_groups[i], username)) {
            remove_from_group(&g_groups[i], username);
            /* Broadcast group leave event */
            for (int m = 0; m < g_groups[i].member_count; m++) {
                ClientSession *mem = find_client_by_name(g_groups[i].members[m]);
                if (mem) {
                    send_to_fd(mem->sockfd, "%s LEAVE %s %s", EVENT_GROUP, g_groups[i].name, username);
                }
            }
        }
    }
}

/* ============================================================
 * handle_command – Process a single parsed command line
 * ============================================================ */
void handle_command(ClientSession *session, char *cmd_line) {
    char *line = trim_whitespace(cmd_line);
    if (strlen(line) == 0) return;

    char cmd[64] = {0};
    char *rest = NULL;

    char *space = strchr(line, ' ');
    if (space) {
        size_t cmd_len = (size_t)(space - line);
        if (cmd_len >= sizeof(cmd)) cmd_len = sizeof(cmd) - 1;
        strncpy(cmd, line, cmd_len);
        cmd[cmd_len] = '\0';
        rest = trim_whitespace(space + 1);
    } else {
        strncpy(cmd, line, sizeof(cmd) - 1);
        cmd[sizeof(cmd) - 1] = '\0';
        rest = "";
    }

    /* ---- Heartbeat: PONG response from client (Task 1.4) ---- */
    if (strcasecmp(cmd, CMD_PONG) == 0) {
        session->ping_pending = false;
        return;
    }

    /* ---- Commands available before authentication ---- */
    if (strcasecmp(cmd, CMD_REGISTER) == 0) {
        char user[MAX_NAME_LEN] = {0};
        char pass[MAX_PASS_LEN] = {0};
        if (sscanf(rest, "%31s %63s", user, pass) != 2) {
            send_to_fd(session->sockfd, "%s Usage: REGISTER <username> <password>", RESP_ERR);
            return;
        }
        if (db_user_exists(user)) {
            send_to_fd(session->sockfd, "%s Username '%s' already exists", RESP_ERR, user);
            return;
        }
        if (db_register_user(user, pass) != 0) {
            send_to_fd(session->sockfd, "%s Registration failed due to internal error", RESP_ERR);
            return;
        }
        send_to_fd(session->sockfd, "%s Registered successfully. You can now login.", RESP_OK);
        printf("[Server] New user registered: %s (SQLite3)\n", user);
        return;
    }

    if (strcasecmp(cmd, CMD_LOGIN) == 0) {
        char user[MAX_NAME_LEN] = {0};
        char pass[MAX_PASS_LEN] = {0};
        if (sscanf(rest, "%31s %63s", user, pass) != 2) {
            send_to_fd(session->sockfd, "%s Usage: LOGIN <username> <password>", RESP_ERR);
            return;
        }
        if (session->is_auth) {
            send_to_fd(session->sockfd, "%s Already logged in as '%s'", RESP_ERR, session->username);
            return;
        }
        if (find_client_by_name(user) != NULL) {
            send_to_fd(session->sockfd, "%s User '%s' is already logged in on another session", RESP_ERR, user);
            return;
        }
        if (!db_authenticate_user(user, pass)) {
            send_to_fd(session->sockfd, "%s Invalid username or password", RESP_ERR);
            return;
        }

        /* Successful authentication */
        session->is_auth = true;
        safe_copy(session->username, user, sizeof(session->username));
        db_update_last_seen(session->username);
        send_to_fd(session->sockfd, "%s LOGIN_OK %s", RESP_OK, session->username);
        printf("[Server] User logged in: %s (fd=%d)\n", session->username, session->sockfd);

        /* Send current online list to new user */
        char online_list[BUFFER_SIZE] = "";
        size_t cur_len = 0;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (g_clients[i].sockfd > 0 && g_clients[i].is_auth) {
                int written = snprintf(online_list + cur_len, sizeof(online_list) - cur_len,
                                       "%s%s", (cur_len > 0 ? " " : ""), g_clients[i].username);
                if (written > 0 && cur_len + (size_t)written < sizeof(online_list)) {
                    cur_len += (size_t)written;
                }
            }
        }
        send_to_fd(session->sockfd, "%s %s", RESP_ONLINE_LIST, online_list);

        /* Broadcast presence event to other online clients */
        broadcast_presence(session->username, true, session->sockfd);
        return;
    }

    if (strcasecmp(cmd, CMD_QUIT) == 0) {
        send_to_fd(session->sockfd, "%s Goodbye!", RESP_OK);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (&g_clients[i] == session) {
                disconnect_client(i);
                break;
            }
        }
        return;
    }

    /* ---- All following commands require authentication ---- */
    if (!session->is_auth) {
        send_to_fd(session->sockfd, "%s Please login first with LOGIN <username> <password>", RESP_ERR);
        return;
    }

    if (strcasecmp(cmd, CMD_LOGOUT) == 0) {
        printf("[Server] User logged out: %s\n", session->username);
        db_update_last_seen(session->username);
        broadcast_presence(session->username, false, session->sockfd);
        remove_client_from_all_groups(session->username);
        if (strlen(session->active_partner) > 0) {
            ClientSession *partner = find_client_by_name(session->active_partner);
            if (partner) {
                send_to_fd(partner->sockfd, "%s User '%s' has logged out.", RESP_NOTICE, session->username);
            }
            session->active_partner[0] = '\0';
        }
        session->is_auth = false;
        session->username[0] = '\0';
        send_to_fd(session->sockfd, "%s Logged out successfully.", RESP_OK);
        return;
    }

    if (strcasecmp(cmd, CMD_USERS) == 0) {
        char online_list[BUFFER_SIZE] = "";
        size_t cur_len = 0;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (g_clients[i].sockfd > 0 && g_clients[i].is_auth) {
                int written = snprintf(online_list + cur_len, sizeof(online_list) - cur_len,
                                       "%s%s", (cur_len > 0 ? " " : ""), g_clients[i].username);
                if (written > 0 && cur_len + (size_t)written < sizeof(online_list)) {
                    cur_len += (size_t)written;
                }
            }
        }
        send_to_fd(session->sockfd, "%s %s", RESP_ONLINE_LIST, online_list);
        return;
    }

    if (strcasecmp(cmd, CMD_MSG) == 0) {
        char target[MAX_NAME_LEN] = {0};
        char *target_end = strchr(rest, ' ');
        if (!target_end) {
            send_to_fd(session->sockfd, "%s Usage: MSG <target_username> <message>", RESP_ERR);
            return;
        }
        size_t tlen = (size_t)(target_end - rest);
        if (tlen >= sizeof(target)) tlen = sizeof(target) - 1;
        strncpy(target, rest, tlen);
        target[tlen] = '\0';
        char *msg_content = trim_whitespace(target_end + 1);

        if (strlen(msg_content) == 0) {
            send_to_fd(session->sockfd, "%s Message cannot be empty", RESP_ERR);
            return;
        }

        if (strcmp(target, session->username) == 0) {
            send_to_fd(session->sockfd, "%s Cannot send message to yourself", RESP_ERR);
            return;
        }

        ClientSession *recipient = find_client_by_name(target);
        if (!recipient) {
            send_to_fd(session->sockfd, "%s User '%s' is not online", RESP_ERR, target);
            return;
        }

        send_to_fd(recipient->sockfd, "%s %s %s", RESP_FROM, session->username, msg_content);
        send_to_fd(session->sockfd, "%s Sent to %s", RESP_OK, target);
        return;
    }

    if (strcasecmp(cmd, CMD_START_CHAT) == 0) {
        char target[MAX_NAME_LEN] = {0};
        if (sscanf(rest, "%31s", target) != 1) {
            send_to_fd(session->sockfd, "%s Usage: START_CHAT <target_username>", RESP_ERR);
            return;
        }
        if (strcmp(target, session->username) == 0) {
            send_to_fd(session->sockfd, "%s Cannot start chat session with yourself", RESP_ERR);
            return;
        }
        ClientSession *recipient = find_client_by_name(target);
        if (!recipient) {
            send_to_fd(session->sockfd, "%s User '%s' is not online", RESP_ERR, target);
            return;
        }

        safe_copy(session->active_partner, target, sizeof(session->active_partner));
        send_to_fd(recipient->sockfd, "%s User '%s' started a 1-on-1 chat with you.", RESP_NOTICE, session->username);
        send_to_fd(session->sockfd, "%s START_CHAT_OK %s", RESP_OK, target);
        return;
    }

    if (strcasecmp(cmd, CMD_END_CHAT) == 0) {
        if (strlen(session->active_partner) > 0) {
            ClientSession *partner = find_client_by_name(session->active_partner);
            if (partner) {
                send_to_fd(partner->sockfd, "%s User '%s' ended the 1-on-1 chat session.", RESP_NOTICE, session->username);
                if (strcmp(partner->active_partner, session->username) == 0) {
                    partner->active_partner[0] = '\0';
                }
            }
            session->active_partner[0] = '\0';
            send_to_fd(session->sockfd, "%s END_CHAT_OK", RESP_OK);
        } else {
            send_to_fd(session->sockfd, "%s No active 1-on-1 chat session", RESP_ERR);
        }
        return;
    }

    if (strcasecmp(cmd, CMD_JOIN) == 0) {
        char gname[MAX_GROUP_NAME] = {0};
        if (sscanf(rest, "%31s", gname) != 1) {
            send_to_fd(session->sockfd, "%s Usage: JOIN <group_name>", RESP_ERR);
            return;
        }
        Group *grp = find_or_create_group(gname);
        if (!grp) {
            send_to_fd(session->sockfd, "%s Server group limit reached", RESP_ERR);
            return;
        }
        if (is_group_member(grp, session->username)) {
            send_to_fd(session->sockfd, "%s Already a member of group '%s'", RESP_ERR, gname);
            return;
        }
        if (!add_to_group(grp, session->username)) {
            send_to_fd(session->sockfd, "%s Group '%s' is full", RESP_ERR, gname);
            return;
        }

        send_to_fd(session->sockfd, "%s Joined group '%s'", RESP_OK, gname);
        /* Broadcast to other group members */
        for (int i = 0; i < grp->member_count; i++) {
            if (strcmp(grp->members[i], session->username) != 0) {
                ClientSession *mem = find_client_by_name(grp->members[i]);
                if (mem) {
                    send_to_fd(mem->sockfd, "%s JOIN %s %s", EVENT_GROUP, gname, session->username);
                }
            }
        }
        return;
    }

    if (strcasecmp(cmd, CMD_LEAVE) == 0) {
        char gname[MAX_GROUP_NAME] = {0};
        if (sscanf(rest, "%31s", gname) != 1) {
            send_to_fd(session->sockfd, "%s Usage: LEAVE <group_name>", RESP_ERR);
            return;
        }
        Group *grp = find_group(gname);
        if (!grp || !is_group_member(grp, session->username)) {
            send_to_fd(session->sockfd, "%s You are not in group '%s'", RESP_ERR, gname);
            return;
        }

        /* Broadcast leave to group members before removing */
        for (int i = 0; i < grp->member_count; i++) {
            if (strcmp(grp->members[i], session->username) != 0) {
                ClientSession *mem = find_client_by_name(grp->members[i]);
                if (mem) {
                    send_to_fd(mem->sockfd, "%s LEAVE %s %s", EVENT_GROUP, gname, session->username);
                }
            }
        }
        remove_from_group(grp, session->username);
        send_to_fd(session->sockfd, "%s Left group '%s'", RESP_OK, gname);
        return;
    }

    if (strcasecmp(cmd, CMD_GROUPS) == 0) {
        char g_list[BUFFER_SIZE] = "";
        size_t cur_len = 0;
        for (int i = 0; i < MAX_GROUPS; i++) {
            if (g_groups[i].in_use) {
                int written = snprintf(g_list + cur_len, sizeof(g_list) - cur_len,
                                       "%s%s", (cur_len > 0 ? " " : ""), g_groups[i].name);
                if (written > 0 && cur_len + (size_t)written < sizeof(g_list)) {
                    cur_len += (size_t)written;
                }
            }
        }
        send_to_fd(session->sockfd, "%s %s", RESP_GROUPS_LIST, g_list);
        return;
    }

    if (strcasecmp(cmd, CMD_MEMBERS) == 0) {
        char gname[MAX_GROUP_NAME] = {0};
        if (sscanf(rest, "%31s", gname) != 1) {
            send_to_fd(session->sockfd, "%s Usage: MEMBERS <group_name>", RESP_ERR);
            return;
        }
        Group *grp = find_group(gname);
        if (!grp) {
            send_to_fd(session->sockfd, "%s Group '%s' does not exist", RESP_ERR, gname);
            return;
        }
        char m_list[BUFFER_SIZE] = "";
        size_t cur_len = 0;
        for (int i = 0; i < grp->member_count; i++) {
            int written = snprintf(m_list + cur_len, sizeof(m_list) - cur_len,
                                   "%s%s", (cur_len > 0 ? " " : ""), grp->members[i]);
            if (written > 0 && cur_len + (size_t)written < sizeof(m_list)) {
                cur_len += (size_t)written;
            }
        }
        send_to_fd(session->sockfd, "%s %s %s", RESP_MEMBERS_LIST, gname, m_list);
        return;
    }

    if (strcasecmp(cmd, CMD_GROUP_MSG) == 0) {
        char gname[MAX_GROUP_NAME] = {0};
        char *target_end = strchr(rest, ' ');
        if (!target_end) {
            send_to_fd(session->sockfd, "%s Usage: GROUP_MSG <group_name> <message>", RESP_ERR);
            return;
        }
        size_t glen = (size_t)(target_end - rest);
        if (glen >= sizeof(gname)) glen = sizeof(gname) - 1;
        strncpy(gname, rest, glen);
        gname[glen] = '\0';
        char *msg_content = trim_whitespace(target_end + 1);

        if (strlen(msg_content) == 0) {
            send_to_fd(session->sockfd, "%s Message cannot be empty", RESP_ERR);
            return;
        }

        Group *grp = find_group(gname);
        if (!grp) {
            send_to_fd(session->sockfd, "%s Group '%s' does not exist", RESP_ERR, gname);
            return;
        }
        if (!is_group_member(grp, session->username)) {
            send_to_fd(session->sockfd, "%s You must join group '%s' first (JOIN %s)", RESP_ERR, gname, gname);
            return;
        }

        /* Forward message to all other members */
        int forwarded = 0;
        for (int i = 0; i < grp->member_count; i++) {
            if (strcmp(grp->members[i], session->username) != 0) {
                ClientSession *mem = find_client_by_name(grp->members[i]);
                if (mem) {
                    send_to_fd(mem->sockfd, "%s %s %s %s", RESP_GROUP_FROM, gname, session->username, msg_content);
                    forwarded++;
                }
            }
        }
        send_to_fd(session->sockfd, "%s Group message delivered to %d member(s)", RESP_OK, forwarded);
        return;
    }

    send_to_fd(session->sockfd, "%s Unknown command '%s'.", RESP_ERR, cmd);
}
