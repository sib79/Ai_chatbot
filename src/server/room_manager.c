#include "room_manager.h"

#include "protocol.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static room_node_t *room_find_or_create(room_manager_t *mgr, const char *name)
{
    room_node_t *cur = mgr->rooms;

    while (cur != NULL) {
        if (strcmp(cur->name, name) == 0) {
            return cur;
        }
        cur = cur->next;
    }

    if (mgr->room_count >= MAX_ROOMS) {
        return NULL;
    }

    cur = calloc(1, sizeof(*cur));
    if (cur == NULL) {
        return NULL;
    }

    strncpy(cur->name, name, MAX_ROOM_NAME_LEN - 1);
    cur->next = mgr->rooms;
    mgr->rooms = cur;
    mgr->room_count++;
    return cur;
}

int room_manager_init(room_manager_t *mgr)
{
    if (mgr == NULL) {
        return CHAT_ERR;
    }

    mgr->rooms = NULL;
    mgr->room_count = 0;

    if (pthread_mutex_init(&mgr->lock, NULL) != 0) {
        return CHAT_ERR;
    }

    /* Ensure default lobby exists */
    pthread_mutex_lock(&mgr->lock);
    if (room_find_or_create(mgr, DEFAULT_ROOM) == NULL) {
        pthread_mutex_unlock(&mgr->lock);
        return CHAT_ERR;
    }
    pthread_mutex_unlock(&mgr->lock);

    return CHAT_OK;
}

static void room_free_all(room_node_t *node)
{
    while (node != NULL) {
        room_node_t *next = node->next;
        free(node);
        node = next;
    }
}

void room_manager_destroy(room_manager_t *mgr)
{
    if (mgr == NULL) {
        return;
    }

    pthread_mutex_lock(&mgr->lock);
    room_free_all(mgr->rooms);
    mgr->rooms = NULL;
    mgr->room_count = 0;
    pthread_mutex_unlock(&mgr->lock);
    pthread_mutex_destroy(&mgr->lock);
}

int room_history_add(room_manager_t *mgr, const char *room,
                     const char *username, const char *content, time_t ts)
{
    room_node_t *rn;
    history_entry_t *entry;
    size_t idx;

    if (mgr == NULL || room == NULL || username == NULL || content == NULL) {
        return CHAT_ERR;
    }

    pthread_mutex_lock(&mgr->lock);
    rn = room_find_or_create(mgr, room);
    if (rn == NULL) {
        pthread_mutex_unlock(&mgr->lock);
        return CHAT_ERR_FULL;
    }

    if (rn->count < MAX_ROOM_HISTORY) {
        idx = rn->count;
        rn->count++;
    } else {
        idx = rn->start;
        rn->start = (rn->start + 1) % MAX_ROOM_HISTORY;
    }

    entry = &rn->history[idx];
    strncpy(entry->username, username, MAX_USERNAME_LEN - 1);
    strncpy(entry->content, content, MAX_MSG_LEN - 1);
    format_timestamp(ts, entry->timestamp, sizeof(entry->timestamp));

    pthread_mutex_unlock(&mgr->lock);
    return CHAT_OK;
}

int room_history_format(room_manager_t *mgr, const char *room, char *buf, size_t buflen)
{
    room_node_t *cur;
    size_t used = 0;
    size_t i;
    bool first = true;

    if (mgr == NULL || room == NULL || buf == NULL || buflen == 0) {
        return CHAT_ERR;
    }

    buf[0] = '\0';

    pthread_mutex_lock(&mgr->lock);
    cur = mgr->rooms;
    while (cur != NULL && strcmp(cur->name, room) != 0) {
        cur = cur->next;
    }
    if (cur == NULL) {
        pthread_mutex_unlock(&mgr->lock);
        return CHAT_OK;
    }

    for (i = 0; i < cur->count; i++) {
        size_t idx = (cur->start + i) % MAX_ROOM_HISTORY;
        history_entry_t *e = &cur->history[idx];
        int written;

        if (first) {
            written = snprintf(buf + used, buflen - used, "%s|%s|%s",
                               e->username, e->timestamp, e->content);
            first = false;
        } else {
            written = snprintf(buf + used, buflen - used, "||%s|%s|%s",
                               e->username, e->timestamp, e->content);
        }
        if (written < 0 || (size_t)written >= buflen - used) {
            pthread_mutex_unlock(&mgr->lock);
            return CHAT_ERR;
        }
        used += (size_t)written;
    }

    pthread_mutex_unlock(&mgr->lock);
    return CHAT_OK;
}

int room_history_send_to_client(room_manager_t *mgr, const char *room, int fd,
                                int (*send_fn)(int fd, const char *line))
{
    char payload[MAX_LINE_LEN];
    char line[MAX_LINE_LEN];

    if (room_history_format(mgr, room, payload, sizeof(payload)) != CHAT_OK) {
        return CHAT_ERR;
    }

    if (protocol_build_server_history(line, sizeof(line), payload) != CHAT_OK) {
        return CHAT_ERR;
    }

    return send_fn(fd, line);
}
