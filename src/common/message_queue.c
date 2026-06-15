#include "message_queue.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

int mq_init(message_queue_t *q)
{
    if (q == NULL) {
        return CHAT_ERR;
    }

    q->head = NULL;
    q->tail = NULL;
    q->count = 0;
    q->shutdown = false;

    if (pthread_mutex_init(&q->lock, NULL) != 0) {
        return CHAT_ERR;
    }
    if (pthread_cond_init(&q->not_empty, NULL) != 0) {
        pthread_mutex_destroy(&q->lock);
        return CHAT_ERR;
    }

    return CHAT_OK;
}

static void mq_free_nodes(mq_node_t *node)
{
    while (node != NULL) {
        mq_node_t *next = node->next;
        free(node);
        node = next;
    }
}

void mq_destroy(message_queue_t *q)
{
    if (q == NULL) {
        return;
    }

    pthread_mutex_lock(&q->lock);
    mq_free_nodes(q->head);
    q->head = NULL;
    q->tail = NULL;
    q->count = 0;
    pthread_mutex_unlock(&q->lock);

    pthread_cond_destroy(&q->not_empty);
    pthread_mutex_destroy(&q->lock);
}

int mq_push(message_queue_t *q, const mq_message_t *msg)
{
    mq_node_t *node;

    if (q == NULL || msg == NULL) {
        return CHAT_ERR;
    }

    node = malloc(sizeof(*node));
    if (node == NULL) {
        return CHAT_ERR_NOMEM;
    }

    node->msg = *msg;
    node->next = NULL;

    pthread_mutex_lock(&q->lock);
    if (q->shutdown) {
        pthread_mutex_unlock(&q->lock);
        free(node);
        return CHAT_ERR;
    }

    if (q->tail == NULL) {
        q->head = node;
        q->tail = node;
    } else {
        q->tail->next = node;
        q->tail = node;
    }
    q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->lock);

    return CHAT_OK;
}

int mq_pop(message_queue_t *q, mq_message_t *out)
{
    mq_node_t *node;

    if (q == NULL || out == NULL) {
        return CHAT_ERR;
    }

    pthread_mutex_lock(&q->lock);
    while (q->head == NULL && !q->shutdown) {
        pthread_cond_wait(&q->not_empty, &q->lock);
    }

    if (q->head == NULL) {
        pthread_mutex_unlock(&q->lock);
        return CHAT_ERR;
    }

    node = q->head;
    q->head = node->next;
    if (q->head == NULL) {
        q->tail = NULL;
    }
    q->count--;
    *out = node->msg;
    pthread_mutex_unlock(&q->lock);

    free(node);
    return CHAT_OK;
}

void mq_signal_shutdown(message_queue_t *q)
{
    if (q == NULL) {
        return;
    }

    pthread_mutex_lock(&q->lock);
    q->shutdown = true;
    pthread_cond_broadcast(&q->not_empty);
    pthread_mutex_unlock(&q->lock);
}
