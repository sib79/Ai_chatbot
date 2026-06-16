/*
 * client.c - Advanced TCP chat client (auth, rooms, PM, colors, dual-thread I/O)
 */

#define _POSIX_C_SOURCE 200809L

#include "client.h"

#include "colors.h"
#include "protocol.h"
#include "utils.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void client_set_running(chat_client_t *cli, int value)
{
    pthread_mutex_lock(&cli->lock);
    cli->running = value;
    pthread_mutex_unlock(&cli->lock);
}

static int client_is_running(chat_client_t *cli)
{
    int value;
    pthread_mutex_lock(&cli->lock);
    value = cli->running;
    pthread_mutex_unlock(&cli->lock);
    return value;
}

static int client_get_sockfd(chat_client_t *cli)
{
    int fd;
    pthread_mutex_lock(&cli->lock);
    fd = cli->sockfd;
    pthread_mutex_unlock(&cli->lock);
    return fd;
}

static void client_print(chat_client_t *cli, const char *fmt, ...)
{
    va_list ap;
    pthread_mutex_lock(&cli->io_lock);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
    pthread_mutex_unlock(&cli->io_lock);
}

static int client_send_all(int fd, const char *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return CHAT_ERR_IO;
        }
        if (n == 0) {
            return CHAT_ERR_IO;
        }
        sent += (size_t)n;
    }
    return CHAT_OK;
}

static int client_send_line(chat_client_t *cli, const char *line)
{
    int rc;
    int fd;
    pthread_mutex_lock(&cli->lock);
    if (cli->sockfd < 0 || !cli->running) {
        pthread_mutex_unlock(&cli->lock);
        return CHAT_ERR_IO;
    }
    fd = cli->sockfd;
    rc = client_send_all(fd, line, strlen(line));
    pthread_mutex_unlock(&cli->lock);
    return rc;
}

