#ifndef AUTH_H
#define AUTH_H

#include <pthread.h>
#include <stdbool.h>

#include "common.h"

typedef struct user_record {
    char username[MAX_USERNAME_LEN];
    char password_hash[SHA256_HEX_LEN];
    bool is_admin;
    struct user_record *next;
} user_record_t;

typedef struct {
    user_record_t *head;
    pthread_mutex_t lock;
    char db_path[256];
} auth_db_t;

int auth_init(auth_db_t *db, const char *path);
void auth_destroy(auth_db_t *db);

int auth_hash_password(const char *password, char *out_hex, size_t out_len);
int auth_verify(auth_db_t *db, const char *username, const char *password);
bool auth_is_admin(auth_db_t *db, const char *username);

#endif /* AUTH_H */
