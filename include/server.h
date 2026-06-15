#ifndef SERVER_H
#define SERVER_H

#include <pthread.h>
#include <stdint.h>

#include "auth.h"
#include "client_registry.h"
#include "message_queue.h"
#include "room_manager.h"

typedef struct {
    int listen_fd;
    uint16_t port;
    volatile int running;
    int active_connections;
    pthread_mutex_t conn_lock;
    client_registry_t registry;
    message_queue_t broadcast_queue;
    pthread_t broadcaster_tid;
    auth_db_t auth;
    room_manager_t rooms;
} chat_server_t;

int server_init(chat_server_t *srv, uint16_t port, const char *users_db);
int server_run(chat_server_t *srv);
void server_shutdown(chat_server_t *srv);
void server_destroy(chat_server_t *srv);

void server_install_signal_handlers(chat_server_t *srv);

#endif /* SERVER_H */
