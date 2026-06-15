#ifndef CLIENT_H
#define CLIENT_H

#include <pthread.h>
#include <stdint.h>

#include "common.h"

typedef struct {
    int sockfd;
    char username[MAX_USERNAME_LEN];
    char password[MAX_PASSWORD_LEN];
    char current_room[MAX_ROOM_NAME_LEN];
    volatile int running;
    int authenticated;
    int joined;
    pthread_mutex_t lock;
    pthread_mutex_t io_lock;
    pthread_t receiver_tid;
    int receiver_started;
} chat_client_t;

int client_init(chat_client_t *cli);
int client_connect(chat_client_t *cli, const char *host, uint16_t port);
int client_run(chat_client_t *cli, const char *username_override,
               const char *password_override);
void client_disconnect(chat_client_t *cli);
void client_destroy(chat_client_t *cli);

#endif /* CLIENT_H */
