#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hash_utils.h"
#include <stdint.h>

uint32_t murmur3_hash(const char *key, size_t len, uint32_t seed) {
    uint32_t hash = seed;
    for (size_t i = 0; i < len; i++) {
        hash ^= key[i];
        hash *= 0x5bd1e995;
        hash ^= hash >> 24;
    }
    return hash;
}

uint32_t hash_key(const char *ip, uint16_t port) {
    char key[32];
    snprintf(key, sizeof(key), "%s:%u", ip, port);
    return murmur3_hash(key, strlen(key), 0xdeadbeef) % TABLE_SIZE;
}

struct conn_table *ct_new(size_t size) {
    struct conn_table *ct = malloc(sizeof(struct conn_table));
    if (!ct) return NULL;

    ct->table = calloc(size, sizeof(struct conn_entry *));
    if (!ct->table) {
        free(ct);
        return NULL;
    }

    ct->size = size;
    return ct;
}

void ct_add(struct conn_table *ct, int fd, const char *ip, uint16_t port) {
    uint32_t index = hash_key(ip, port);
    struct conn_entry *entry = malloc(sizeof(struct conn_entry));
    if (!entry) return;

    entry->client_fd = fd;
    strncpy(entry->ip, ip, IP_MAX_LEN - 1);
    entry->ip[IP_MAX_LEN - 1] = '\0';
    entry->port = port;
    entry->request_count = 0;
    entry->last_request = time(NULL);
    entry->next = ct->table[index];
    ct->table[index] = entry;
}

struct conn_entry *ct_get(struct conn_table *ct, const char *ip, uint16_t port) {
    uint32_t index = hash_key(ip, port);
    for (struct conn_entry *e = ct->table[index]; e; e = e->next) {
        if (strcmp(e->ip, ip) == 0 && e->port == port) {
            return e;
        }
    }
    return NULL;
}

void ct_remove(struct conn_table *ct, const char *ip, uint16_t port) {
    uint32_t index = hash_key(ip, port);
    struct conn_entry *curr = ct->table[index];
    struct conn_entry *prev = NULL;

    while (curr) {
        if (strcmp(curr->ip, ip) == 0 && curr->port == port) {
            if (prev) {
                prev->next = curr->next;
            } else {
                ct->table[index] = curr->next;
            }
            free(curr);
            return;
        }
        prev = curr;
        curr = curr->next;
    }
}