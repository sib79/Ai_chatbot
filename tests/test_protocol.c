#include "protocol.h"

#include <stdio.h>
#include <string.h>

static int assert_eq_int(int expected, int actual, const char *label)
{
    if (expected != actual) {
        fprintf(stderr, "FAIL %s: expected %d, got %d\n", label, expected, actual);
        return 1;
    }
    printf("PASS %s\n", label);
    return 0;
}

static int assert_str_eq(const char *expected, const char *actual, const char *label)
{
    if (strcmp(expected, actual) != 0) {
        fprintf(stderr, "FAIL %s: expected '%s', got '%s'\n", label, expected, actual);
        return 1;
    }
    printf("PASS %s\n", label);
    return 0;
}

int main(void)
{
    int failures = 0;
    parsed_cmd_t cmd;
    parsed_resp_t resp;
    char buf[MAX_LINE_LEN];

    failures += assert_eq_int(CHAT_OK,
                              protocol_parse_client_line("AUTH alice pass", &cmd),
                              "parse AUTH");
    failures += assert_eq_int(CMD_AUTH, cmd.cmd, "AUTH cmd type");
    failures += assert_str_eq("alice", cmd.arg1, "AUTH username");
    failures += assert_str_eq("pass", cmd.arg2, "AUTH password");

    failures += assert_eq_int(CHAT_OK,
                              protocol_parse_client_line("JOIN_ROOM room1", &cmd),
                              "parse JOIN_ROOM");
    failures += assert_str_eq("room1", cmd.arg1, "JOIN_ROOM name");

    failures += assert_eq_int(CHAT_OK,
                              protocol_parse_client_line("PM bob hello there", &cmd),
                              "parse PM");
    failures += assert_str_eq("bob", cmd.arg1, "PM target");
    failures += assert_str_eq("hello there", cmd.arg2, "PM body");

    failures += assert_eq_int(CHAT_OK,
                              protocol_parse_client_line("MSG Hello world", &cmd),
                              "parse MSG");

    protocol_build_server_broadcast(buf, sizeof(buf), "alice", "2026-06-16T10:00:00",
                                    "lobby", "hi all");
    failures += assert_eq_int(CHAT_OK, protocol_parse_server_line(buf, &resp),
                              "roundtrip BROADCAST");
    failures += assert_eq_int(RESP_BROADCAST, resp.type, "BROADCAST type");
    failures += assert_str_eq("lobby", resp.room, "BROADCAST room");
    failures += assert_str_eq("hi all", resp.message, "BROADCAST msg");

    protocol_build_server_pm(buf, sizeof(buf), "bob", "2026-06-16T10:01:00", "secret");
    failures += assert_eq_int(CHAT_OK, protocol_parse_server_line(buf, &resp), "parse PM resp");
    failures += assert_str_eq("bob", resp.from_user, "PM from");
    failures += assert_str_eq("secret", resp.message, "PM body");

    protocol_build_server_user_join(buf, sizeof(buf), "alice", "lobby");
    failures += assert_eq_int(CHAT_OK, protocol_parse_server_line(buf, &resp), "USER_JOIN");
    failures += assert_str_eq("lobby", resp.room, "USER_JOIN room");

    if (failures == 0) {
        printf("\nAll protocol tests passed.\n");
        return 0;
    }

    fprintf(stderr, "\n%d protocol test(s) failed.\n", failures);
    return 1;
}
