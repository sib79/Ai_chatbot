#include "utils.h"

#include "common.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

void trim_newline(char *s)
{
    if (s == NULL) {
        return;
    }
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[len - 1] = '\0';
        len--;
    }
}

int set_socket_nonblocking(int fd, int enable)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return CHAT_ERR_IO;
    }
    if (enable) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    if (fcntl(fd, F_SETFL, flags) < 0) {
        return CHAT_ERR_IO;
    }
    return CHAT_OK;
}

int send_all(int fd, const char *buf, size_t len)
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

int recv_line(int fd, char *buf, size_t buflen)
{
    size_t pos = 0;

    if (buf == NULL || buflen < 2) {
        return CHAT_ERR;
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
            if (pos == 0) {
                return CHAT_ERR_IO;
            }
            break;
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

void format_timestamp(time_t t, char *buf, size_t buflen)
{
    struct tm tm_info;

    if (buf == NULL || buflen == 0) {
        return;
    }

    if (localtime_r(&t, &tm_info) == NULL) {
        snprintf(buf, buflen, "unknown");
        return;
    }

    strftime(buf, buflen, "%Y-%m-%dT%H:%M:%S", &tm_info);
}

int parse_port(const char *s, uint16_t *port_out)
{
    char *end = NULL;
    long val;

    if (s == NULL || port_out == NULL) {
        return CHAT_ERR;
    }

    errno = 0;
    val = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        return CHAT_ERR;
    }
    if (val <= 0 || val > 65535) {
        return CHAT_ERR;
    }

    *port_out = (uint16_t)val;
    return CHAT_OK;
}
