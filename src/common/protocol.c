#include "protocol.h"

#include <stdio.h>
#include <string.h>

static int split_first_token(const char *line, char *cmd, size_t cmd_len,
                             const char **rest)
{
    const char *space = strchr(line, ' ');
    if (space == NULL) {
        if (strlen(line) >= cmd_len) {
            return CHAT_ERR_PROTOCOL;
        }
        strncpy(cmd, line, cmd_len - 1);
        cmd[cmd_len - 1] = '\0';
        *rest = "";
        return CHAT_OK;
    }

    size_t cmd_size = (size_t)(space - line);
    if (cmd_size >= cmd_len) {
        return CHAT_ERR_PROTOCOL;
    }
    memcpy(cmd, line, cmd_size);
    cmd[cmd_size] = '\0';
    *rest = space + 1;
    return CHAT_OK;
}

static int parse_two_tokens(const char *rest, char *a, size_t a_len,
                            char *b, size_t b_len)
{
    const char *sp = strchr(rest, ' ');
    if (sp == NULL) {
        return CHAT_ERR_PROTOCOL;
    }
    size_t len_a = (size_t)(sp - rest);
    if (len_a == 0 || len_a >= a_len) {
        return CHAT_ERR_PROTOCOL;
    }
    memcpy(a, rest, len_a);
    a[len_a] = '\0';

    const char *msg = sp + 1;
    if (msg[0] == '\0' || strlen(msg) >= b_len) {
        return CHAT_ERR_PROTOCOL;
    }
    strncpy(b, msg, b_len - 1);
    b[b_len - 1] = '\0';
    return CHAT_OK;
}

int protocol_parse_client_line(const char *line, parsed_cmd_t *out)
{
    char cmd[16];
    const char *rest = NULL;

    if (line == NULL || out == NULL) {
        return CHAT_ERR;
    }

    memset(out, 0, sizeof(*out));

    if (split_first_token(line, cmd, sizeof(cmd), &rest) != CHAT_OK) {
        return CHAT_ERR_PROTOCOL;
    }

    if (strcmp(cmd, "AUTH") == 0) {
        out->cmd = CMD_AUTH;
        return parse_two_tokens(rest, out->arg1, MAX_USERNAME_LEN,
                                out->arg2, MAX_MSG_LEN);
    }

    if (strcmp(cmd, "JOIN_ROOM") == 0) {
        if (rest[0] == '\0' || strlen(rest) >= MAX_ROOM_NAME_LEN) {
            return CHAT_ERR_PROTOCOL;
        }
        out->cmd = CMD_JOIN_ROOM;
        strncpy(out->arg1, rest, MAX_ROOM_NAME_LEN - 1);
        return CHAT_OK;
    }

    if (strcmp(cmd, "MSG") == 0) {
        if (rest[0] == '\0' || strlen(rest) >= MAX_MSG_LEN) {
            return CHAT_ERR_PROTOCOL;
        }
        out->cmd = CMD_MSG;
        strncpy(out->arg2, rest, MAX_MSG_LEN - 1);
        return CHAT_OK;
    }

    if (strcmp(cmd, "PM") == 0) {
        out->cmd = CMD_PM;
        return parse_two_tokens(rest, out->arg1, MAX_USERNAME_LEN,
                                out->arg2, MAX_MSG_LEN);
    }

    if (strcmp(cmd, "LIST") == 0) {
        out->cmd = CMD_LIST;
        return CHAT_OK;
    }

    if (strcmp(cmd, "HISTORY") == 0) {
        out->cmd = CMD_HISTORY;
        return CHAT_OK;
    }

    if (strcmp(cmd, "KICK") == 0) {
        if (rest[0] == '\0' || strlen(rest) >= MAX_USERNAME_LEN) {
            return CHAT_ERR_PROTOCOL;
        }
        out->cmd = CMD_KICK;
        strncpy(out->arg1, rest, MAX_USERNAME_LEN - 1);
        return CHAT_OK;
    }

    if (strcmp(cmd, "QUIT") == 0) {
        out->cmd = CMD_QUIT;
        return CHAT_OK;
    }

    out->cmd = CMD_UNKNOWN;
    return CHAT_ERR_PROTOCOL;
}

int protocol_build_client_auth(char *buf, size_t buflen,
                               const char *username, const char *password)
{
    return snprintf(buf, buflen, "AUTH %s %s\n", username, password) >= (int)buflen
               ? CHAT_ERR : CHAT_OK;
}

