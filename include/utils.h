#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>
#include <stdint.h>

void trim_newline(char *s);
int set_socket_nonblocking(int fd, int enable);
int send_all(int fd, const char *buf, size_t len);
int recv_line(int fd, char *buf, size_t buflen);
void format_timestamp(time_t t, char *buf, size_t buflen);
int parse_port(const char *s, uint16_t *port_out);

#endif /* UTILS_H */
