#include "auth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int expect_verify(auth_db_t *db, const char *user, const char *pass)
{
    int rc = auth_verify(db, user, pass);
    if (rc != CHAT_OK) {
        fprintf(stderr, "FAIL: auth_verify(%s, %s) returned %d\n", user, pass, rc);
        return 1;
    }
    printf("PASS: %s authenticated\n", user);
    return 0;
}

static int expect_hash(const char *password, const char *expected_hex)
{
    char computed[SHA256_HEX_LEN];

    if (auth_hash_password(password, computed, sizeof(computed)) != CHAT_OK) {
        fprintf(stderr, "FAIL: auth_hash_password(%s)\n", password);
        return 1;
    }
    if (strcmp(computed, expected_hex) != 0) {
        fprintf(stderr, "FAIL: hash mismatch for '%s'\n", password);
        fprintf(stderr, "  expected: %s\n", expected_hex);
        fprintf(stderr, "  computed: %s\n", computed);
        return 1;
    }
    printf("PASS: hash for '%s' matches\n", password);
    return 0;
}

int main(void)
{
    auth_db_t db;
    int failures = 0;
    const char *db_path = "data/users.db";

    failures += expect_hash("admin123",
        "240be518fabd2724ddb6f04eeb1da5967448d7e831c08c8fa822809f74c720a9");
    failures += expect_hash("pass",
        "d74ff0ee8da3b9806b18c877dbf29bbde50b5bd8e4dad7a3a725000feb82e8f1");

    if (auth_init(&db, db_path) != CHAT_OK) {
        fprintf(stderr, "FAIL: auth_init(%s)\n", db_path);
        return 1;
    }

    failures += expect_verify(&db, "alice", "pass");
    failures += expect_verify(&db, "admin", "admin123");

    if (!auth_is_admin(&db, "admin")) {
        fprintf(stderr, "FAIL: admin should be admin\n");
        failures++;
    } else {
        printf("PASS: admin is admin\n");
    }

    if (auth_is_admin(&db, "alice")) {
        fprintf(stderr, "FAIL: alice should not be admin\n");
        failures++;
    } else {
        printf("PASS: alice is not admin\n");
    }

    if (auth_verify(&db, "alice", "wrong") == CHAT_OK) {
        fprintf(stderr, "FAIL: wrong password should fail\n");
        failures++;
    } else {
        printf("PASS: wrong password rejected\n");
    }

    auth_destroy(&db);

    if (failures != 0) {
        fprintf(stderr, "%d auth test(s) failed\n", failures);
        return 1;
    }

    printf("All auth tests passed.\n");
    return 0;
}
