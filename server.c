#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>

#define BUFFER (1<<12)
#define PORT 1738

typedef struct {
    char *buffer[BUFFER];
    int head, tail;
    int full, empty;
    pthread_mutex_t *mutex;
    pthread_cond_t *notFull, *notEmpty;
} queue;

// Important server data
typedef struct {
    fd_set server_read_fds;
    int socket_fd;
    int client_sockets[BUFFER];
    int num_clients;
    pthread_mutex_t *client_list_mutex;
    queue *queue;
} chat_data_vars;

// Important client data
typedef struct {
    chat_data_vars *data;
    int client_socket_fd;
} client_handler_vars;

void start_chat(int socket_fd);
void remove_client(chat_data_vars *data, int client_socket_fd);

void *new_client_handler(void *data);
void *client_handler(void *chv);
void *message_handler(void *data);

void queue_destroy(queue *q);
queue* queue_init();
void queue_push(queue *q, char* msg);
char* queue_pop(queue *q);

int main(int argc, char *argv[]) {
    struct sockaddr_in server_addr;
    int socket_fd;

    if ((socket_fd = socket(AF_INET, SOCK_STREAM, 0))== -1) {
        perror("Socket creation failed");
        exit(1);
    }

    // Sets up and binds the socket
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORT);

    bind(socket_fd, (struct sockaddr *)(&server_addr), sizeof(struct sockaddr_in));
    listen(socket_fd, 1);

    start_chat(socket_fd);

    close(socket_fd);
}

// Spawns the new client handler thread and message consumer thread
void start_chat(int socket_fd) {
    chat_data_vars data;
    data.num_clients = 0;
    data.socket_fd = socket_fd;
    data.queue = queue_init();
    data.client_list_mutex = (pthread_mutex_t *) malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(data.client_list_mutex, NULL);

    // Start thread to handle new clients
    pthread_t connectionThread;
    if ((pthread_create(&connectionThread, NULL, (void *)&new_client_handler, (void *)&data)) == 0) {
        fprintf(stderr, "Started connection handler.\n");
    }

    FD_ZERO(&(data.server_read_fds));
    FD_SET(socket_fd, &(data.server_read_fds));

    // Start thread to receive messages
    pthread_t messages_thread;
    if ((pthread_create(&messages_thread, NULL, (void *)&message_handler, (void *)&data)) == 0) {
        fprintf(stderr, "Started message handler.\n");
    }

    pthread_join(connectionThread, NULL);
    pthread_join(messages_thread, NULL);

    queue_destroy(data.queue);
    pthread_mutex_destroy(data.client_list_mutex);
    free(data.client_list_mutex);
}

// Initializes queue
queue* queue_init() {
    queue *q = (queue *)malloc(sizeof(queue));

    q->empty = 1;
    q->full = q->head = q->tail = 0;

    q->mutex = (pthread_mutex_t *) malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(q->mutex, NULL);
    q->notFull = (pthread_cond_t *) malloc(sizeof(pthread_cond_t));
    pthread_cond_init(q->notFull, NULL);
    q->notEmpty = (pthread_cond_t *) malloc(sizeof(pthread_cond_t));
    pthread_cond_init(q->notEmpty, NULL);

    return q;
}

// Free a queue
void queue_destroy(queue *q) {
    pthread_mutex_destroy(q->mutex);
    pthread_cond_destroy(q->notFull);
    pthread_cond_destroy(q->notEmpty);
    free(q->mutex);
    free(q->notFull);
    free(q->notEmpty);
    free(q);
}

// Push to end of queue
void queue_push(queue *q, char* msg) {
    q->buffer[q->tail] = msg;
    q->tail++;
    if(q->tail == BUFFER)
        q->tail = 0;
    if(q->tail == q->head)
        q->full = 1;
    q->empty = 0;
}

// Pop front of queue
char* queue_pop(queue *q) {
    char* msg = q->buffer[q->head];
    q->head++;
    if(q->head == BUFFER)
        q->head = 0;
    if(q->head == q->tail)
        q->empty = 1;
    q->full = 0;

    return msg;
}



