#include "commands.h"

#include "protocol.h"
#include "utils.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

int cmd_send_response(int fd, const char *line)
{
    return send_all(fd, line, strlen(line));
}

static int enqueue_room_event(chat_server_t *srv, mq_event_type_t type,
                              const char *username, const char *room)
{
    mq_message_t evt;

    evt.type = type;
    strncpy(evt.username, username, MAX_USERNAME_LEN - 1);
    strncpy(evt.room, room, MAX_ROOM_NAME_LEN - 1);
    evt.content[0] = '\0';
    evt.timestamp = time(NULL);
    return mq_push(&srv->broadcast_queue, &evt);
}

int cmd_handle_auth(chat_server_t *srv, int fd,
                    const char *username, const char *password)
{
    char line[MAX_LINE_LEN];
    bool admin;

    if (username[0] == '\0' || password[0] == '\0') {
        protocol_build_server_err(line, sizeof(line), "username and password required");
        cmd_send_response(fd, line);
        return CHAT_ERR_AUTH;
    }

    if (auth_verify(&srv->auth, username, password) != CHAT_OK) {
        protocol_build_server_err(line, sizeof(line), "invalid credentials");
        cmd_send_response(fd, line);
        return CHAT_ERR_AUTH;
    }

    if (registry_username_exists(&srv->registry, username)) {
        protocol_build_server_err(line, sizeof(line), "user already online");
        cmd_send_response(fd, line);
        return CHAT_ERR_AUTH;
    }

    admin = auth_is_admin(&srv->auth, username);

    switch (registry_add(&srv->registry, fd, username, DEFAULT_ROOM, admin)) {
    case CHAT_OK:
        break;
    default:
        protocol_build_server_err(line, sizeof(line), "server full");
        cmd_send_response(fd, line);
        return CHAT_ERR_AUTH;
    }

    protocol_build_server_welcome(line, sizeof(line), username);
    if (cmd_send_response(fd, line) != CHAT_OK) {
        registry_remove(&srv->registry, fd);
        return CHAT_ERR_IO;
    }

    protocol_build_server_room_joined(line, sizeof(line), DEFAULT_ROOM);
    cmd_send_response(fd, line);

    room_history_send_to_client(&srv->rooms, DEFAULT_ROOM, fd, cmd_send_response);

    protocol_build_server_ok(line, sizeof(line), "authenticated");
    cmd_send_response(fd, line);

    enqueue_room_event(srv, MQ_JOIN, username, DEFAULT_ROOM);

    printf("[auth] '%s'%s joined lobby (fd=%d)\n",
           username, admin ? " (admin)" : "", fd);
    return CHAT_OK;
}

int cmd_handle_join_room(chat_server_t *srv, int fd, const char *room)
{
    char username[MAX_USERNAME_LEN];
    char old_room[MAX_ROOM_NAME_LEN];
    char line[MAX_LINE_LEN];

    if (room[0] == '\0') {
        protocol_build_server_err(line, sizeof(line), "room name required");
        return cmd_send_response(fd, line);
    }

    if (registry_get_username(&srv->registry, fd, username, sizeof(username)) != CHAT_OK) {
        protocol_build_server_err(line, sizeof(line), "authenticate first");
        return cmd_send_response(fd, line);
    }

    if (registry_get_room(&srv->registry, fd, old_room, sizeof(old_room)) != CHAT_OK) {
        return CHAT_ERR;
    }

    if (strcmp(old_room, room) == 0) {
        protocol_build_server_err(line, sizeof(line), "already in that room");
        return cmd_send_response(fd, line);
    }

    enqueue_room_event(srv, MQ_LEAVE, username, old_room);
    registry_set_room(&srv->registry, fd, room);

    protocol_build_server_room_joined(line, sizeof(line), room);
    cmd_send_response(fd, line);

    room_history_send_to_client(&srv->rooms, room, fd, cmd_send_response);

    enqueue_room_event(srv, MQ_JOIN, username, room);

    protocol_build_server_ok(line, sizeof(line), "room changed");
    return cmd_send_response(fd, line);
}

int cmd_handle_msg(chat_server_t *srv, int fd, const char *content)
{
    char username[MAX_USERNAME_LEN];
    char room[MAX_ROOM_NAME_LEN];
    char line[MAX_LINE_LEN];
    char ts[32];
    mq_message_t evt;
    time_t now;

    if (registry_get_username(&srv->registry, fd, username, sizeof(username)) != CHAT_OK) {
        protocol_build_server_err(line, sizeof(line), "authenticate first");
        return cmd_send_response(fd, line);
    }

    if (registry_get_room(&srv->registry, fd, room, sizeof(room)) != CHAT_OK) {
        return CHAT_ERR;
    }

    if (content[0] == '\0') {
        protocol_build_server_err(line, sizeof(line), "empty message");
        return cmd_send_response(fd, line);
    }

    now = time(NULL);
    room_history_add(&srv->rooms, room, username, content, now);

    evt.type = MQ_CHAT;
    strncpy(evt.username, username, MAX_USERNAME_LEN - 1);
    strncpy(evt.room, room, MAX_ROOM_NAME_LEN - 1);
    strncpy(evt.content, content, MAX_MSG_LEN - 1);
    evt.timestamp = now;

    if (mq_push(&srv->broadcast_queue, &evt) != CHAT_OK) {
        protocol_build_server_err(line, sizeof(line), "broadcast queue error");
        return cmd_send_response(fd, line);
    }

    format_timestamp(now, ts, sizeof(ts));
    protocol_build_server_ok(line, sizeof(line), "message sent");
    return cmd_send_response(fd, line);
}

