#include "server.h"

#include "protocol.h"
#include "utils.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    const char *line;
    const char *room;
} broadcast_ctx_t;

static int send_to_client(client_node_t *node, void *ctx)
{
    broadcast_ctx_t *bctx = ctx;

    if (strcmp(node->room, bctx->room) != 0) {
        return CHAT_OK;
    }

    if (send_all(node->sockfd, bctx->line, strlen(bctx->line)) != CHAT_OK) {
        return CHAT_ERR_IO;
    }
    return CHAT_OK;
}

static void broadcast_line_to_room(client_registry_t *registry,
                                   const char *room, const char *line)
{
    broadcast_ctx_t ctx = {
        .line = line,
        .room = room
    };
    (void)registry_foreach(registry, send_to_client, &ctx);
}

void *broadcaster_thread(void *arg)
{
    chat_server_t *srv = arg;
    mq_message_t msg;
    char line[MAX_LINE_LEN];
    char ts[32];

    while (srv->running) {
        if (mq_pop(&srv->broadcast_queue, &msg) != CHAT_OK) {
            break;
        }

        if (msg.type == MQ_SHUTDOWN) {
            break;
        }

        format_timestamp(msg.timestamp, ts, sizeof(ts));

        switch (msg.type) {
        case MQ_CHAT:
            if (protocol_build_server_broadcast(line, sizeof(line),
                                                msg.username, ts, msg.room,
                                                msg.content) == CHAT_OK) {
                broadcast_line_to_room(&srv->registry, msg.room, line);
            }
            break;
        case MQ_JOIN:
        case MQ_ROOM_JOIN:
            if (protocol_build_server_user_join(line, sizeof(line),
                                                msg.username, msg.room) == CHAT_OK) {
                broadcast_line_to_room(&srv->registry, msg.room, line);
            }
            break;
        case MQ_LEAVE:
        case MQ_ROOM_LEAVE:
            if (protocol_build_server_user_left(line, sizeof(line),
                                                msg.username, msg.room) == CHAT_OK) {
                broadcast_line_to_room(&srv->registry, msg.room, line);
            }
            break;
        default:
            break;
        }
    }

    return NULL;
}
