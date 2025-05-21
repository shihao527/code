#include "uthash.h"

struct fd_entry{

    int fd;
    UT_hash_handle hh;
};

struct fd_entry *queued_fds =NULL;

void add_ququed_fd(int fd){
    struct fd_entry *entry = malloc(sizeof(struct fd_entry));
    entry->fd =fd;
    HASH_ADD_INT(queued_fds,fd,entry);
}
int is_fd_queued(int fd){

    struct fd_entry *entry;
    HASH_FIND_INT(queued_fds, &fd, entry);
    return entry !=NULL;
}

void remove_queued_fd(int fd){

    struct fd_entry *entry;
    HASH_FIND_INT(queued_fds,&fd,entry);
    if(entry){
        HASH_DEL(queued_fds,entry);
        free(entry);
    }
}