#include "uthash.h"
#define QUEUED_FD_H
struct fd_entry {
    int fd;
    UT_hash_handle hh;
};
void add_ququed_fd(int fd);
int is_fd_queued(int fd);
void remove_queued_fd(int fd);


