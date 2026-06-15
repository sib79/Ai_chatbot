#ifndef ROOM_MANAGER_H
#define ROOM_MANAGER_H

#include <pthread.h>
#include <time.h>

#include "common.h"

typedef struct {
    char username[MAX_USERNAME_LEN];
    char content[MAX_MSG_LEN];
    char timestamp[32];
} history_entry_t;

typedef struct room_node {
    char name[MAX_ROOM_NAME_LEN];
    history_entry_t history[MAX_ROOM_HISTORY];
    size_t count;
    size_t start;
    struct room_node *next;
} room_node_t;

typedef struct {
    room_node_t *rooms;
    size_t room_count;
    pthread_mutex_t lock;
} room_manager_t;

int room_manager_init(room_manager_t *mgr);
void room_manager_destroy(room_manager_t *mgr);

int room_history_add(room_manager_t *mgr, const char *room,
                     const char *username, const char *content, time_t ts);

int room_history_format(room_manager_t *mgr, const char *room,
                        char *buf, size_t buflen);

int room_history_send_to_client(room_manager_t *mgr, const char *room, int fd,
                                int (*send_fn)(int fd, const char *line));

#endif /* ROOM_MANAGER_H */
