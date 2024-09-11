#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/time.h>

#define BUFFER_SIZE 1024

int port;
int max_clients;
char directory[1024];

pthread_mutex_t lock;
int client_count = 0;

void send_file(int client_socket, char *filename) 
{
    FILE *file = fopen(filename, "rb");
    if (!file) 
    {
        perror("Error opening file");
        exit(EXIT_FAILURE);
    }

    char buffer[BUFFER_SIZE];
    size_t bytes_read;

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) 
    {   
        if (send(client_socket, buffer, bytes_read, 0) != bytes_read) 
        {
            perror("Error sending file here");
            exit(EXIT_FAILURE);
        }
    }

    if (bytes_read == -1) 
    {
        perror("Error reading file");
        exit(EXIT_FAILURE);
    }
    
    fclose(file);
}

void *handle_request(void *arg)
{
    int client_fd = *((int*)(arg));

    // Receive command from client
    char command[BUFFER_SIZE];
    ssize_t bytes_received = recv(client_fd, command, BUFFER_SIZE, 0);
    if (bytes_received == -1) 
    {
        perror("Error receiving command");
        exit(EXIT_FAILURE);
    }
    command[bytes_received] = '\0';

    char filename[100];
    strcpy(filename,directory);
    strcat(filename,"/");
    switch (atoi(command)) 
    {
        case 1:
            strcat(filename, "Master_Of_Puppets_Solo.mp3");
            printf("Song requested: Master of Puppets\n");
            break;
        case 2:
            strcat(filename, "Let_It_Happen.mp3");
            printf("Song requested: Let it happen\n");
            break;
        default:
            fprintf(stderr, "Invalid song number\n");
            exit(EXIT_FAILURE);
    }

    // Send the requested file to client
    send_file(client_fd, filename);

    // Close client socket
    close(client_fd);

    pthread_mutex_lock(&lock);
    client_count--; 
    pthread_mutex_unlock(&lock);
}

int main(int argc, char *argv[]) 
{
    if (argc != 4) 
    {
        printf("Usage: %s <port> <song_directory> <max_clients>\n", argv[0]);
        return 1;
    }

    port = atoi(argv[1]);
    strcpy(directory,argv[2]);
    max_clients = atoi(argv[3]);

    int server_fd, client_fd;
    struct sockaddr_in server_addr;

    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) 
    {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    // Initialize server address structure
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    // Bind socket to address and port
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) 
    {
        perror("Error binding socket");
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_fd, max_clients) == -1) 
    {
        perror("Error listening for connections");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", port);
    pthread_mutex_init(&lock, NULL);

    while(1)
    {
        struct sockaddr_in client_addr;
        socklen_t client_addr_size = sizeof(client_addr);
        
        // Accept incoming connection
        client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_addr_size);
        
        pthread_mutex_lock(&lock);
        client_count++;
        pthread_mutex_unlock(&lock);

        if(client_count > max_clients){
            close(client_fd);
            pthread_mutex_lock(&lock);
            client_count--;
            pthread_mutex_unlock(&lock);
            continue;
        }

        if (client_fd <= -1) 
        {
            perror("Error accepting connection");
            exit(EXIT_FAILURE);
        }   

        //Print client ip address
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        printf("Client connected: %s\n", client_ip);

        pthread_t serve_thread;
        pthread_create(&serve_thread, NULL, handle_request, (void*)(&client_fd));
    }
    
    // Close server socket
    close(server_fd);

    return 0;
}
