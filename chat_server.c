#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include <string.h>
#include <sys/time.h>

#define BUFFER_SIZE 1024

int port;
int timeout_period;
int max_clients;

pthread_mutex_t client_count_lock;
int client_count = 0;

struct client{
    char *name;
    int client_fd;
    struct client* next;
};

struct client *client_list;
pthread_mutex_t list_lock;

bool chat_active = true;
pthread_mutex_t chat_lock;

//Server Socket Helpers
int create_socket();
void bind_socket(int sock);
void listen_for_connections(int sock);

void initialize_user_list();

//Client Helpers
int accept_client(int server_sock, struct sockaddr* client_addr, socklen_t* client_addr_size);
void print_client(struct sockaddr client_addr);
void serve_client(int client_fd);

void printList();
void sendUsersList(int client_fd);
struct client* addUser(int client_fd, char *client_name);
char *removeUser(int client_fd);
void broadcast_all(char *msg, int msg_size);
void broadcast_message(int from_client_fd, char *msg, int msg_size);
int checkUserExists(char *client_name);
void *handle_client(void *client_fd_ptr);


int main(int argc, char *argv[]){

    if (argc != 4) 
    {
        printf("Usage: %s <port> <max_clients <timeout_period>\n", argv[0]);
        return 1;
    }

    port = atoi(argv[1]);
    max_clients = atoi(argv[2]);
    timeout_period = atoi(argv[3]);
    
    int server_fd = create_socket();
    bind_socket(server_fd);
    listen_for_connections(server_fd);
    initialize_user_list();
    pthread_mutex_init(&client_count_lock,NULL);

    while(chat_active)
    {
        struct sockaddr_in client_addr;
        socklen_t client_addr_size = sizeof(client_addr);  
        
        // Accept incoming connection
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_addr_size);

        pthread_mutex_lock(&client_count_lock);
        client_count++;
        pthread_mutex_unlock(&client_count_lock);

        if(client_count > max_clients){
            close(client_fd);
            pthread_mutex_lock(&client_count_lock);
            client_count--;
            pthread_mutex_unlock(&client_count_lock);   
            continue;
        }

        struct timeval timeout;      
        timeout.tv_sec = timeout_period;
        timeout.tv_usec = 0;
        
        if (setsockopt (client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                    sizeof timeout) < 0)
            perror("setsockopt failed\n");

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
        pthread_create(&serve_thread, NULL, handle_client, (void*)(&client_fd));
    }
    
    // Close server socket
    close(server_fd);
    pthread_mutex_lock(&client_count_lock);
    client_count--; 
    pthread_mutex_unlock(&client_count_lock);

    return 0;
}

int create_socket(){
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("Error creating socket\n");
        exit(EXIT_FAILURE);
    }
    return sock;
}

