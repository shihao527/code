#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#define SERVER_IP "192.168.1.166" // Replace with your server IP
#define SERVER_PORT 53031         // Server port
#define BUFFER_SIZE 1024          // Buffer size for send/receive
#define NUM_CONNECTIONS 500       // Number of connections to create
#define BASE_PORT 32767           // Start of ephemeral port range

// Client connection function
int sockets[3];
int handle_connection(int source_port)
{
    int sock_fd;
    struct sockaddr_in server_addr, client_addr;
    char buffer[BUFFER_SIZE];
    const char *message = "Hello, Server!\n";
    int bytes_sent, bytes_received;
    printf("create socket");
    // Create socket
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0)
    {
        perror("Socket creation failed");
        return -1;
    }
    int opt = 1;
    setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    printf("bind");
    // Bind to specific source port
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(source_port);
    client_addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock_fd, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0)
    {
        perror("Bind failed");
        close(sock_fd);
        return -1;
    }

    // Configure server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0)
    {
        perror("Invalid server address");
        close(sock_fd);
        return -1;
    }

    printf("start connection");
    // Connect to server
    if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Connection failed");
        close(sock_fd);
        return -1;
    }

    printf("Connected to server %s:%d from port %d\n", SERVER_IP, SERVER_PORT, source_port);

    // Send message to server
    bytes_sent = send(sock_fd, message, strlen(message), 0);
    if (bytes_sent < 0)
    {
        perror("Send failed");
        close(sock_fd);
        return -1;
    }
    printf("Sent to server from port %d: %s", source_port, message);

    // Receive response from server
    bytes_received = recv(sock_fd, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_received < 0)
    {
        perror("Receive failed");
    }
    else if (bytes_received == 0)
    {
        printf("Server closed connection for port %d\n", source_port);
    }
    else
    {
        buffer[bytes_received] = '\0';
        printf("Received from server on port %d: %s", source_port, buffer);
    }

    return sock_fd;
}

int main()
{
    // Create multiple connections with different source ports
    printf("start\n");
    for (int i = 0; i < 3; i++)
    {
        printf("yes\n");
        int source_port = BASE_PORT + i;
        sockets[i] = handle_connection(source_port);
        printf("number %d", i);
    }
    char buffer[100];
    char *mesage = "ping !";
    close(sockets[0]);
    close(sockets[2]);
    for (int j = 0; j < 150; j++)
    {

        if (send(sockets[1], mesage, strlen(mesage), 0) < 0)
        {

            perror("send failed");
        }
        int rev = recv(sockets[1], buffer, 99, 0);
        if (rev <= 0)
        {
            printf("receive failed");
        }

        if (rev > 0)
        {
            buffer[rev] = '\0';
            printf("Received: %s\n", buffer);
        }
        else
        {
            perror("Receive failed");
        }
        sleep(1);
    }
    printf("All connections completed\n");
    while (1)
    {
        /* code */
    }

    return 0;
}