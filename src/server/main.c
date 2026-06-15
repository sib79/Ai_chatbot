#include "server.h"

#include "utils.h"

#include <stdio.h>
#include <stdlib.h>

static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [port] [users.db]\n", prog);
    fprintf(stderr, "  port      TCP port (default: %d)\n", DEFAULT_PORT);
    fprintf(stderr, "  users.db  User database path (default: %s)\n", DEFAULT_USERS_DB);
}

int main(int argc, char *argv[])
{
    chat_server_t srv;
    uint16_t port = DEFAULT_PORT;
    const char *users_db = DEFAULT_USERS_DB;

    if (argc > 3) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (argc >= 2) {
        if (parse_port(argv[1], &port) != CHAT_OK) {
            fprintf(stderr, "Invalid port: %s\n", argv[1]);
            return EXIT_FAILURE;
        }
    }
    if (argc == 3) {
        users_db = argv[2];
    }

    if (server_init(&srv, port, users_db) != CHAT_OK) {
        fprintf(stderr, "Failed to initialize server (check users db: %s)\n", users_db);
        return EXIT_FAILURE;
    }

    server_install_signal_handlers(&srv);

    if (server_run(&srv) != CHAT_OK) {
        server_shutdown(&srv);
        server_destroy(&srv);
        return EXIT_FAILURE;
    }

    server_shutdown(&srv);
    server_destroy(&srv);

    printf("Server shut down cleanly\n");
    return EXIT_SUCCESS;
}
