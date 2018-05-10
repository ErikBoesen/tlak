#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define BUFFER 1024
#define PORT 1738

static int socket_fd;

int main(int argc, char *argv[]) {
    struct sockaddr_in server_addr;
    struct hostent *host;

    if (argc < 2) {
        fprintf(stderr, "usage: client [host]\n");
        return 1;
    }
    host = gethostbyname(argv[1]);
    if ((socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        fprintf(stderr, "can't create socket\n");
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr = *((struct in_addr *)host->h_addr_list[0]);
    server_addr.sin_port = htons(PORT);
    if (connect(socket_fd, (struct sockaddr *)(&server_addr), sizeof(struct sockaddr)) < 0) {
        perror("Couldn't connect to server");
        return 1;
    }

    fd_set client_fds;
    char chatBuffer[BUFFER], msgBuffer[BUFFER];

    while (1) {
        // Reset the fd set each time since select() modifies it
        FD_ZERO(&client_fds);
        FD_SET(socket_fd, &client_fds);
        FD_SET(0, &client_fds);
        if (select(FD_SETSIZE, &client_fds, NULL, NULL, NULL) != -1) { // wait for an available fd
            for (int fd = 0; fd < FD_SETSIZE; fd++) {
                if (FD_ISSET(fd, &client_fds)) {
                    if (fd == socket_fd) {
                        // Receive data from server
                        int numBytesRead = read(socket_fd, msgBuffer, BUFFER - 1);
                        msgBuffer[numBytesRead] = '\0';
                        printf("%s", msgBuffer);
                        memset(&msgBuffer, 0, sizeof(msgBuffer));
                    } else if (fd == 0) {
                        // Read from stdin and send to server
                        fgets(chatBuffer, BUFFER - 1, stdin);
                        if(write(socket_fd, chatBuffer, BUFFER - 1) == -1) perror("write failed: ");
                        memset(&chatBuffer, 0, sizeof(chatBuffer));
                    }
                }
            }
        }
    }
}
