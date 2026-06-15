#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "common.h"

typedef enum {
    CMD_UNKNOWN = 0,
    CMD_AUTH,
    CMD_JOIN_ROOM,
    CMD_MSG,
    CMD_PM,
    CMD_LIST,
    CMD_HISTORY,
    CMD_KICK,
    CMD_QUIT
} client_cmd_t;

typedef enum {
    RESP_UNKNOWN = 0,
    RESP_OK,
    RESP_ERR,
    RESP_WELCOME,
    RESP_BROADCAST,
    RESP_PM,
    RESP_USER_JOIN,
    RESP_USER_LEFT,
    RESP_ROOM_JOINED,
    RESP_HISTORY,
    RESP_KICKED,
    RESP_LIST
} server_resp_t;

typedef struct {
    client_cmd_t cmd;
    char arg1[MAX_USERNAME_LEN];
    char arg2[MAX_MSG_LEN];
} parsed_cmd_t;

typedef struct {
    server_resp_t type;
    char username[MAX_USERNAME_LEN];
    char room[MAX_ROOM_NAME_LEN];
    char message[MAX_MSG_LEN];
    char timestamp[32];
    char user_list[MAX_MSG_LEN];
    char history_payload[MAX_LINE_LEN];
    char from_user[MAX_USERNAME_LEN];
} parsed_resp_t;

int protocol_parse_client_line(const char *line, parsed_cmd_t *out);

int protocol_build_client_auth(char *buf, size_t buflen,
                               const char *username, const char *password);
int protocol_build_client_join_room(char *buf, size_t buflen, const char *room);
int protocol_build_client_msg(char *buf, size_t buflen, const char *message);
int protocol_build_client_pm(char *buf, size_t buflen,
                             const char *target, const char *message);
int protocol_build_client_list(char *buf, size_t buflen);
int protocol_build_client_history(char *buf, size_t buflen);
int protocol_build_client_kick(char *buf, size_t buflen, const char *username);
int protocol_build_client_quit(char *buf, size_t buflen);

int protocol_parse_server_line(const char *line, parsed_resp_t *out);
int protocol_build_server_ok(char *buf, size_t buflen, const char *msg);
int protocol_build_server_err(char *buf, size_t buflen, const char *msg);
int protocol_build_server_welcome(char *buf, size_t buflen, const char *username);
int protocol_build_server_broadcast(char *buf, size_t buflen,
                                    const char *username,
                                    const char *timestamp,
                                    const char *room,
                                    const char *message);
int protocol_build_server_pm(char *buf, size_t buflen,
                               const char *from_user,
                               const char *timestamp,
                               const char *message);
int protocol_build_server_user_join(char *buf, size_t buflen,
                                    const char *username, const char *room);
int protocol_build_server_user_left(char *buf, size_t buflen,
                                    const char *username, const char *room);
int protocol_build_server_room_joined(char *buf, size_t buflen, const char *room);
int protocol_build_server_history(char *buf, size_t buflen, const char *payload);
int protocol_build_server_kicked(char *buf, size_t buflen, const char *reason);
int protocol_build_server_list(char *buf, size_t buflen, const char *user_list);

const char *protocol_cmd_name(client_cmd_t cmd);
const char *protocol_resp_name(server_resp_t type);

#endif /* PROTOCOL_H */
