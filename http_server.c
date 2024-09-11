#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/time.h>

#define MAX_CONNECTIONS 10
#define BUFFER_SIZE 1024
#define DEFAULT_PAGE "index.html"
#define NOT_FOUND_PAGE "404.html"

char* root_directory = NULL;
int port = 8000;

int first_occurrence(char* request)
{
    char* found = strstr(request, "%**%");
    return found - request;
}

int last_occurrence(char *request) 
{
    char *last = NULL;
    char *found;

    found = strstr(request, "%**%");
    while (found != NULL) {
        last = found;
        found = strstr(found + 1, "%**%");
    }

    return last - request;
}

char* extract_message(char* request)
{
    int start = first_occurrence(request) + 4;
    int end = last_occurrence(request);
    int length = end - start;
    char* message = malloc(length + 1);
    strncpy(message, request + start, length);
    message[length] = '\0';
    return message;
}

char* get_content_type(char* extension)
{
    char *content_type = "text/html";
    if (extension != NULL) 
    {
        if (strcmp(extension, ".css") == 0)
            content_type = "text/css";
        else if (strcmp(extension, ".js") == 0)
            content_type = "application/javascript";
        else if (strcmp(extension, ".jpg") == 0 || strcmp(extension, ".jpeg") == 0)
            content_type = "image/jpeg";
        else if (strcmp(extension, ".png") == 0)
            content_type = "image/png";
    }
    return content_type;
}

void count_chars(char* data, int* characters, int* words, int* sentences)
{
    int i = 0;
    while (data[i] != '\0')
    {
        if (data[i] == ' ')
        {
            (*words)++;
            (*characters)++;
        }
        else if (data[i] == '.' || data[i] == '!' || data[i] == '?')
        {
            (*sentences)++;
            (*words)++;
            (*characters)++;
        }
        else
            (*characters)++;
        i++;
    }
}

void handle_post_request(char* request, int client_socket)
{
    char response[BUFFER_SIZE] = {0};

    char* message = extract_message(request);
    int characters = 0, words = 0, sentences = 0;

    count_chars(message, &characters, &words, &sentences);

    // Calculate the length of the response body
    int response_length = snprintf(NULL, 0, "Characters: %d\nWords: %d\nSentences: %d\n", characters, words, sentences);

    // Send the HTTP header with content type, content length, and connection keep-alive
    snprintf(response, sizeof(response), "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\nConnection: keep-alive\r\n\r\n", response_length);
    send(client_socket, response, strlen(response), 0);

    // Send the response body
    snprintf(response, sizeof(response), "Characters: %d\nWords: %d\nSentences: %d\n", characters, words, sentences);
    send(client_socket, response, strlen(response), 0);
}

void handle_get_request(char* request, int client_socket) 
{
    char response[BUFFER_SIZE] = {0};
    char buffer[BUFFER_SIZE] = {0};

    char* requested_file = strtok(NULL, " ");

    if (strcmp(requested_file, "/") == 0)
        requested_file = DEFAULT_PAGE;
    else
        requested_file++;

    char file_path[100];
    snprintf(file_path, sizeof(file_path), "%s/%s", root_directory, requested_file);

    // Serve the 404.jpg file statically always
    if (strcmp(requested_file, "images/404.jpg") == 0) 
    {
        snprintf(file_path, sizeof(file_path), "%s/%s", "./", "images/404.jpg");
    }

    // Check if the requested file exists
    FILE *file = fopen(file_path, "r");
    if (file == NULL) 
    {
        // If file not found, serve 404 page statically
        snprintf(file_path, sizeof(file_path), "%s/%s", "./", NOT_FOUND_PAGE);
        file = fopen(file_path, "r");
    }

    // Determine the content type based on file extension
    char *extension = strrchr(requested_file, '.');
    char* content_type = get_content_type(extension);
    
    // Determine the size of the file
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Send the HTTP header with content type, content length, and connection keep-alive
    snprintf(response, sizeof(response), "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\nConnection: keep-alive\r\n\r\n", content_type, file_size);
    send(client_socket, response, strlen(response), 0);

    // Send the content of the file
    int bytes_read = 0;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) 
    {
        if (send(client_socket, buffer, bytes_read, 0) != bytes_read) 
        {
            perror("Error sending file");
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

void* handle_request(void *arg) 
{
    int client_socket = *((int *)arg);
    char buffer[BUFFER_SIZE] = {0};

    struct timeval timeout;
    timeout.tv_sec = 10; // Set timeout to 10 seconds
    timeout.tv_usec = 0;

    setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));

    // Read the request from the client
    while(1)
    {
        int bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
        if (bytes_received <= 0) 
        {
            printf("Timeout or error occurred, closing connection\n");
            break;
        }

        buffer[bytes_received] = '\0';
        char* request = strdup(buffer);
        char* request_method = strtok(buffer, " ");

        // Handle POST request and GET request independently
        if(strcmp(request_method, "POST") == 0) 
        {
            handle_post_request(request, client_socket);
        }
        else if(strcmp(request_method, "GET") == 0)
        {
            handle_get_request(request, client_socket);
        }   
    }
    
    close(client_socket);
    pthread_exit(NULL);
}

int main(int argc, char *argv[]) 
{
    if (argc != 3) 
    {
        printf("Usage: %s <port> <root_directory>\n", argv[0]);
        return 1;
    }

    port = atoi(argv[1]);
    root_directory = (char *)malloc(sizeof(char) * strlen(argv[2]));
    strcpy(root_directory, argv[2]);

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) 
    {
        perror("Failed to create server socket");
        return 1;
    }

    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(server_socket, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) 
    {
        perror("Failed to bind server socket");
        close(server_socket);
        return 1;
    }

    if (listen(server_socket, MAX_CONNECTIONS) < 0) {
        perror("Failed to listen on server socket");
        close(server_socket);
        return 1;
    }

    printf("Server listening on port %d...\n", port);

    while (1) 
    {
        struct sockaddr_in client_address;
        socklen_t client_address_len = sizeof(client_address);

        int client_socket = accept(server_socket, (struct sockaddr *)&client_address, &client_address_len);
        if (client_socket < 0) 
        {
            perror("Failed to accept client connection");
            continue;
        }

        // Create a new thread to handle the client request
        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_request, (void *)&client_socket) != 0) 
        {
            perror("Thread creation failed");
            close(client_socket);
            continue;
        }

        // Detach the thread to avoid memory leak
        pthread_detach(tid);
    }

    // Close the server socket
    close(server_socket);

    return 0;
}   