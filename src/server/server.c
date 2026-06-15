/*
 * server.c - Core TCP chat server orchestration
 *
 * Architecture overview
 * ---------------------
 *   Main thread       : socket/bind/listen/accept loop; spawns detached client threads
 *   Client threads    : One per TCP connection; recv/send protocol lines; enqueue broadcasts
 *   Broadcaster thread: Dequeues events and send()s formatted lines to all registered clients
 *
 * Concurrency model
 * -----------------
 *   Thread-per-client (detached pthreads) — simple blocking I/O, suitable for up to
 *   MAX_CLIENTS (100) simultaneous connections on Fedora.
 *
 * Synchronization
 * ---------------
 *   srv->conn_lock     : Protects active_connections (accept + disconnect paths)
 *   registry.lock      : Protects the online-user linked list (in client_registry.c)
 *   broadcast_queue    : Mutex + condition variable (in message_queue.c)
 *
 * Memory ownership
 * ----------------
 *   client_thread_arg_t : malloc'd in accept loop, free'd at handler entry
 *   client_node_t       : malloc'd in registry_add, free'd in registry_remove/destroy
 *   mq_node_t           : malloc'd in mq_push, free'd in mq_pop/destroy
 */

#include "server.h"

#include "commands.h"
#include "protocol.h"
#include "utils.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* Broadcaster thread entry point defined in broadcaster.c */
extern void *broadcaster_thread(void *arg);

/* Global pointer for async-signal-safe shutdown flag updates */
static chat_server_t *g_server = NULL;

/* Per-client thread argument passed from accept loop to handler */
typedef struct {
    chat_server_t *srv;
    int client_fd;
} client_thread_arg_t;

/* -------------------------------------------------------------------------- */
/* Connection counter helpers (protected by srv->conn_lock)                   */
/* -------------------------------------------------------------------------- */

static void conn_track_increment(chat_server_t *srv)
{
    /*
     * pthread_mutex_lock() - Acquire the connection counter mutex.
     *
     * Parameters:
     *   &srv->conn_lock  Pointer to the mutex guarding active_connections.
     *
     * Returns:
     *   0 on success; non-zero error code on failure.
     *
     * Why here:
     *   The main accept loop and every client handler thread may update
     *   active_connections concurrently.  We serialize those updates to
     *   enforce the MAX_CLIENTS (100) limit and keep statistics accurate.
     */
    pthread_mutex_lock(&srv->conn_lock);
    srv->active_connections++;
    /*
     * pthread_mutex_unlock() - Release the connection counter mutex.
     *
     * Parameters:
     *   &srv->conn_lock  Same mutex acquired above.
     *
     * Returns:
     *   0 on success; non-zero on failure (e.g., unlock by non-owner).
     *
     * Why here:
     *   Critical section is limited to a single integer increment so the
     *   lock is held for the shortest time possible.
     */
    pthread_mutex_unlock(&srv->conn_lock);
}

static void conn_track_decrement(chat_server_t *srv)
{
    /*
     * pthread_mutex_lock() - Acquire mutex before decrementing live session count.
     * See conn_track_increment() for full parameter/return documentation.
     */
    pthread_mutex_lock(&srv->conn_lock);
    if (srv->active_connections > 0) {
        srv->active_connections--;
    }
    /*
     * pthread_mutex_unlock() - Release mutex after counter update.
     * See conn_track_increment() for full parameter/return documentation.
     */
    pthread_mutex_unlock(&srv->conn_lock);
}

static bool conn_at_capacity(chat_server_t *srv)
{
    bool full;

    /*
     * pthread_mutex_lock() - Serialize read of active_connections vs MAX_CLIENTS.
     * Prevents a race where two accept() calls both observe count < MAX_CLIENTS
     * and together exceed the 100-client limit.
     */
    pthread_mutex_lock(&srv->conn_lock);
    full = (srv->active_connections >= MAX_CLIENTS);
    /*
     * pthread_mutex_unlock() - End of capacity check critical section.
     */
    pthread_mutex_unlock(&srv->conn_lock);

    return full;
}

/* -------------------------------------------------------------------------- */
/* Socket I/O helpers using send() and recv() directly                        */
/* -------------------------------------------------------------------------- */

