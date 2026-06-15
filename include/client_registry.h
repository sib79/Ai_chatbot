#ifndef CLIENT_REGISTRY_H
#define CLIENT_REGISTRY_H

#include <pthread.h>
#include <stdbool.h>

#include "common.h"

typedef struct client_node {
    int sockfd;
    char username[MAX_USERNAME_LEN];
    char room[MAX_ROOM_NAME_LEN];
    bool authenticated;
    bool is_admin;
    struct client_node *next;
} client_node_t;

typedef struct {
    client_node_t *head;
    size_t count;
    pthread_mutex_t lock;
} client_registry_t;

int registry_init(client_registry_t *reg);
void registry_destroy(client_registry_t *reg);

int registry_add(client_registry_t *reg, int sockfd, const char *username,
                 const char *room, bool is_admin);
int registry_get_username(client_registry_t *reg, int sockfd, char *buf, size_t buflen);
int registry_get_room(client_registry_t *reg, int sockfd, char *buf, size_t buflen);
int registry_set_room(client_registry_t *reg, int sockfd, const char *room);
int registry_is_admin(client_registry_t *reg, int sockfd);
int registry_get_sockfd_by_username(client_registry_t *reg, const char *username);
int registry_remove(client_registry_t *reg, int sockfd);
bool registry_username_exists(client_registry_t *reg, const char *username);
size_t registry_count(client_registry_t *reg);

int registry_build_user_list(client_registry_t *reg, char *buf, size_t buflen);
int registry_build_room_user_list(client_registry_t *reg, const char *room,
                                  char *buf, size_t buflen);

typedef int (*registry_foreach_fn)(client_node_t *node, void *ctx);
int registry_foreach(client_registry_t *reg, registry_foreach_fn fn, void *ctx);
int registry_foreach_in_room(client_registry_t *reg, const char *room,
                             registry_foreach_fn fn, void *ctx);

#endif /* CLIENT_REGISTRY_H */
