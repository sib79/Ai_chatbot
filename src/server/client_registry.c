#include "client_registry.h"

#include <stdlib.h>
#include <string.h>

int registry_init(client_registry_t *reg)
{
    if (reg == NULL) {
        return CHAT_ERR;
    }

    reg->head = NULL;
    reg->count = 0;

    if (pthread_mutex_init(&reg->lock, NULL) != 0) {
        return CHAT_ERR;
    }

    return CHAT_OK;
}

static void registry_free_nodes(client_node_t *node)
{
    while (node != NULL) {
        client_node_t *next = node->next;
        free(node);
        node = next;
    }
}

void registry_destroy(client_registry_t *reg)
{
    if (reg == NULL) {
        return;
    }

    pthread_mutex_lock(&reg->lock);
    registry_free_nodes(reg->head);
    reg->head = NULL;
    reg->count = 0;
    pthread_mutex_unlock(&reg->lock);

    pthread_mutex_destroy(&reg->lock);
}

int registry_add(client_registry_t *reg, int sockfd, const char *username,
                 const char *room, bool is_admin)
{
    client_node_t *node;
    client_node_t *cur;

    if (reg == NULL || username == NULL || room == NULL) {
        return CHAT_ERR;
    }

    node = malloc(sizeof(*node));
    if (node == NULL) {
        return CHAT_ERR_NOMEM;
    }

    node->sockfd = sockfd;
    strncpy(node->username, username, MAX_USERNAME_LEN - 1);
    strncpy(node->room, room, MAX_ROOM_NAME_LEN - 1);
    node->authenticated = true;
    node->is_admin = is_admin;
    node->next = NULL;

    pthread_mutex_lock(&reg->lock);
    if (reg->count >= MAX_CLIENTS) {
        pthread_mutex_unlock(&reg->lock);
        free(node);
        return CHAT_ERR_FULL;
    }

    cur = reg->head;
    while (cur != NULL) {
        if (strcmp(cur->username, username) == 0) {
            pthread_mutex_unlock(&reg->lock);
            free(node);
            return CHAT_ERR_DUPLICATE;
        }
        cur = cur->next;
    }

    node->next = reg->head;
    reg->head = node;
    reg->count++;
    pthread_mutex_unlock(&reg->lock);

    return CHAT_OK;
}

int registry_remove(client_registry_t *reg, int sockfd)
{
    client_node_t *prev = NULL;
    client_node_t *cur;

    if (reg == NULL) {
        return CHAT_ERR;
    }

    pthread_mutex_lock(&reg->lock);
    cur = reg->head;
    while (cur != NULL) {
        if (cur->sockfd == sockfd) {
            if (prev == NULL) {
                reg->head = cur->next;
            } else {
                prev->next = cur->next;
            }
            reg->count--;
            pthread_mutex_unlock(&reg->lock);
            free(cur);
            return CHAT_OK;
        }
        prev = cur;
        cur = cur->next;
    }
    pthread_mutex_unlock(&reg->lock);

    return CHAT_ERR_NOT_FOUND;
}