/*
 * server_send_all() - Send an entire buffer reliably over a TCP socket.
 *
 * Wraps send() in a loop to handle partial sends, which are normal for
 * stream sockets when kernel buffers are nearly full.
 */
static int server_send_all(int fd, const char *buf, size_t len)
{
    size_t sent = 0;

    while (sent < len) {
        /*
         * send() - Transmit data on a connected TCP socket.
         *
         * Parameters:
         *   fd                  Connected socket file descriptor.
         *   buf + sent          Pointer to the next unsent byte.
         *   len - sent          Number of bytes remaining to transmit.
         *   0                   No special flags (blocking send).
         *
         * Returns:
         *   Number of bytes accepted by the kernel (> 0) on success.
         *   -1 on failure with errno set (EPIPE if peer closed, EINTR, etc.).
         *
         * Why here:
         *   All server-to-client protocol responses (WELCOME, OK, ERR) and
         *   the broadcaster's fan-out path ultimately rely on send().  TCP
         *   may accept fewer bytes than requested, so we loop until complete.
         */
        ssize_t n = send(fd, buf + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue; /* Interrupted by signal; retry send() */
            }
            return CHAT_ERR_IO;
        }
        if (n == 0) {
            return CHAT_ERR_IO; /* Unexpected on connected stream socket */
        }
        sent += (size_t)n;
    }
    return CHAT_OK;
}

static int send_response(int fd, const char *line)
{
    return server_send_all(fd, line, strlen(line));
}

/*
 * server_recv_line() - Read one newline-terminated text line from a client.
 *
 * Reads byte-by-byte via recv() until '\n' or disconnect.  Matches the
 * line-oriented application protocol (JOIN, MSG, LIST, QUIT commands).
 */
static int server_recv_line(int fd, char *buf, size_t buflen)
{
    size_t pos = 0;

    if (buf == NULL || buflen < 2) {
        return CHAT_ERR;
    }

    while (pos < buflen - 1) {
        char c;
        /*
         * recv() - Receive data from a connected TCP socket.
         *
         * Parameters:
         *   fd       Connected client socket file descriptor.
         *   &c       Buffer for a single incoming byte.
         *   1        Request exactly one byte (line-oriented parsing).
         *   0        No special flags (blocking receive).
         *
         * Returns:
         *   Number of bytes received (> 0) on success.
         *   0 when the peer has performed an orderly shutdown (EOF).
         *   -1 on failure with errno set (EINTR, ECONNRESET, etc.).
         *
         * Why here:
         *   Each client handler thread blocks in recv() waiting for the next
         *   protocol command.  Byte-at-a-time reading simplifies delimiter
         *   detection without needing a separate framing layer.
         */
        ssize_t n = recv(fd, &c, 1, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue; /* Signal interrupted recv(); retry */
            }
            return CHAT_ERR_IO;
        }
        if (n == 0) {
            /* Peer closed connection (recv returns 0 at EOF) */
            return (pos == 0) ? CHAT_ERR_IO : CHAT_OK;
        }
        buf[pos++] = c;
        if (c == '\n') {
            break;
        }
    }

    buf[pos] = '\0';
    trim_newline(buf);
    return CHAT_OK;
}

/* -------------------------------------------------------------------------- */
/* Command dispatch — handlers live in commands.c                           */
/* -------------------------------------------------------------------------- */

static void disconnect_client(chat_server_t *srv, int fd,
                              const char *username, int authenticated)
{
    cmd_disconnect_client(srv, fd, username, authenticated);
    conn_track_decrement(srv);
}

/* -------------------------------------------------------------------------- */
/* Per-client handler thread                                                  */
/* -------------------------------------------------------------------------- */

/*
 * client_handler() - Thread entry point for one connected client.
 *
 * Reads protocol commands with recv(), dispatches to handlers, and on exit
 * performs cleanup including leave notification and close().
 */
