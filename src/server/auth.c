#include "auth.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <openssl/evp.h>

static void strip_line(char *s)
{
    char *nl = strchr(s, '\n');
    if (nl) {
        *nl = '\0';
    }
    nl = strchr(s, '\r');
    if (nl) {
        *nl = '\0';
    }
}

int auth_hash_password(const char *password, char *out_hex, size_t out_len)
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    EVP_MD_CTX *ctx;
    size_t i;

    if (password == NULL || out_hex == NULL || out_len < SHA256_HEX_LEN) {
        return CHAT_ERR;
    }

    ctx = EVP_MD_CTX_new();
    if (ctx == NULL) {
        return CHAT_ERR_NOMEM;
    }

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1 ||
        EVP_DigestUpdate(ctx, password, strlen(password)) != 1 ||
        EVP_DigestFinal_ex(ctx, digest, &digest_len) != 1) {
        EVP_MD_CTX_free(ctx);
        return CHAT_ERR;
    }
    EVP_MD_CTX_free(ctx);

    for (i = 0; i < digest_len; i++) {
        snprintf(out_hex + (i * 2), 3, "%02x", digest[i]);
    }
    out_hex[digest_len * 2] = '\0';
    return CHAT_OK;
}

static void auth_free_records(user_record_t *head)
{
    while (head != NULL) {
        user_record_t *next = head->next;
        free(head);
        head = next;
    }
}

int auth_init(auth_db_t *db, const char *path)
{
    FILE *fp;
    char line[512];
    const char *db_path;

    if (db == NULL) {
        return CHAT_ERR;
    }

    memset(db, 0, sizeof(*db));
    db_path = (path != NULL && path[0] != '\0') ? path : DEFAULT_USERS_DB;
    strncpy(db->db_path, db_path, sizeof(db->db_path) - 1);

    if (pthread_mutex_init(&db->lock, NULL) != 0) {
        return CHAT_ERR;
    }

    fp = fopen(db_path, "r");
    if (fp == NULL) {
        fprintf(stderr, "auth: cannot open %s\n", db_path);
        return CHAT_ERR;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        char username[MAX_USERNAME_LEN];
        char hash[SHA256_HEX_LEN];
        char admin_flag[8];
        user_record_t *rec;

        strip_line(line);
        if (line[0] == '#' || line[0] == '\0') {
            continue;
        }

        if (sscanf(line, "%31[^:]:%64[^:]:%7s", username, hash, admin_flag) != 3) {
            continue;
        }

        rec = malloc(sizeof(*rec));
        if (rec == NULL) {
            fclose(fp);
            auth_destroy(db);
            return CHAT_ERR_NOMEM;
        }

        strncpy(rec->username, username, MAX_USERNAME_LEN - 1);
        strncpy(rec->password_hash, hash, SHA256_HEX_LEN - 1);
        rec->is_admin = (admin_flag[0] == '1' ||
                         strcasecmp(admin_flag, "admin") == 0 ||
                         strcasecmp(admin_flag, "true") == 0 ||
                         strcasecmp(admin_flag, "yes") == 0);
        rec->next = db->head;
        db->head = rec;
    }

    fclose(fp);
    return CHAT_OK;
}

void auth_destroy(auth_db_t *db)
{
    if (db == NULL) {
        return;
    }

    pthread_mutex_lock(&db->lock);
    auth_free_records(db->head);
    db->head = NULL;
    pthread_mutex_unlock(&db->lock);
    pthread_mutex_destroy(&db->lock);
}

int auth_verify(auth_db_t *db, const char *username, const char *password)
{
    char computed[SHA256_HEX_LEN];
    user_record_t *cur;
    int rc = CHAT_ERR_AUTH;

    if (db == NULL || username == NULL || password == NULL) {
        return CHAT_ERR;
    }

    if (auth_hash_password(password, computed, sizeof(computed)) != CHAT_OK) {
        return CHAT_ERR;
    }

    pthread_mutex_lock(&db->lock);
    cur = db->head;
    while (cur != NULL) {
        if (strcmp(cur->username, username) == 0) {
            if (strcmp(cur->password_hash, computed) == 0) {
                rc = CHAT_OK;
            }
            break;
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&db->lock);

    return rc;
}

bool auth_is_admin(auth_db_t *db, const char *username)
{
    user_record_t *cur;
    bool admin = false;

    if (db == NULL || username == NULL) {
        return false;
    }

    pthread_mutex_lock(&db->lock);
    cur = db->head;
    while (cur != NULL) {
        if (strcmp(cur->username, username) == 0) {
            admin = cur->is_admin;
            break;
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&db->lock);

    return admin;
}