int protocol_build_client_join_room(char *buf, size_t buflen, const char *room)
{
    return snprintf(buf, buflen, "JOIN_ROOM %s\n", room) >= (int)buflen ? CHAT_ERR : CHAT_OK;
}

int protocol_build_client_msg(char *buf, size_t buflen, const char *message)
{
    return snprintf(buf, buflen, "MSG %s\n", message) >= (int)buflen ? CHAT_ERR : CHAT_OK;
}

int protocol_build_client_pm(char *buf, size_t buflen,
                             const char *target, const char *message)
{
    return snprintf(buf, buflen, "PM %s %s\n", target, message) >= (int)buflen
               ? CHAT_ERR : CHAT_OK;
}

int protocol_build_client_list(char *buf, size_t buflen)
{
    return snprintf(buf, buflen, "LIST\n") >= (int)buflen ? CHAT_ERR : CHAT_OK;
}

int protocol_build_client_history(char *buf, size_t buflen)
{
    return snprintf(buf, buflen, "HISTORY\n") >= (int)buflen ? CHAT_ERR : CHAT_OK;
}

int protocol_build_client_kick(char *buf, size_t buflen, const char *username)
{
    return snprintf(buf, buflen, "KICK %s\n", username) >= (int)buflen ? CHAT_ERR : CHAT_OK;
}

int protocol_build_client_quit(char *buf, size_t buflen)
{
    return snprintf(buf, buflen, "QUIT\n") >= (int)buflen ? CHAT_ERR : CHAT_OK;
}

static int parse_four_fields_rest_msg(const char *rest,
                                      char *f1, size_t f1_len,
                                      char *f2, size_t f2_len,
                                      char *f3, size_t f3_len,
                                      char *f4, size_t f4_len)
{
    const char *sp1 = strchr(rest, ' ');
    const char *sp2;
    const char *sp3;
    size_t len;

    if (sp1 == NULL) {
        return CHAT_ERR_PROTOCOL;
    }
    len = (size_t)(sp1 - rest);
    if (len == 0 || len >= f1_len) {
        return CHAT_ERR_PROTOCOL;
    }
    memcpy(f1, rest, len);
    f1[len] = '\0';

    sp2 = strchr(sp1 + 1, ' ');
    if (sp2 == NULL) {
        return CHAT_ERR_PROTOCOL;
    }
    len = (size_t)(sp2 - (sp1 + 1));
    if (len == 0 || len >= f2_len) {
        return CHAT_ERR_PROTOCOL;
    }
    memcpy(f2, sp1 + 1, len);
    f2[len] = '\0';

    sp3 = strchr(sp2 + 1, ' ');
    if (sp3 == NULL) {
        return CHAT_ERR_PROTOCOL;
    }
    len = (size_t)(sp3 - (sp2 + 1));
    if (len == 0 || len >= f3_len) {
        return CHAT_ERR_PROTOCOL;
    }
    memcpy(f3, sp2 + 1, len);
    f3[len] = '\0';

    const char *msg = sp3 + 1;
    if (msg[0] == '\0' || strlen(msg) >= f4_len) {
        return CHAT_ERR_PROTOCOL;
    }
    strncpy(f4, msg, f4_len - 1);
    f4[f4_len - 1] = '\0';
    return CHAT_OK;
}

static int parse_two_fields(const char *rest, char *f1, size_t f1_len,
                            char *f2, size_t f2_len)
{
    const char *sp = strchr(rest, ' ');
    if (sp == NULL) {
        if (rest[0] == '\0' || strlen(rest) >= f1_len) {
            return CHAT_ERR_PROTOCOL;
        }
        strncpy(f1, rest, f1_len - 1);
        f2[0] = '\0';
        return CHAT_OK;
    }
    size_t len = (size_t)(sp - rest);
    if (len == 0 || len >= f1_len) {
        return CHAT_ERR_PROTOCOL;
    }
    memcpy(f1, rest, len);
    f1[len] = '\0';
    const char *f2src = sp + 1;
    if (f2src[0] == '\0' || strlen(f2src) >= f2_len) {
        return CHAT_ERR_PROTOCOL;
    }
    strncpy(f2, f2src, f2_len - 1);
    return CHAT_OK;
}