static void *client_handler(void *arg)
{
    client_thread_arg_t *targ = arg;
    chat_server_t *srv = targ->srv;
    int fd = targ->client_fd;
    char line[MAX_LINE_LEN];
    char username[MAX_USERNAME_LEN] = {0};
    int authenticated = 0;

    free(targ); /* Ownership transferred from accept loop to this thread */

    while (srv->running) {
        parsed_cmd_t cmd;
        char errline[MAX_LINE_LEN];
        int rc = server_recv_line(fd, line, sizeof(line));

        if (rc != CHAT_OK) {
            break; /* recv() error or peer disconnect */
        }

        if (protocol_parse_client_line(line, &cmd) != CHAT_OK) {
            protocol_build_server_err(errline, sizeof(errline), "invalid command");
            if (send_response(fd, errline) != CHAT_OK) {
                break;
            }
            continue;
        }

        if (!authenticated) {
            if (cmd.cmd != CMD_AUTH) {
                protocol_build_server_err(errline, sizeof(errline),
                                          "authentication required: AUTH user pass");
                if (send_response(fd, errline) != CHAT_OK) {
                    break;
                }
                continue;
            }
            if (cmd_handle_auth(srv, fd, cmd.arg1, cmd.arg2) != CHAT_OK) {
                break;
            }
            authenticated = 1;
            strncpy(username, cmd.arg1, MAX_USERNAME_LEN - 1);
            username[MAX_USERNAME_LEN - 1] = '\0';
            continue;
        }

        switch (cmd.cmd) {
        case CMD_JOIN_ROOM:
            if (cmd_handle_join_room(srv, fd, cmd.arg1) != CHAT_OK) {
                goto done;
            }
            break;
        case CMD_MSG:
            if (cmd_handle_msg(srv, fd, cmd.arg2) != CHAT_OK) {
                goto done;
            }
            break;
        case CMD_PM:
            if (cmd_handle_pm(srv, fd, cmd.arg1, cmd.arg2) != CHAT_OK) {
                goto done;
            }
            break;
        case CMD_LIST:
            if (cmd_handle_list(srv, fd) != CHAT_OK) {
                goto done;
            }
            break;
        case CMD_HISTORY:
            if (cmd_handle_history(srv, fd) != CHAT_OK) {
                goto done;
            }
            break;
        case CMD_KICK:
            if (cmd_handle_kick(srv, fd, cmd.arg1) != CHAT_OK) {
                goto done;
            }
            break;
        case CMD_QUIT:
            goto done;
        default:
            protocol_build_server_err(errline, sizeof(errline), "unknown command");
            if (send_response(fd, errline) != CHAT_OK) {
                goto done;
            }
            break;
        }
    }

done:
    disconnect_client(srv, fd, username, authenticated);
    return NULL;
}

/* -------------------------------------------------------------------------- */
/* Signal handling for graceful shutdown                                      */
/* -------------------------------------------------------------------------- */

static void signal_handler(int sig)
{
    (void)sig;
    /*
     * Only set a volatile flag — async-signal-safe.
     * The main accept loop observes srv->running and exits cleanly.
     */
    if (g_server != NULL) {
        g_server->running = 0;
    }
}

