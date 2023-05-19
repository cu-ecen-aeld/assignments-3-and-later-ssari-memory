#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <fcntl.h>


#define FILENAME "/var/tmp/aesdsocketdata"
#define PORT "9000"
#define MAX_PACKET_SIZE 1024


bool server_flag;
int sockfd, new_fd;
FILE *fptr;

void signal_handler(int signal_number){
    if ((signal_number == SIGINT) || (signal_number == SIGTERM)){
        syslog(LOG_INFO, "Caught signal, exiting\n");
        shutdown(sockfd, SHUT_RDWR);
        server_flag = false;
    }
}

int send_file(int socket_fd, FILE* file_fd) {
    int rc;
    size_t size;
    char buffer[MAX_PACKET_SIZE];

    rc = fseek(file_fd, 0, SEEK_SET);
    if (rc == -1) {
        syslog(LOG_ERR, "Error: %s\n", strerror(errno));
        return -1;
    }

    while ((size = fread(buffer, sizeof(char), MAX_PACKET_SIZE, file_fd)) > 0) {
        printf("send data: %.*s", (int)size, buffer);
        ssize_t sent_bytes = send(socket_fd, buffer, size, 0);
        if (sent_bytes == -1) {
            syslog(LOG_ERR, "Error sending data: %s\n", strerror(errno));
            return -1;
        } else if (sent_bytes < size) {
            syslog(LOG_ERR, "Incomplete data sent: %zd out of %zu bytes\n", sent_bytes, size);
            return -1;
        }
    }

    return 0;
}


int file_append(FILE* fd, char *buffer, int size) {
    int rc;

    rc = fseek(fd, 0, SEEK_END);
    if (rc == -1) {
        syslog(LOG_ERR, "Error: %s\n", strerror(errno));
        return -1;
    }

    size_t bytes_written = fwrite(buffer, sizeof(char), size, fd);
    if (bytes_written != size) {
        syslog(LOG_ERR, "Error writing to file: %s\n", strerror(errno));
        return -1;
    }

    return bytes_written;
}


int start_daemon(){

    pid_t pid = fork();
    if(pid > 0){
        exit(0);
    }
    chdir("/");
    return 0;
}

int main(int argc, char** argv){
    struct addrinfo hints;
    struct addrinfo *servinfo;
	struct sockaddr_storage their_addr;
    socklen_t addr_size;
    int ret;
	char buffer[MAX_PACKET_SIZE];
    struct sigaction new_action;
    memset(&new_action,0,sizeof(struct sigaction));

	openlog(NULL, 0, LOG_USER);
	syslog(LOG_INFO, "Start logging");
    printf("Start aesd socket\n");

    new_action.sa_handler=signal_handler;
    if(sigaction(SIGTERM, &new_action, NULL) != 0){
        syslog(LOG_ERR, "Error setting up sigaction for SIGTERM");
        return -1;
    }
    if(sigaction(SIGINT, &new_action, NULL) != 0){
        syslog(LOG_ERR, "Error setting up sigaction for SIGINT");
        return -1;
    }
    server_flag = true;
    
    sockfd = socket(PF_INET, SOCK_STREAM, 0);
    if(sockfd == -1){
        syslog(LOG_ERR, "Error opening socket: %s\n", strerror(errno));
        return -1;
    }

    memset(&hints, 0, sizeof hints);
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_INET;
    ret = getaddrinfo(NULL, PORT , &hints, &servinfo);
    if (ret != 0){
        syslog(LOG_ERR, "Error setting up getaddrinfo. Errno: %s\n", strerror(errno));
        return -1;
    }

    ret = bind(sockfd, servinfo->ai_addr, servinfo->ai_addrlen);
    if(ret == -1){
        syslog(LOG_ERR, "Error binding socket: %s\n", strerror(errno));
        return -1;
    }


    freeaddrinfo(servinfo);

    if(argc == 2 && strcmp(argv[1], "-d") == 0){
        start_daemon();
    }

	ret = listen(sockfd, 20);
    if(ret == -1){
        syslog(LOG_ERR, "Error listening to socket: %s\n", strerror(errno));
        return -1;
    }

    while(server_flag){
        addr_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &addr_size);
        if(new_fd == -1){
            syslog(LOG_ERR, "Error accepting socket: %s\n", strerror(errno));
            break;
        }

        char hoststr[NI_MAXHOST];
        char portstr[NI_MAXSERV];

        ret = getnameinfo((struct sockaddr *)&their_addr, addr_size, hoststr, sizeof(hoststr), portstr, sizeof(portstr), NI_NUMERICHOST | NI_NUMERICSERV);
        if(ret != 0){
            return -1;
        }
        syslog(LOG_INFO, "Accepted connection from %s", hoststr);


        fptr = fopen(FILENAME, "a+");

        if(fptr == NULL){
            syslog(LOG_ERR, "Error opening file var/tmp/aesdsocketdata: %s\n", strerror(errno));
            return 1;
        }

        while(1){
      
            ret = recv(new_fd, &buffer, MAX_PACKET_SIZE, 0);
            if(ret == 0){
                syslog(LOG_INFO, "Closed connection from %s", hoststr);
                break;
            }
            else if(ret < 0){
                printf("recv error\n");
                syslog(LOG_ERR, "Recv error: %s", strerror(errno));
                break;
            }
            else if (ret > 0){
                printf("received data\n");
                if(strchr(buffer, '\n')){
                    file_append(fptr, buffer, ret);
                    printf("written data: %.*s", ret, buffer);
                    fsync(fileno(fptr));

                    send_file(new_fd, fptr);
                }
                else if(ret == MAX_PACKET_SIZE){
                    file_append(fptr, buffer, ret);
                }
            }
        }
    }

    if (sockfd)
        close(sockfd);
    if (new_fd)
        close(new_fd);
    if (fptr)
        fclose(fptr);
    remove(FILENAME);
    printf("Server closed\n");

	return 0;
}