#include <time.h>
#include <stdint.h>

#define TABLE_SIZE 1024
#define IP_MAX_LEN 16

struct conn_entry {
    int client_fd;
    char ip[IP_MAX_LEN];
    uint16_t port;
    int request_count;
    time_t last_request;
    struct conn_entry *next;
};

struct conn_table {
    struct conn_entry **table;
    size_t size;
};

uint32_t murmur3_hash(const char *key, size_t len, uint32_t seed);
struct conn_table *ct_new(size_t size);
void ct_add(struct conn_table *ct, int fd, const char *ip, uint16_t port);
struct conn_entry *ct_get(struct conn_table *ct, const char *ip, uint16_t port);
void ct_remove(struct conn_table *ct, const char *ip, uint16_t port);