static int client_recv_line(chat_client_t *cli, char *buf, size_t buflen)
{
    size_t pos = 0;
    int fd = client_get_sockfd(cli);

    if (fd < 0) {
        return CHAT_ERR_IO;
    }

    while (pos < buflen - 1) {
        char c;
        ssize_t n = recv(fd, &c, 1, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return CHAT_ERR_IO;
        }
        if (n == 0) {
            return CHAT_ERR_IO;
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

static void print_history_payload(chat_client_t *cli, const char *payload)
{
    char buf[MAX_LINE_LEN];
    char *save = NULL;
    char *entry;

    if (payload == NULL || payload[0] == '\0') {
        client_print(cli, "%s(no history)%s\n> ", C_YELLOW, C_RESET);
        return;
    }

    strncpy(buf, payload, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    entry = strtok_r(buf, "||", &save);
    while (entry != NULL) {
        char user[MAX_USERNAME_LEN];
        char ts[32];
        char *msg;
        char *s2 = NULL;
        char *tok;

        tok = strtok_r(entry, "|", &s2);
        if (tok == NULL) {
            entry = strtok_r(NULL, "||", &save);
            continue;
        }
        strncpy(user, tok, MAX_USERNAME_LEN - 1);
        user[MAX_USERNAME_LEN - 1] = '\0';
        tok = strtok_r(NULL, "|", &s2);
        if (tok == NULL) {
            entry = strtok_r(NULL, "||", &save);
            continue;
        }
        strncpy(ts, tok, sizeof(ts) - 1);
        ts[sizeof(ts) - 1] = '\0';
        msg = strtok_r(NULL, "|", &s2);
        if (msg != NULL) {
            client_print(cli, "%s[history %s] %s%s%s: %s\n",
                         C_CYAN, ts, C_BOLD, user, C_RESET, msg);
        }
        entry = strtok_r(NULL, "||", &save);
    }
    client_print(cli, "> ");
}

static void display_server_message(chat_client_t *cli, const parsed_resp_t *resp)
{
    switch (resp->type) {
    case RESP_WELCOME:
        pthread_mutex_lock(&cli->lock);
        cli->authenticated = 1;
        strncpy(cli->username, resp->username, MAX_USERNAME_LEN - 1);
        cli->username[MAX_USERNAME_LEN - 1] = '\0';
        pthread_mutex_unlock(&cli->lock);
        client_print(cli, "\n%s*** Welcome, %s! ***%s\n> ",
                     C_GREEN, resp->username, C_RESET);
        break;
    case RESP_ROOM_JOINED:
        pthread_mutex_lock(&cli->lock);
        strncpy(cli->current_room, resp->room, MAX_ROOM_NAME_LEN - 1);
        cli->current_room[MAX_ROOM_NAME_LEN - 1] = '\0';
        cli->joined = 1;
        pthread_mutex_unlock(&cli->lock);
        client_print(cli, "\n%s*** Joined room: %s ***%s\n> ",
                     C_CYAN, resp->room, C_RESET);
        break;
    case RESP_BROADCAST:
        client_print(cli, "\n%s[%s] [%s] %s%s%s: %s%s\n> ",
                     C_GREEN, resp->timestamp, resp->room,
                     C_BOLD, resp->username, C_RESET, resp->message, C_RESET);
        break;
    case RESP_PM:
        client_print(cli, "\n%s[PM %s] %s%s%s: %s%s\n> ",
                     C_MAGENTA, resp->timestamp,
                     C_BOLD, resp->from_user, C_RESET, resp->message, C_RESET);
        break;
    case RESP_USER_JOIN:
        client_print(cli, "\n%s*** %s joined %s ***%s\n> ",
                     C_YELLOW, resp->username, resp->room, C_RESET);
        break;
    case RESP_USER_LEFT:
        client_print(cli, "\n%s*** %s left %s ***%s\n> ",
                     C_YELLOW, resp->username, resp->room, C_RESET);
        break;
    case RESP_LIST:
        client_print(cli, "\n%s*** Online: %s ***%s\n> ",
                     C_CYAN,
                     resp->user_list[0] ? resp->user_list : "(none)",
                     C_RESET);
        break;
    case RESP_HISTORY:
        client_print(cli, "\n%s--- Room history ---%s\n", C_CYAN, C_RESET);
        print_history_payload(cli, resp->history_payload);
        break;
    case RESP_KICKED:
        client_print(cli, "\n%s*** KICKED: %s ***%s\n",
                     C_RED, resp->message, C_RESET);
        client_set_running(cli, 0);
        break;
    case RESP_OK:
        break;
    case RESP_ERR:
        client_print(cli, "\n%s[error] %s%s\n> ", C_RED, resp->message, C_RESET);
        break;
    default:
        client_print(cli, "\n[server] (unknown response)\n> ");
        break;
    }
}

static void *receiver_thread(void *arg)
{
    chat_client_t *cli = arg;
    char line[MAX_LINE_LEN];
    parsed_resp_t resp;

    while (client_is_running(cli)) {
        if (client_recv_line(cli, line, sizeof(line)) != CHAT_OK) {
            client_print(cli, "\n%s*** Server disconnected ***%s\n", C_RED, C_RESET);
            client_set_running(cli, 0);
            break;
        }

        if (protocol_parse_server_line(line, &resp) != CHAT_OK) {
            client_print(cli, "\n[server] %s\n> ", line);
            continue;
        }
        display_server_message(cli, &resp);
    }
    return NULL;
}

static int resolve_credentials(chat_client_t *cli,
                               const char *user_override,
                               const char *pass_override)
{
    if (user_override != NULL && user_override[0] != '\0') {
        strncpy(cli->username, user_override, MAX_USERNAME_LEN - 1);
        cli->username[MAX_USERNAME_LEN - 1] = '\0';
    } else {
        printf("Username: ");
        fflush(stdout);
        if (fgets(cli->username, sizeof(cli->username), stdin) == NULL) {
            return CHAT_ERR;
        }
        trim_newline(cli->username);
    }

    if (pass_override != NULL && pass_override[0] != '\0') {
        strncpy(cli->password, pass_override, MAX_PASSWORD_LEN - 1);
        cli->password[MAX_PASSWORD_LEN - 1] = '\0';
    } else {
        printf("Password: ");
        fflush(stdout);
        if (fgets(cli->password, sizeof(cli->password), stdin) == NULL) {
            return CHAT_ERR;
        }
        trim_newline(cli->password);
    }

    if (cli->username[0] == '\0' || cli->password[0] == '\0') {
        fprintf(stderr, "Username and password required\n");
        return CHAT_ERR;
    }
    return CHAT_OK;
}

static int parse_at_pm(const char *input, char *target, size_t tlen,
                       char *msg, size_t mlen)
{
    if (input[0] != '@') {
        return CHAT_ERR;
    }
    const char *sp = strchr(input + 1, ' ');
    if (sp == NULL) {
        return CHAT_ERR;
    }
    size_t ulen = (size_t)(sp - (input + 1));
    if (ulen == 0 || ulen >= tlen) {
        return CHAT_ERR;
    }
    memcpy(target, input + 1, ulen);
    target[ulen] = '\0';
    const char *body = sp + 1;
    if (body[0] == '\0' || strlen(body) >= mlen) {
        return CHAT_ERR;
    }
    strncpy(msg, body, mlen - 1);
    msg[mlen - 1] = '\0';
    return CHAT_OK;
}

int client_init(chat_client_t *cli)
{
    if (cli == NULL) {
        return CHAT_ERR;
    }
    memset(cli, 0, sizeof(*cli));
    cli->sockfd = -1;
    cli->running = 1;
    colors_init();
    if (pthread_mutex_init(&cli->lock, NULL) != 0) {
        return CHAT_ERR;
    }
    if (pthread_mutex_init(&cli->io_lock, NULL) != 0) {
        pthread_mutex_destroy(&cli->lock);
        return CHAT_ERR;
    }
    return CHAT_OK;
}

int client_connect(chat_client_t *cli, const char *host, uint16_t port)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *rp;
    char port_str[8];
    int fd = -1;
    int gai_err;

    if (cli == NULL || host == NULL) {
        return CHAT_ERR;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(port_str, sizeof(port_str), "%u", (unsigned)port);

    gai_err = getaddrinfo(host, port_str, &hints, &res);
    if (gai_err != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(gai_err));
        return CHAT_ERR;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);

    if (fd < 0) {
        fprintf(stderr, "Could not connect to %s:%u\n", host, (unsigned)port);
        return CHAT_ERR;
    }

    pthread_mutex_lock(&cli->lock);
    cli->sockfd = fd;
    pthread_mutex_unlock(&cli->lock);
    return CHAT_OK;
}

int client_run(chat_client_t *cli, const char *username_override,
               const char *password_override)
{
    char input[MAX_LINE_LEN];
    char out[MAX_LINE_LEN];

    if (cli == NULL || cli->sockfd < 0) {
        return CHAT_ERR;
    }

    if (resolve_credentials(cli, username_override, password_override) != CHAT_OK) {
        return CHAT_ERR;
    }

    if (protocol_build_client_auth(out, sizeof(out), cli->username, cli->password) != CHAT_OK ||
        client_send_line(cli, out) != CHAT_OK) {
        return CHAT_ERR_IO;
    }

    if (pthread_create(&cli->receiver_tid, NULL, receiver_thread, cli) != 0) {
        return CHAT_ERR;
    }
    cli->receiver_started = 1;

    client_print(cli, "%sConnected. Commands: message, @user PM, /users, /join room, "
                 "/history, /kick user (admin), /quit%s\n> ", C_BOLD, C_RESET);

    while (client_is_running(cli)) {
        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }
        trim_newline(input);

        if (strcmp(input, "/quit") == 0 || strcmp(input, "/exit") == 0) {
            protocol_build_client_quit(out, sizeof(out));
            client_send_line(cli, out);
            client_set_running(cli, 0);
            break;
        }

        if (strcmp(input, "/users") == 0 || strcmp(input, "/list") == 0) {
            protocol_build_client_list(out, sizeof(out));
            client_send_line(cli, out);
            continue;
        }

        if (strncmp(input, "/join ", 6) == 0) {
            protocol_build_client_join_room(out, sizeof(out), input + 6);
            client_send_line(cli, out);
            continue;
        }

        if (strcmp(input, "/history") == 0) {
            protocol_build_client_history(out, sizeof(out));
            client_send_line(cli, out);
            continue;
        }

        if (strncmp(input, "/kick ", 6) == 0) {
            protocol_build_client_kick(out, sizeof(out), input + 6);
            client_send_line(cli, out);
            continue;
        }

        if (input[0] == '\0') {
            client_print(cli, "> ");
            continue;
        }

        if (input[0] == '@') {
            char target[MAX_USERNAME_LEN];
            char msg[MAX_MSG_LEN];
            if (parse_at_pm(input, target, sizeof(target), msg, sizeof(msg)) == CHAT_OK) {
                protocol_build_client_pm(out, sizeof(out), target, msg);
                client_send_line(cli, out);
                continue;
            }
            client_print(cli, "%sUsage: @username message%s\n> ", C_RED, C_RESET);
            continue;
        }

        if (protocol_build_client_msg(out, sizeof(out), input) != CHAT_OK) {
            continue;
        }
        if (client_send_line(cli, out) != CHAT_OK) {
            client_set_running(cli, 0);
            break;
        }
        client_print(cli, "> ");
    }

    pthread_mutex_lock(&cli->lock);
    if (cli->sockfd >= 0) {
        shutdown(cli->sockfd, SHUT_RDWR);
    }
    pthread_mutex_unlock(&cli->lock);

    if (cli->receiver_started) {
        pthread_join(cli->receiver_tid, NULL);
        cli->receiver_started = 0;
    }
    return CHAT_OK;
}

void client_disconnect(chat_client_t *cli)
{
    if (cli == NULL) {
        return;
    }
    client_set_running(cli, 0);
    pthread_mutex_lock(&cli->lock);
    if (cli->sockfd >= 0) {
        close(cli->sockfd);
        cli->sockfd = -1;
    }
    pthread_mutex_unlock(&cli->lock);
}

void client_destroy(chat_client_t *cli)
{
    if (cli == NULL) {
        return;
    }
    client_disconnect(cli);
    pthread_mutex_destroy(&cli->io_lock);
    pthread_mutex_destroy(&cli->lock);
}