void bind_socket(int sock){

    struct sockaddr_in server_addr;

    // Initialize server address structure
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    // Bind socket to address and port
    if (bind(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("Error binding socket\n");
        exit(EXIT_FAILURE);
    }
}

void listen_for_connections(int sock){
    // Listen for incoming connections
    if (listen(sock, 1) == -1) {
        perror("Error listening for connections\n");
        exit(EXIT_FAILURE);
    }
    printf("Server listening on port %d\n", port);
}

void initialize_user_list(){
    client_list = NULL;
    pthread_mutex_init(&list_lock, NULL);
}

int accept_client(int server_sock, struct sockaddr* client_addr, socklen_t* client_addr_size){
    int client_fd = accept(server_sock, (struct sockaddr*)&client_addr, client_addr_size);
    if (client_fd <= -1) 
    {
        printf("Error accepting connection");
        exit(EXIT_FAILURE);
    }  
    return client_fd; 
}

void serve_client(int client_fd){
    pthread_t serve_thread;
    pthread_create(&serve_thread, NULL, handle_client, (void*)(&client_fd));
}

void printList(){

    pthread_mutex_lock(&list_lock);
    struct client *cur = client_list;
    while(cur!=NULL){
        printf("%s ", cur->name);
        cur = cur->next;
    }
    printf("\n");
    pthread_mutex_unlock(&list_lock);
}

void sendUsersList(int client_fd){

    pthread_mutex_lock(&list_lock);
    struct client *cur = client_list;
    while(cur!=NULL){
        char sendUser_buf[128];
        strcpy(sendUser_buf, "\t");
        strcat(sendUser_buf, cur->name);
        sendUser_buf[strlen(cur->name)+1] = '\0';
        if(send(client_fd, sendUser_buf, strlen(sendUser_buf), 0) != strlen(sendUser_buf)){
            printf("Error sending message to %d", cur->client_fd);
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&list_lock);
}

struct client* addUser(int client_fd, char *client_name){

    struct client *new_client = (struct client*)malloc(sizeof(struct client));
    new_client->name = client_name;
    new_client->client_fd = client_fd;

    pthread_mutex_lock(&list_lock);
    if(client_list==NULL){
        client_list = new_client;
    }
    else{
        new_client->next = client_list;
        client_list = new_client;
    }
    pthread_mutex_unlock(&list_lock);

    return new_client;
}

char *removeUser(int client_fd){

    char *removed_user_name;
    struct client *removed_client;
    pthread_mutex_lock(&list_lock);

    if(client_list->client_fd == client_fd){
        client_list = client_list->next;
    }
    else{
        struct client *cur = client_list;
        while(cur->next!=NULL){
            if(cur->next->client_fd == client_fd){
                removed_user_name = strdup(cur->next->name);
                removed_client = cur->next;
                cur->next = cur->next->next;
                break;
            }   
            cur = cur->next;
        }
    }
    pthread_mutex_unlock(&list_lock);

    // free(removed_client);
    return removed_user_name;
}

void broadcast_all(char *msg, int msg_size){

    printf("%s\n", msg);
    pthread_mutex_lock(&list_lock);
    struct client *cur = client_list;
    while(cur!=NULL){
        if(send(cur->client_fd, msg, msg_size, 0) != msg_size){
            printf("Error sending message to %d", cur->client_fd);
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&list_lock);
}

void broadcast_message(int from_client_fd, char *msg, int msg_size){

    printf("%s\n", msg);
    pthread_mutex_lock(&list_lock);
    struct client *cur = client_list;
    while(cur!=NULL){
        if(cur->client_fd != from_client_fd){
            if(send(cur->client_fd, msg, msg_size, 0) != msg_size){
                printf("Error sending message to %d", cur->client_fd);
            }
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&list_lock);
}

int checkUserExists(char *client_name){

    int ret = 0;
    pthread_mutex_lock(&list_lock);
    struct client *cur = client_list;
    while(cur!=NULL){
        if(strcmp(client_name, cur->name)==0){
            ret = 1;
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&list_lock);
    return ret;
}

void *handle_client(void *client_fd_ptr){
    
    int client_fd = *(int *)client_fd_ptr;
    char client_name[128];

    char hello_buf[64] = "Enter Username";
    hello_buf[strlen(hello_buf)] = '\0';
    if(send(client_fd, hello_buf, strlen(hello_buf), 0) != strlen(hello_buf)){
        printf("Error sending message to %d", client_fd);
    }

    while(true){
        ssize_t bytes_received = recv(client_fd, client_name, BUFFER_SIZE, 0);
        client_name[bytes_received] = '\0';
        
        if (bytes_received > 0) {
            if(checkUserExists(client_name)){
                char buf[64] = "Username already exists, try again";
                buf[strlen(buf)] = '\0';
                if(send(client_fd, buf, strlen(buf), 0) != strlen(buf)){
                    printf("Error sending message to %d", client_fd);
                }
            }
            else{
                break;
            }
        } 
        else if (bytes_received == 0) {
            close(client_fd);
            pthread_mutex_lock(&client_count_lock);
            client_count--; 
            pthread_mutex_unlock(&client_count_lock);
            pthread_exit(NULL);
        } 
        else {
            printf("Hi\n");
            char buf1[64] = "You have been disconnected due to inactivity";
            buf1[strlen(buf1)] = '\0';
            if(send(client_fd, buf1, strlen(buf1), 0) != strlen(buf1)){
                printf("Error sending message to %d", client_fd);
            }
            close(client_fd);
            pthread_mutex_lock(&client_count_lock);
            client_count--; 
            pthread_mutex_unlock(&client_count_lock);
            pthread_exit(NULL);
        }

    }

    char welcome_buf[64] = "Welcome to the chat room!\nActive Users:";
    welcome_buf[strlen(welcome_buf)] = '\0';
    if(send(client_fd, welcome_buf, strlen(welcome_buf), 0) != strlen(welcome_buf)){
        printf("Error sending message to %d", client_fd);
    }

    struct client* client_node = addUser(client_fd, client_name);
    char join_buf[128];
    strcpy(join_buf, client_name);
    strcat(join_buf, " has entered the chat.");
    join_buf[strlen(join_buf)]='\0';
    broadcast_message(client_fd, join_buf, strlen(join_buf));
    sendUsersList(client_fd);

    
    char msg[BUFFER_SIZE];
    printList();

    while(true){
        ssize_t bytes_received = recv(client_fd, msg, BUFFER_SIZE, 0);
        msg[bytes_received] = '\0';
        
        char broadcast_msg[BUFFER_SIZE];

        if (bytes_received > 0) {

            if(strcmp(msg, "\\bye")==0){
                strcpy(broadcast_msg, client_name);
                char buf[64] = " has disconnected";
                strcat(broadcast_msg, buf);
                broadcast_msg[strlen(broadcast_msg)] = '\0';
                broadcast_message(client_fd, broadcast_msg, strlen(broadcast_msg));
                removeUser(client_fd);
                break;
            }
            else if(strcmp(msg, "\\list")==0){
                sendUsersList(client_fd);
            }
            else{
                strcpy(broadcast_msg, client_name);
                strcat(broadcast_msg, ": ");
                strcat(broadcast_msg, msg);
                broadcast_msg[strlen(client_name) + 2 + bytes_received] = '\0';
                broadcast_all(broadcast_msg, strlen(broadcast_msg));
            }
            
        } 
        else if (bytes_received == 0) {
            strcpy(broadcast_msg, client_name);
            char buf[64] = " has disconnected";
            strcat(broadcast_msg, buf);
            broadcast_msg[strlen(broadcast_msg)] = '\0';
            broadcast_message(client_fd, broadcast_msg, strlen(broadcast_msg));
            removeUser(client_fd);
            break;
        } 
        else {
            char buf1[64] = "You have been disconnected due to inactivity";
            buf1[strlen(buf1)] = '\0';
            if(send(client_fd, buf1, strlen(buf1), 0) != strlen(buf1)){
                printf("Error sending message to %d", client_fd);
            }
            strcpy(broadcast_msg, client_name);
            char buf[64] = " has disconnected due to inactivity";
            strcat(broadcast_msg, buf);
            broadcast_msg[strlen(broadcast_msg)] = '\0';
            broadcast_message(client_fd, broadcast_msg, strlen(broadcast_msg));
            removeUser(client_fd);
            break;
        }
    }

    close(client_fd);
    pthread_mutex_lock(&client_count_lock);
    client_count--; 
    pthread_mutex_unlock(&client_count_lock);
}