int cmd_handle_pm(chat_server_t *srv, int fd,
                  const char *target, const char *content)
{
    char from[MAX_USERNAME_LEN];
    char line[MAX_LINE_LEN];
    char ts[32];
    int target_fd;
    time_t now;

    if (registry_get_username(&srv->registry, fd, from, sizeof(from)) != CHAT_OK) {
        protocol_build_server_err(line, sizeof(line), "authenticate first");
        return cmd_send_response(fd, line);
    }

    if (target[0] == '\0' || content[0] == '\0') {
        protocol_build_server_err(line, sizeof(line), "usage: PM user message");
        return cmd_send_response(fd, line);
    }

    if (strcmp(from, target) == 0) {
        protocol_build_server_err(line, sizeof(line), "cannot PM yourself");
        return cmd_send_response(fd, line);
    }

    target_fd = registry_get_sockfd_by_username(&srv->registry, target);
    if (target_fd < 0) {
        protocol_build_server_err(line, sizeof(line), "user not online");
        return cmd_send_response(fd, line);
    }

    now = time(NULL);
    format_timestamp(now, ts, sizeof(ts));
    protocol_build_server_pm(line, sizeof(line), from, ts, content);
    if (cmd_send_response(target_fd, line) != CHAT_OK) {
        protocol_build_server_err(line, sizeof(line), "failed to deliver PM");
        return cmd_send_response(fd, line);
    }

    protocol_build_server_ok(line, sizeof(line), "PM delivered");
    return cmd_send_response(fd, line);
}

int cmd_handle_list(chat_server_t *srv, int fd)
{
    char users[MAX_MSG_LEN];
    char line[MAX_LINE_LEN];
    char ignored[MAX_USERNAME_LEN];

    if (registry_get_username(&srv->registry, fd, ignored, sizeof(ignored)) != CHAT_OK) {
        protocol_build_server_err(line, sizeof(line), "authenticate first");
        return cmd_send_response(fd, line);
    }

    if (registry_build_user_list(&srv->registry, users, sizeof(users)) != CHAT_OK) {
        protocol_build_server_err(line, sizeof(line), "failed to build user list");
        return cmd_send_response(fd, line);
    }

    protocol_build_server_list(line, sizeof(line), users);
    return cmd_send_response(fd, line);
}

int cmd_handle_history(chat_server_t *srv, int fd)
{
    char room[MAX_ROOM_NAME_LEN];
    char ignored[MAX_USERNAME_LEN];
    char line[MAX_LINE_LEN];

    if (registry_get_username(&srv->registry, fd, ignored, sizeof(ignored)) != CHAT_OK) {
        protocol_build_server_err(line, sizeof(line), "authenticate first");
        return cmd_send_response(fd, line);
    }

    if (registry_get_room(&srv->registry, fd, room, sizeof(room)) != CHAT_OK) {
        return CHAT_ERR;
    }

    if (room_history_send_to_client(&srv->rooms, room, fd, cmd_send_response) != CHAT_OK) {
        protocol_build_server_err(line, sizeof(line), "history unavailable");
        return cmd_send_response(fd, line);
    }

    protocol_build_server_ok(line, sizeof(line), "history sent");
    return cmd_send_response(fd, line);
}

int cmd_handle_kick(chat_server_t *srv, int fd, const char *target)
{
    char line[MAX_LINE_LEN];
    char admin[MAX_USERNAME_LEN];
    int target_fd;

    if (!registry_is_admin(&srv->registry, fd)) {
        protocol_build_server_err(line, sizeof(line), "admin privileges required");
        return cmd_send_response(fd, line);
    }

    if (target[0] == '\0') {
        protocol_build_server_err(line, sizeof(line), "username required");
        return cmd_send_response(fd, line);
    }

    if (registry_get_username(&srv->registry, fd, admin, sizeof(admin)) == CHAT_OK &&
        strcmp(admin, target) == 0) {
        protocol_build_server_err(line, sizeof(line), "cannot kick yourself");
        return cmd_send_response(fd, line);
    }

    target_fd = registry_get_sockfd_by_username(&srv->registry, target);
    if (target_fd < 0) {
        protocol_build_server_err(line, sizeof(line), "user not online");
        return cmd_send_response(fd, line);
    }

    protocol_build_server_kicked(line, sizeof(line), "kicked by admin");
    cmd_send_response(target_fd, line);
    shutdown(target_fd, SHUT_RDWR);

    protocol_build_server_ok(line, sizeof(line), "user kicked");
    return cmd_send_response(fd, line);
}

void cmd_disconnect_client(chat_server_t *srv, int fd,
                           const char *username, int authenticated)
{
    char room[MAX_ROOM_NAME_LEN];

    if (authenticated && username[0] != '\0') {
        if (registry_get_room(&srv->registry, fd, room, sizeof(room)) == CHAT_OK) {
            enqueue_room_event(srv, MQ_LEAVE, username, room);
        }
        registry_remove(&srv->registry, fd);
        printf("[leave] '%s' disconnected (fd=%d, online=%zu)\n",
               username, fd, registry_count(&srv->registry));
    }

    close(fd);
}
