#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#include "hash_utils.h"
#include <stdio.h>
#include <sys/epoll.h>
#include "thread_pool.h"
#include "string.h"
#include <errno.h>
#include "fd_queued.h"
#include <fcntl.h>
const char *message = "-ERR Rate Limit\r\n"
void handle_client(struct conn_table *ct, int client_fd, const char *ip, uint16_t port)
{
    struct conn_entry *entry = ct_get(ct, ip, port);
    if (!entry)
    {
        ct_add(ct, client_fd, ip, port);
        entry = ct_get(ct, ip, port);
    }

    time_t now = time(NULL);
    if (entry->request_count > 50 && difftime(now, entry->last_request) < 60)
    {
        send(client_fd, message, strlen(message), 0);
        close(client_fd);
        ct_remove(ct, ip, port);
        return;
    }

    char buffer[1024];

    while (1)
    {
        int n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (n > 0)
        {
            buffer[n] = '\0';
            entry->request_count++;
            entry->last_request = now;
            if (send(client_fd, "+OK\r\n", strlen("+OK\r\n"), 0) < 0)
            {
   
                break;
            } 
        }
        else if (n == 0)
        {
         
            close(client_fd);
            remove_queued_fd(client_fd);

            break;
        }
        else if (errno == EAGAIN || errno == EWOULDBLOCK)
        {

            break;
        }
        else
        {
 
            close(client_fd);
            remove_queued_fd(client_fd);
        }
    }
    remove_queued_fd(client_fd);
}

int main()
{
    struct conn_table *ct = ct_new(TABLE_SIZE);

    if (!ct)
    {
        fprintf(stderr, "Failed to create connection table\n");
        return 1;
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in server_addr = {.sin_family = AF_INET, .sin_port = htons(53031), .sin_addr.s_addr = INADDR_ANY};

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bind");
        close(server_fd);
        return 1;
    }
    if (listen(server_fd, 10) < 0)
    {
        perror("listen");
        close(server_fd);
        return 1;
    }

    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0)
    {
        perror("epoll_create1");
        close(server_fd);
        return 1;
    }
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) < 0)
    {
        perror("epoll_ctl");
        close(server_fd);
        close(epoll_fd);
        return 1;
    }

    
    struct thread_pool *pool = thread_pool_create(THREAD_POOL_SIZE, TASK_QUEUE_SIZE);

    if (!pool)
    {
        fprintf(stderr, "Failed to create thread pool\n");
        close(server_fd);
        close(epoll_fd);
        return 1;
    }
    while (1)
    {
        struct epoll_event events[10];
        int nfds = epoll_wait(epoll_fd, events, 10, -1);
        if (nfds < 0)
        {
            perror("epoll_wait");
            break;
        }
        for (int i = 0; i < nfds; i++)
        {

            int fd = events[i].data.fd;

            if (fd == server_fd)
            {
                // Accept new client
                int client_fd = accept(server_fd, NULL, NULL);
             
                if (client_fd < 0)
                {
                    perror("accept");
                    continue;
                }
                int flags = fcntl(client_fd, F_GETFL, 0);
                if (fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) < 0)
                {
                    printf("err");
                }
                struct epoll_event ev;
                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = client_fd;
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev);
            }
            else
            {

                if (events[i].events & (EPOLLERR | EPOLLHUP))
                {
             
                    close(fd);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL); // Clean up epoll
                    remove_queued_fd(fd);
                }
                else if (events[i].events & EPOLLIN)
                {

                    char ip[INET_ADDRSTRLEN];
                    struct sockaddr_in client_addr;
                    socklen_t addr_len = sizeof(client_addr);
                    if (!is_fd_queued(fd))
                    {
                        if (getpeername(fd, (struct sockaddr *)&client_addr, &addr_len) == 0)
                        {
                            inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
                            uint16_t port = ntohs(client_addr.sin_port);
                            struct task task = {
                                .ct = ct,
                                .fd = fd,
                                .port = port};
                            strncpy(task.ip, ip, INET_ADDRSTRLEN);
                            task.ip[sizeof(task.ip) - 1] = '\0';

                            thread_pool_add_task(pool, task);
                            add_ququed_fd(task.fd);
                        }
                        else
                        {
                            
                            perror("getpeername");
                            remove_queued_fd(fd);
                        }
                    }
                }
            }
        }
    }
    thread_pool_destroy(pool);
    close(epoll_fd);
    close(server_fd);
    return 0;
}