#include "client_registry.h"

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

static int assert_eq_size(size_t expected, size_t actual, const char *label)
{
    if (expected != actual) {
        fprintf(stderr, "FAIL %s: expected %zu, got %zu\n", label, expected, actual);
        return 1;
    }
    printf("PASS %s\n", label);
    return 0;
}

static int assert_true(int cond, const char *label)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", label);
        return 1;
    }
    printf("PASS %s\n", label);
    return 0;
}

int main(void)
{
    client_registry_t reg;
    char list[MAX_MSG_LEN];
    int failures = 0;

    failures += assert_eq_int(CHAT_OK, registry_init(&reg), "registry_init");

    failures += assert_eq_int(CHAT_OK,
                              registry_add(&reg, 10, "alice", DEFAULT_ROOM, false),
                              "add alice");
    failures += assert_eq_int(CHAT_OK,
                              registry_add(&reg, 11, "bob", "room1", false),
                              "add bob room1");
    failures += assert_eq_size(2, registry_count(&reg), "count two users");

    failures += assert_true(registry_username_exists(&reg, "alice"), "alice online");

    failures += assert_eq_int(CHAT_OK, registry_build_user_list(&reg, list, sizeof(list)),
                              "build user list");
    failures += assert_true(strstr(list, "alice") != NULL && strstr(list, "bob") != NULL,
                            "list has users");

    failures += assert_eq_int(CHAT_OK,
                              registry_build_room_user_list(&reg, DEFAULT_ROOM, list, sizeof(list)),
                              "room list lobby");
    failures += assert_true(strstr(list, "alice") != NULL && strstr(list, "bob") == NULL,
                            "lobby has alice only");

    failures += assert_eq_int(CHAT_ERR_DUPLICATE,
                              registry_add(&reg, 12, "alice", DEFAULT_ROOM, false),
                              "reject duplicate");

    failures += assert_eq_int(CHAT_OK, registry_remove(&reg, 10), "remove alice");
    failures += assert_eq_size(1, registry_count(&reg), "count after remove");

    registry_destroy(&reg);

    if (failures == 0) {
        printf("\nAll registry tests passed.\n");
        return 0;
    }

    fprintf(stderr, "\n%d registry test(s) failed.\n", failures);
    return 1;
}
