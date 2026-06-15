#ifndef MESSAGE_QUEUE_H
#define MESSAGE_QUEUE_H

#include <pthread.h>
#include <stdbool.h>
#include <time.h>

#include "common.h"

typedef enum {
    MQ_CHAT = 0,
    MQ_JOIN,
    MQ_LEAVE,
    MQ_ROOM_JOIN,
    MQ_ROOM_LEAVE,
    MQ_SHUTDOWN
} mq_event_type_t;

typedef struct {
    mq_event_type_t type;
    char username[MAX_USERNAME_LEN];
    char room[MAX_ROOM_NAME_LEN];
    char content[MAX_MSG_LEN];
    time_t timestamp;
} mq_message_t;

typedef struct mq_node {
    mq_message_t msg;
    struct mq_node *next;
} mq_node_t;

typedef struct {
    mq_node_t *head;
    mq_node_t *tail;
    size_t count;
    bool shutdown;
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
} message_queue_t;

int mq_init(message_queue_t *q);
void mq_destroy(message_queue_t *q);

int mq_push(message_queue_t *q, const mq_message_t *msg);
int mq_pop(message_queue_t *q, mq_message_t *out);
void mq_signal_shutdown(message_queue_t *q);

#endif /* MESSAGE_QUEUE_H */