static int parse_three_fields_msg(const char *rest,
                                  char *f1, size_t f1_len,
                                  char *f2, size_t f2_len,
                                  char *f3, size_t f3_len)
{
    const char *sp1 = strchr(rest, ' ');
    const char *sp2;
    size_t len;

    if (sp1 == NULL) {
        return CHAT_ERR_PROTOCOL;
    }
    len = (size_t)(sp1 - rest);
    if (len == 0 || len >= f1_len) {
        return CHAT_ERR_PROTOCOL;
    }
    memcpy(f1, rest, len);
    f1[len] = '\0';

    sp2 = strchr(sp1 + 1, ' ');
    if (sp2 == NULL) {
        return CHAT_ERR_PROTOCOL;
    }
    len = (size_t)(sp2 - (sp1 + 1));
    if (len == 0 || len >= f2_len) {
        return CHAT_ERR_PROTOCOL;
    }
    memcpy(f2, sp1 + 1, len);
    f2[len] = '\0';

    const char *msg = sp2 + 1;
    if (msg[0] == '\0' || strlen(msg) >= f3_len) {
        return CHAT_ERR_PROTOCOL;
    }
    strncpy(f3, msg, f3_len - 1);
    return CHAT_OK;
}

int protocol_parse_server_line(const char *line, parsed_resp_t *out)
{
    char cmd[16];
    const char *rest = NULL;

    if (line == NULL || out == NULL) {
        return CHAT_ERR;
    }

    memset(out, 0, sizeof(*out));

    if (split_first_token(line, cmd, sizeof(cmd), &rest) != CHAT_OK) {
        return CHAT_ERR_PROTOCOL;
    }

    if (strcmp(cmd, "OK") == 0) {
        out->type = RESP_OK;
        strncpy(out->message, rest, MAX_MSG_LEN - 1);
        return CHAT_OK;
    }

    if (strcmp(cmd, "ERR") == 0) {
        out->type = RESP_ERR;
        strncpy(out->message, rest, MAX_MSG_LEN - 1);
        return CHAT_OK;
    }

    if (strcmp(cmd, "WELCOME") == 0) {
        out->type = RESP_WELCOME;
        strncpy(out->username, rest, MAX_USERNAME_LEN - 1);
        return CHAT_OK;
    }

    if (strcmp(cmd, "BROADCAST") == 0) {
        out->type = RESP_BROADCAST;
        return parse_four_fields_rest_msg(rest,
                                          out->username, MAX_USERNAME_LEN,
                                          out->timestamp, sizeof(out->timestamp),
                                          out->room, MAX_ROOM_NAME_LEN,
                                          out->message, MAX_MSG_LEN);
    }

    if (strcmp(cmd, "PM") == 0) {
        out->type = RESP_PM;
        return parse_three_fields_msg(rest,
                                      out->from_user, MAX_USERNAME_LEN,
                                      out->timestamp, sizeof(out->timestamp),
                                      out->message, MAX_MSG_LEN);
    }

    if (strcmp(cmd, "USER_JOIN") == 0) {
        out->type = RESP_USER_JOIN;
        return parse_two_fields(rest, out->username, MAX_USERNAME_LEN,
                                out->room, MAX_ROOM_NAME_LEN);
    }

    if (strcmp(cmd, "USER_LEFT") == 0) {
        out->type = RESP_USER_LEFT;
        return parse_two_fields(rest, out->username, MAX_USERNAME_LEN,
                                out->room, MAX_ROOM_NAME_LEN);
    }

    if (strcmp(cmd, "ROOM_JOINED") == 0) {
        out->type = RESP_ROOM_JOINED;
        strncpy(out->room, rest, MAX_ROOM_NAME_LEN - 1);
        return CHAT_OK;
    }

    if (strcmp(cmd, "HISTORY") == 0) {
        out->type = RESP_HISTORY;
        strncpy(out->history_payload, rest, MAX_LINE_LEN - 1);
        return CHAT_OK;
    }

    if (strcmp(cmd, "KICKED") == 0) {
        out->type = RESP_KICKED;
        strncpy(out->message, rest, MAX_MSG_LEN - 1);
        return CHAT_OK;
    }

    if (strcmp(cmd, "LIST") == 0) {
        out->type = RESP_LIST;
        strncpy(out->user_list, rest, MAX_MSG_LEN - 1);
        return CHAT_OK;
    }

    out->type = RESP_UNKNOWN;
    return CHAT_ERR_PROTOCOL;
}