bool registry_username_exists(client_registry_t *reg, const char *username)
{
    client_node_t *cur;
    bool found = false;

    if (reg == NULL || username == NULL) {
        return false;
    }

    pthread_mutex_lock(&reg->lock);
    cur = reg->head;
    while (cur != NULL) {
        if (strcmp(cur->username, username) == 0) {
            found = true;
            break;
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&reg->lock);

    return found;
}

int registry_get_username(client_registry_t *reg, int sockfd, char *buf, size_t buflen)
{
    client_node_t *cur;

    if (reg == NULL || buf == NULL || buflen == 0) {
        return CHAT_ERR;
    }

    buf[0] = '\0';

    pthread_mutex_lock(&reg->lock);
    cur = reg->head;
    while (cur != NULL) {
        if (cur->sockfd == sockfd) {
            strncpy(buf, cur->username, buflen - 1);
            buf[buflen - 1] = '\0';
            pthread_mutex_unlock(&reg->lock);
            return CHAT_OK;
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&reg->lock);

    return CHAT_ERR_NOT_FOUND;
}

int registry_get_room(client_registry_t *reg, int sockfd, char *buf, size_t buflen)
{
    client_node_t *cur;

    if (reg == NULL || buf == NULL || buflen == 0) {
        return CHAT_ERR;
    }

    buf[0] = '\0';

    pthread_mutex_lock(&reg->lock);
    cur = reg->head;
    while (cur != NULL) {
        if (cur->sockfd == sockfd) {
            strncpy(buf, cur->room, buflen - 1);
            buf[buflen - 1] = '\0';
            pthread_mutex_unlock(&reg->lock);
            return CHAT_OK;
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&reg->lock);

    return CHAT_ERR_NOT_FOUND;
}

int registry_set_room(client_registry_t *reg, int sockfd, const char *room)
{
    client_node_t *cur;

    if (reg == NULL || room == NULL) {
        return CHAT_ERR;
    }

    pthread_mutex_lock(&reg->lock);
    cur = reg->head;
    while (cur != NULL) {
        if (cur->sockfd == sockfd) {
            strncpy(cur->room, room, MAX_ROOM_NAME_LEN - 1);
            cur->room[MAX_ROOM_NAME_LEN - 1] = '\0';
            pthread_mutex_unlock(&reg->lock);
            return CHAT_OK;
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&reg->lock);

    return CHAT_ERR_NOT_FOUND;
}

int registry_is_admin(client_registry_t *reg, int sockfd)
{
    client_node_t *cur;
    int admin = 0;

    pthread_mutex_lock(&reg->lock);
    cur = reg->head;
    while (cur != NULL) {
        if (cur->sockfd == sockfd) {
            admin = cur->is_admin ? 1 : 0;
            break;
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&reg->lock);

    return admin;
}

int registry_get_sockfd_by_username(client_registry_t *reg, const char *username)
{
    client_node_t *cur;
    int fd = -1;

    if (reg == NULL || username == NULL) {
        return -1;
    }

    pthread_mutex_lock(&reg->lock);
    cur = reg->head;
    while (cur != NULL) {
        if (strcmp(cur->username, username) == 0) {
            fd = cur->sockfd;
            break;
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&reg->lock);

    return fd;
}

size_t registry_count(client_registry_t *reg)
{
    size_t count;

    if (reg == NULL) {
        return 0;
    }

    pthread_mutex_lock(&reg->lock);
    count = reg->count;
    pthread_mutex_unlock(&reg->lock);

    return count;
}

int registry_build_user_list(client_registry_t *reg, char *buf, size_t buflen)
{
    client_node_t *cur;
    size_t used = 0;
    bool first = true;

    if (reg == NULL || buf == NULL || buflen == 0) {
        return CHAT_ERR;
    }

    buf[0] = '\0';

    pthread_mutex_lock(&reg->lock);
    cur = reg->head;
    while (cur != NULL) {
        int written;
        if (first) {
            written = snprintf(buf + used, buflen - used, "%s", cur->username);
            first = false;
        } else {
            written = snprintf(buf + used, buflen - used, ",%s", cur->username);
        }
        if (written < 0 || (size_t)written >= buflen - used) {
            pthread_mutex_unlock(&reg->lock);
            return CHAT_ERR;
        }
        used += (size_t)written;
        cur = cur->next;
    }
    pthread_mutex_unlock(&reg->lock);

    return CHAT_OK;
}

int registry_build_room_user_list(client_registry_t *reg, const char *room,
                                  char *buf, size_t buflen)
{
    client_node_t *cur;
    size_t used = 0;
    bool first = true;

    if (reg == NULL || room == NULL || buf == NULL || buflen == 0) {
        return CHAT_ERR;
    }

    buf[0] = '\0';

    pthread_mutex_lock(&reg->lock);
    cur = reg->head;
    while (cur != NULL) {
        int written;
        if (strcmp(cur->room, room) != 0) {
            cur = cur->next;
            continue;
        }
        if (first) {
            written = snprintf(buf + used, buflen - used, "%s", cur->username);
            first = false;
        } else {
            written = snprintf(buf + used, buflen - used, ",%s", cur->username);
        }
        if (written < 0 || (size_t)written >= buflen - used) {
            pthread_mutex_unlock(&reg->lock);
            return CHAT_ERR;
        }
        used += (size_t)written;
        cur = cur->next;
    }
    pthread_mutex_unlock(&reg->lock);

    return CHAT_OK;
}

int registry_foreach(client_registry_t *reg, registry_foreach_fn fn, void *ctx)
{
    client_node_t *cur;
    int rc = CHAT_OK;

    if (reg == NULL || fn == NULL) {
        return CHAT_ERR;
    }

    pthread_mutex_lock(&reg->lock);
    cur = reg->head;
    while (cur != NULL) {
        client_node_t snapshot = *cur;
        pthread_mutex_unlock(&reg->lock);

        rc = fn(&snapshot, ctx);
        if (rc != CHAT_OK) {
            return rc;
        }

        pthread_mutex_lock(&reg->lock);
        cur = cur->next;
    }
    pthread_mutex_unlock(&reg->lock);

    return CHAT_OK;
}

int registry_foreach_in_room(client_registry_t *reg, const char *room,
                             registry_foreach_fn fn, void *ctx)
{
    client_node_t *cur;
    int rc = CHAT_OK;

    if (reg == NULL || room == NULL || fn == NULL) {
        return CHAT_ERR;
    }

    pthread_mutex_lock(&reg->lock);
    cur = reg->head;
    while (cur != NULL) {
        client_node_t snapshot = *cur;
        if (strcmp(cur->room, room) == 0) {
            pthread_mutex_unlock(&reg->lock);
            rc = fn(&snapshot, ctx);
            if (rc != CHAT_OK) {
                return rc;
            }
            pthread_mutex_lock(&reg->lock);
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&reg->lock);

    return CHAT_OK;
}
