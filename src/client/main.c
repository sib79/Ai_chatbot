#include "client.h"

#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [host] [port] [username] [password]\n", prog);
    fprintf(stderr, "  host      Server IP/hostname (default: 127.0.0.1)\n");
    fprintf(stderr, "  port      TCP port (default: %d)\n", DEFAULT_PORT);
    fprintf(stderr, "  username  Optional — prompt if omitted\n");
    fprintf(stderr, "  password  Optional — prompt if omitted\n");
}

int main(int argc, char *argv[])
{
    chat_client_t cli;
    const char *host = "127.0.0.1";
    const char *username = NULL;
    const char *password = NULL;
    uint16_t port = DEFAULT_PORT;

    if (argc > 5) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (argc >= 2) {
        host = argv[1];
    }
    if (argc >= 3) {
        if (parse_port(argv[2], &port) != CHAT_OK) {
            fprintf(stderr, "Invalid port: %s\n", argv[2]);
            return EXIT_FAILURE;
        }
    }
    if (argc >= 4) {
        username = argv[3];
    }
    if (argc == 5) {
        password = argv[4];
    }

    if (client_init(&cli) != CHAT_OK) {
        return EXIT_FAILURE;
    }

    if (client_connect(&cli, host, port) != CHAT_OK) {
        client_destroy(&cli);
        return EXIT_FAILURE;
    }

    if (client_run(&cli, username, password) != CHAT_OK) {
        client_destroy(&cli);
        return EXIT_FAILURE;
    }

    client_destroy(&cli);
    printf("Disconnected.\n");
    return EXIT_SUCCESS;
}
