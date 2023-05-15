#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

#define PORT 9000
#define FILENAME "/var/tmp/aesdsocketdata"
#define MAX_PACKET_SIZE 1024

int sockfd;

// Signal handler to gracefully exit on SIGINT or SIGTERM
void signal_handler(int sig) {
    syslog(LOG_INFO, "Caught signal, exiting");
    close(sockfd);
    remove(FILENAME);
    exit(0);
}

int main(int argc, char** argv) {
    int newsockfd, n;
    socklen_t clilen;
    char buffer[MAX_PACKET_SIZE];
    struct sockaddr_in serv_addr, cli_addr;
    int flag_deamon=0;

    if(argc == 2){
        flag_deamon = (!strcmp(argv[1],"-d"))? 1:0;
    }

    // Create a socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("ERROR opening socket");
        return -1;
    }

    // Bind the socket to a port
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(PORT);
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR on binding");
        return -1;
    }

    // Listen for incoming connections
    listen(sockfd, 5);
    clilen = sizeof(cli_addr);

    // Register the signal handler for SIGINT and SIGTERM
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Deamon starts
    if(flag_deamon){
        pid_t pid = fork();
        if (pid > 0)
            return 0;
        else    
            return -1;
    }

    // Loop forever, accepting new connections and handling them
    while (1) {
        // Accept a new connection
        newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
        if (newsockfd < 0) {
            perror("ERROR on accept");
            continue;
        }

        // Log the accepted connection
        char *client_ip = inet_ntoa(cli_addr.sin_addr);
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        // Open the file to append received data
        FILE *file = fopen(FILENAME, "a");
        if (file == NULL) {
            perror("ERROR opening file");
            close(newsockfd);
            continue;
        }

        // Receive data over the connection and append to the file
        while ((n = recv(newsockfd, buffer, MAX_PACKET_SIZE, 0)) > 0) {
            buffer[n] = '\0';
            char *packet_end = strchr(buffer, '\n');
            if (packet_end == NULL) {
                fputs(buffer, file);
            } else {
                size_t packet_size = packet_end - buffer + 1;
                fwrite(buffer, packet_size, 1, file);
                break;
            }
        }

        // Return the contents of the file to the client
        fseek(file, 0, SEEK_SET);
        while ((n = fread(buffer, 1, MAX_PACKET_SIZE, file)) > 0) {
            send(newsockfd, buffer, n, 0);
        }

        // Close the file and the connection
        fclose(file);
        close(newsockfd);

        // Log the closed connection
        syslog(LOG_INFO, "Closed connection from %s", client_ip);
    }

    return 0;
}