int protocol_build_server_ok(char *buf, size_t buflen, const char *msg)
{
    return snprintf(buf, buflen, "OK %s\n", msg != NULL ? msg : "") >= (int)buflen
               ? CHAT_ERR : CHAT_OK;
}

int protocol_build_server_err(char *buf, size_t buflen, const char *msg)
{
    return snprintf(buf, buflen, "ERR %s\n", msg != NULL ? msg : "") >= (int)buflen
               ? CHAT_ERR : CHAT_OK;
}

int protocol_build_server_welcome(char *buf, size_t buflen, const char *username)
{
    return snprintf(buf, buflen, "WELCOME %s\n", username) >= (int)buflen ? CHAT_ERR : CHAT_OK;
}

int protocol_build_server_broadcast(char *buf, size_t buflen,
                                    const char *username,
                                    const char *timestamp,
                                    const char *room,
                                    const char *message)
{
    return snprintf(buf, buflen, "BROADCAST %s %s %s %s\n",
                    username, timestamp, room, message) >= (int)buflen
               ? CHAT_ERR : CHAT_OK;
}

int protocol_build_server_pm(char *buf, size_t buflen,
                             const char *from_user,
                             const char *timestamp,
                             const char *message)
{
    return snprintf(buf, buflen, "PM %s %s %s\n", from_user, timestamp, message) >= (int)buflen
               ? CHAT_ERR : CHAT_OK;
}

int protocol_build_server_user_join(char *buf, size_t buflen,
                                    const char *username, const char *room)
{
    return snprintf(buf, buflen, "USER_JOIN %s %s\n", username, room) >= (int)buflen
               ? CHAT_ERR : CHAT_OK;
}

int protocol_build_server_user_left(char *buf, size_t buflen,
                                    const char *username, const char *room)
{
    return snprintf(buf, buflen, "USER_LEFT %s %s\n", username, room) >= (int)buflen
               ? CHAT_ERR : CHAT_OK;
}

int protocol_build_server_room_joined(char *buf, size_t buflen, const char *room)
{
    return snprintf(buf, buflen, "ROOM_JOINED %s\n", room) >= (int)buflen ? CHAT_ERR : CHAT_OK;
}

int protocol_build_server_history(char *buf, size_t buflen, const char *payload)
{
    return snprintf(buf, buflen, "HISTORY %s\n", payload != NULL ? payload : "") >= (int)buflen
               ? CHAT_ERR : CHAT_OK;
}

int protocol_build_server_kicked(char *buf, size_t buflen, const char *reason)
{
    return snprintf(buf, buflen, "KICKED %s\n", reason != NULL ? reason : "") >= (int)buflen
               ? CHAT_ERR : CHAT_OK;
}

int protocol_build_server_list(char *buf, size_t buflen, const char *user_list)
{
    return snprintf(buf, buflen, "LIST %s\n", user_list != NULL ? user_list : "") >= (int)buflen
               ? CHAT_ERR : CHAT_OK;
}

const char *protocol_cmd_name(client_cmd_t cmd)
{
    switch (cmd) {
    case CMD_AUTH: return "AUTH";
    case CMD_JOIN_ROOM: return "JOIN_ROOM";
    case CMD_MSG: return "MSG";
    case CMD_PM: return "PM";
    case CMD_LIST: return "LIST";
    case CMD_HISTORY: return "HISTORY";
    case CMD_KICK: return "KICK";
    case CMD_QUIT: return "QUIT";
    default: return "UNKNOWN";
    }
}

const char *protocol_resp_name(server_resp_t type)
{
    switch (type) {
    case RESP_OK: return "OK";
    case RESP_ERR: return "ERR";
    case RESP_WELCOME: return "WELCOME";
    case RESP_BROADCAST: return "BROADCAST";
    case RESP_PM: return "PM";
    case RESP_USER_JOIN: return "USER_JOIN";
    case RESP_USER_LEFT: return "USER_LEFT";
    case RESP_ROOM_JOINED: return "ROOM_JOINED";
    case RESP_HISTORY: return "HISTORY";
    case RESP_KICKED: return "KICKED";
    case RESP_LIST: return "LIST";
    default: return "UNKNOWN";
    }
}
