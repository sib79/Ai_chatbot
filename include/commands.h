#ifndef COMMANDS_H
#define COMMANDS_H

#include "server.h"

int cmd_send_response(int fd, const char *line);

int cmd_handle_auth(chat_server_t *srv, int fd,
                    const char *username, const char *password);
int cmd_handle_join_room(chat_server_t *srv, int fd, const char *room);
int cmd_handle_msg(chat_server_t *srv, int fd, const char *content);
int cmd_handle_pm(chat_server_t *srv, int fd,
                  const char *target, const char *content);
int cmd_handle_list(chat_server_t *srv, int fd);
int cmd_handle_history(chat_server_t *srv, int fd);
int cmd_handle_kick(chat_server_t *srv, int fd, const char *target);
void cmd_disconnect_client(chat_server_t *srv, int fd,
                           const char *username, int authenticated);

#endif /* COMMANDS_H */
