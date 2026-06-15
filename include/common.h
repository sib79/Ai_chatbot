#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DEFAULT_PORT            8080
#define MAX_USERNAME_LEN        32
#define MAX_PASSWORD_LEN        64
#define MAX_MSG_LEN             512
#define MAX_LINE_LEN            2048
#define MAX_CLIENTS             100
#define MAX_ROOM_NAME_LEN       32
#define MAX_ROOM_HISTORY        100
#define MAX_ROOMS               64
#define SHA256_HEX_LEN          65
#define DEFAULT_ROOM            "lobby"
#define DEFAULT_USERS_DB        "data/users.db"
#define BACKLOG                 16
#define RECV_BUF_SIZE           4096

typedef enum {
    CHAT_OK = 0,
    CHAT_ERR = -1,
    CHAT_ERR_NOMEM = -2,
    CHAT_ERR_PROTOCOL = -3,
    CHAT_ERR_DUPLICATE = -4,
    CHAT_ERR_NOT_FOUND = -5,
    CHAT_ERR_IO = -6,
    CHAT_ERR_FULL = -7,
    CHAT_ERR_AUTH = -8,
    CHAT_ERR_FORBIDDEN = -9
} chat_status_t;

#endif /* COMMON_H */