// Remove the socket from the list of active client sockets and closes it
void remove_client(chat_data_vars *data, int client_socket_fd) {
    pthread_mutex_lock(data->client_list_mutex);
    for (int i = 0; i < BUFFER; i++) {
        if (data->client_sockets[i] == client_socket_fd) {
            data->client_sockets[i] = 0;
            close(client_socket_fd);
            data->num_clients--;
            break;
        }
    }
    pthread_mutex_unlock(data->client_list_mutex);
}

// Thread to handle new connections. Add client's fd to list of client fds and spawn a new client_handler thread for it
void *new_client_handler(void *data) {
    chat_data_vars *chat_data = (chat_data_vars *) data;
    while(1) {
        int client_socket_fd = accept(chat_data->socket_fd, NULL, NULL);
        if (client_socket_fd > 0) {
            fprintf(stderr, ":) Server accepted new client. Socket: %d\n", client_socket_fd);

            // Obtain lock on clients list and add new client in
            pthread_mutex_lock(chat_data->client_list_mutex);
            if (chat_data->num_clients < BUFFER) {
                // Add new client to list
                for (int i = 0; i < BUFFER; i++) {
                    if (!FD_ISSET(chat_data->client_sockets[i], &(chat_data->server_read_fds))) {
                        chat_data->client_sockets[i] = client_socket_fd;
                        i = BUFFER;
                    }
                }

                FD_SET(client_socket_fd, &(chat_data->server_read_fds));

                // Spawn thread to handle client's messages
                client_handler_vars chv;
                chv.client_socket_fd = client_socket_fd;
                chv.data = chat_data;

                pthread_t clientThread;
                if ((pthread_create(&clientThread, NULL, (void *)&client_handler, (void *)&chv)) == 0) {
                    chat_data->num_clients++;
                    fprintf(stderr, ":) Client has joined chat. Socket: %d\n", client_socket_fd);
                }
                else
                    close(client_socket_fd);
            }
            pthread_mutex_unlock(chat_data->client_list_mutex);
        }
    }
}

// The "producer" -- Listens for messages from client to add to message queue
void *client_handler(void *chv) {
    client_handler_vars *vars = (client_handler_vars *)chv;
    chat_data_vars *data = (chat_data_vars *)vars->data;

    queue *q = data->queue;
    int client_socket_fd = vars->client_socket_fd;

    char msg_buffer[BUFFER];
    while (1) {
        msg_buffer[read(client_socket_fd, msg_buffer, BUFFER - 1)] = '\0';

        // If the client sent exit, remove them from the client list and close their socket
        if (strcmp(msg_buffer, "") == 0) {
            fprintf(stderr, ":( Client on socket %d disconnected.\n", client_socket_fd);
            remove_client(data, client_socket_fd);
            return NULL;
        }
        else {
            // Wait for queue to not be full before pushing message
            while (q->full) pthread_cond_wait(q->notFull, q->mutex);

            // Obtain lock, push message to queue, unlock, set condition variable
            pthread_mutex_lock(q->mutex);
            fprintf(stderr, "-> Queueing: %s", msg_buffer);
            queue_push(q, msg_buffer);
            pthread_mutex_unlock(q->mutex);
            pthread_cond_signal(q->notEmpty);
        }
    }
}

// Wait for the queue to have messages then remove them from queue and broadcast to clients
void *message_handler(void *data) {
    chat_data_vars *chat_data = (chat_data_vars *)data;
    queue *q = chat_data->queue;
    int *client_sockets = chat_data->client_sockets;

    while (1) {
        // Obtain lock and pop message from queue when not empty
        pthread_mutex_lock(q->mutex);
        while (q->empty) {
            pthread_cond_wait(q->notEmpty, q->mutex);
        }
        char* msg = queue_pop(q);
        pthread_mutex_unlock(q->mutex);
        pthread_cond_signal(q->notFull);

        // Broadcast message to all connected clients
        fprintf(stderr, "<- Broadcasting: %s", msg);
        for (int i = 0; i < chat_data->num_clients; i++) {
            int socket = client_sockets[i];
            if (socket != 0 && write(socket, msg, BUFFER - 1) == -1)
                perror("Socket write failed: ");
        }
    }
}