void server_install_signal_handlers(chat_server_t *srv)
{
    struct sigaction sa;

    g_server = srv;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

/* -------------------------------------------------------------------------- */
/* Server lifecycle                                                           */
/* -------------------------------------------------------------------------- */

int server_init(chat_server_t *srv, uint16_t port, const char *users_db)
{
    if (srv == NULL) {
        return CHAT_ERR;
    }

    memset(srv, 0, sizeof(*srv));
    srv->port = port;
    srv->listen_fd = -1;
    srv->running = 1;
    srv->active_connections = 0;

    if (pthread_mutex_init(&srv->conn_lock, NULL) != 0) {
        return CHAT_ERR;
    }

    if (registry_init(&srv->registry) != CHAT_OK) {
        pthread_mutex_destroy(&srv->conn_lock);
        return CHAT_ERR;
    }

    if (mq_init(&srv->broadcast_queue) != CHAT_OK) {
        registry_destroy(&srv->registry);
        pthread_mutex_destroy(&srv->conn_lock);
        return CHAT_ERR;
    }

    if (room_manager_init(&srv->rooms) != CHAT_OK) {
        mq_destroy(&srv->broadcast_queue);
        registry_destroy(&srv->registry);
        pthread_mutex_destroy(&srv->conn_lock);
        return CHAT_ERR;
    }

    if (auth_init(&srv->auth, users_db) != CHAT_OK) {
        room_manager_destroy(&srv->rooms);
        mq_destroy(&srv->broadcast_queue);
        registry_destroy(&srv->registry);
        pthread_mutex_destroy(&srv->conn_lock);
        return CHAT_ERR;
    }

    return CHAT_OK;
}

int server_run(chat_server_t *srv)
{
    struct sockaddr_in addr;
    int opt = 1;

    /*
     * socket() - Creates an endpoint for communication.
     *
     * Parameters:
     *   AF_INET      IPv4 address family (Internet sockets).
     *   SOCK_STREAM  TCP — reliable, connection-oriented byte stream.
     *   0            Default protocol for the given type (IPPROTO_TCP).
     *
     * Returns:
     *   Non-negative file descriptor on success.
     *   -1 on failure with errno set (EACCES, EMFILE, ENFILE, etc.).
     *
     * Why here:
     *   Creates the listening socket that will accept up to MAX_CLIENTS (100)
     *   simultaneous client TCP connections on the configured port.
     */
    srv->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv->listen_fd < 0) {
        perror("socket");
        return CHAT_ERR;
    }

    if (setsockopt(srv->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(srv->listen_fd);
        srv->listen_fd = -1;
        return CHAT_ERR;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY); /* Listen on all interfaces */
    addr.sin_port = htons(srv->port);

    /*
     * bind() - Assigns a local address (IP + port) to the socket.
     *
     * Parameters:
     *   srv->listen_fd              Listening socket descriptor from socket().
     *   (struct sockaddr *)&addr    IPv4 address structure (INADDR_ANY:port).
     *   sizeof(addr)                Size of the address structure.
     *
     * Returns:
     *   0 on success.
     *   -1 on failure with errno set (EADDRINUSE if port taken, EACCES, etc.).
     *
     * Why here:
     *   Binds the server to the configured TCP port (default 8080) so clients
     *   know where to connect.  SO_REUSEADDR allows quick restart after shutdown.
     */
    if (bind(srv->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(srv->listen_fd);
        srv->listen_fd = -1;
        return CHAT_ERR;
    }

    /*
     * listen() - Marks the socket as passive (ready to accept connections).
     *
     * Parameters:
     *   srv->listen_fd  Bound listening socket descriptor.
     *   BACKLOG         Maximum length of the kernel's pending-connection queue.
     *
     * Returns:
     *   0 on success.
     *   -1 on failure with errno set.
     *
     * Why here:
     *   Transitions the socket from bound to listening state.  Incoming TCP
     *   SYN packets are queued (up to BACKLOG) until accept() retrieves them.
     */
    if (listen(srv->listen_fd, BACKLOG) < 0) {
        perror("listen");
        close(srv->listen_fd);
        srv->listen_fd = -1;
        return CHAT_ERR;
    }

    /*
     * pthread_create() - Spawn the dedicated broadcaster thread.
     *
     * Parameters:
     *   &srv->broadcaster_tid  Output location for the new thread's ID.
     *   NULL                   Default thread attributes (joinable).
     *   broadcaster_thread     Entry function (defined in broadcaster.c).
     *   srv                    Shared server state (queue + registry).
     *
     * Returns:
     *   0 on success; error number on failure.
     *
     * Why here:
     *   Decouples fan-out send() operations from client handler threads.
     *   Handlers enqueue MQ_CHAT / MQ_JOIN / MQ_LEAVE events; the broadcaster
     *   formats protocol lines and send()s them to every registered client.
     */
    if (pthread_create(&srv->broadcaster_tid, NULL, broadcaster_thread, srv) != 0) {
        fprintf(stderr, "failed to start broadcaster thread\n");
        close(srv->listen_fd);
        srv->listen_fd = -1;
        return CHAT_ERR;
    }

    printf("Chat server listening on port %u (max %d clients)\n", srv->port, MAX_CLIENTS);
    printf("Press Ctrl+C to shut down gracefully\n");

    /* ---- Main accept loop: one thread per accepted TCP connection ---- */
    while (srv->running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        /*
         * accept() - Accepts an incoming TCP connection on the listening socket.
         *
         * Parameters:
         *   srv->listen_fd                    Listening socket descriptor.
         *   (struct sockaddr *)&client_addr   Output: remote client address.
         *   &client_len                       Input/output: address buffer size.
         *
         * Returns:
         *   New connected socket fd (>= 0) on success.
         *   -1 on failure with errno set (EINTR, EAGAIN, etc.).
         *
         * Why here:
         *   Blocks until a client completes the TCP three-way handshake, then
         *   returns a new fd for bidirectional recv()/send() with that client.
         */
        int client_fd = accept(srv->listen_fd, (struct sockaddr *)&client_addr, &client_len);

        if (client_fd < 0) {
            if (errno == EINTR) {
                continue; /* Signal interrupted accept(); check srv->running */
            }
            if (!srv->running) {
                break; /* Graceful shutdown in progress */
            }
            perror("accept");
            continue;
        }

        /* Enforce 100-client limit before allocating thread resources */
        if (conn_at_capacity(srv)) {
            char rej[MAX_LINE_LEN];
            protocol_build_server_err(rej, sizeof(rej), "server full (max 100 clients)");
            send_response(client_fd, rej);
            /*
             * close() - Reject excess connection by closing its socket immediately.
             * See disconnect_client() for full close() documentation.
             */
            close(client_fd);
            continue;
        }

        conn_track_increment(srv);

        client_thread_arg_t *targ = malloc(sizeof(*targ));
        if (targ == NULL) {
            close(client_fd);
            conn_track_decrement(srv);
            continue;
        }

        targ->srv = srv;
        targ->client_fd = client_fd;

        pthread_t tid;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

        /*
         * pthread_create() - Spawn a detached handler thread for this client.
         *
         * Parameters:
         *   &tid             Output thread identifier (not joined; detached).
         *   &attr            PTHREAD_CREATE_DETACHED — no join needed on exit.
         *   client_handler   Reads recv() loop, dispatches protocol commands.
         *   targ             Heap-allocated {srv, client_fd}; freed in handler.
         *
         * Returns:
         *   0 on success; error number on failure.
         *
         * Why here:
         *   Each accepted connection gets its own thread so blocking recv()
         *   on one client does not stall others — essential for 100 concurrent users.
         */
        if (pthread_create(&tid, &attr, client_handler, targ) != 0) {
            free(targ);
            close(client_fd);
            conn_track_decrement(srv);
            fprintf(stderr, "pthread_create failed for client fd=%d\n", client_fd);
        }
        pthread_attr_destroy(&attr);

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
        printf("Client connected from %s:%u (fd=%d, sessions=%d)\n",
               ip, ntohs(client_addr.sin_port), client_fd, srv->active_connections);
    }

    return CHAT_OK;
}

void server_shutdown(chat_server_t *srv)
{
    mq_message_t shutdown_msg;

    if (srv == NULL) {
        return;
    }

    srv->running = 0;

    if (srv->listen_fd >= 0) {
        /*
         * close() - Close listening socket to unblock accept() during shutdown.
         * Any blocked accept() will fail, allowing the main loop to exit.
         */
        close(srv->listen_fd);
        srv->listen_fd = -1;
    }

    /* Wake broadcaster thread so it can exit cleanly */
    shutdown_msg.type = MQ_SHUTDOWN;
    mq_signal_shutdown(&srv->broadcast_queue);
    mq_push(&srv->broadcast_queue, &shutdown_msg);

    if (srv->broadcaster_tid) {
        pthread_join(srv->broadcaster_tid, NULL);
    }

    printf("Server shutdown complete (remaining sessions will drain on disconnect)\n");
}

void server_destroy(chat_server_t *srv)
{
    if (srv == NULL) {
        return;
    }

    mq_destroy(&srv->broadcast_queue);
    registry_destroy(&srv->registry);
    room_manager_destroy(&srv->rooms);
    auth_destroy(&srv->auth);
    pthread_mutex_destroy(&srv->conn_lock);

    if (srv->listen_fd >= 0) {
        close(srv->listen_fd);
        srv->listen_fd = -1;
    }
}
