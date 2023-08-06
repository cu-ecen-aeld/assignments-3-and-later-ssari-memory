#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <syslog.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/stat.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>

#define SOCKET_TARGET_PORT "9000"
#define BACKLOG 20
#define MSG_BUFFER_SIZE 1000

struct SocketData {
	bool socket_complete;
	pthread_mutex_t *mutex;
	int accepted_fd;
	char *msg;
};

struct LinkedList {
	struct LinkedList *next;
	pthread_t thread_id;
	struct SocketData *socket;
};

const char* TMP_FILE = "/var/tmp/aesdsocketdata";
int sockfd;
pthread_mutex_t MUTEX = PTHREAD_MUTEX_INITIALIZER;

bool server_is_running = true;

struct LinkedList *HEAD = NULL;

static void signal_handler(int signal_number);
static void start_daemon();
void *get_in_addr(struct sockaddr *sa);
void *socket_thread(void *socket_param);
void cleanup_socket(bool socket_was_terminated);
void linked_list_add_node(struct LinkedList *node);
void timer_10sec(int signal_number);

void timer_10sec(int signal_number)
{
	char timestamp[200];
	char output_timestamp[300];
	time_t abs_time;
	struct tm *local_time;
	FILE *fp;
	
	syslog(LOG_DEBUG, "aesdsocket testing timescript");
	strcpy(output_timestamp, "timestamp:");
	abs_time = time(NULL);
	local_time = localtime(&abs_time);
	strftime(timestamp, sizeof(timestamp), "%a, %d %b %Y %T %z", local_time);
	strcat(output_timestamp, timestamp);
	strcat(output_timestamp, "\n");
	pthread_mutex_lock(&MUTEX);
	fp = fopen(TMP_FILE, "a+");
	fwrite(output_timestamp, sizeof(char), strlen(output_timestamp), fp);
	fclose(fp);
	pthread_mutex_unlock(&MUTEX);
	syslog(LOG_DEBUG, "aesdsocket ending timescript");
}


void linked_list_add_node(struct LinkedList *node)
{
	if (NULL == HEAD)
	{
		HEAD = node;
	}
	else
	{
		struct LinkedList *curr_head = HEAD;
		while (curr_head->next)
		{
			curr_head = curr_head->next;
		}
		curr_head->next = node;
	}
}


void cleanup_socket(bool socket_was_terminated)
{
	struct LinkedList *curr_head = HEAD;
	struct LinkedList *prev_head = NULL;
	while (NULL != curr_head)
	{
		if (socket_was_terminated || curr_head->socket->socket_complete)
		{
			pthread_join(curr_head->thread_id, NULL);
			if (0 <= curr_head->socket->accepted_fd)
			{
				close(curr_head->socket->accepted_fd);
			}
			if (NULL != curr_head->socket->msg)
			{
				free(curr_head->socket->msg);
			}
			free(curr_head->socket);
			if (NULL == prev_head)
			{
				HEAD = curr_head->next;
				free(curr_head);
				curr_head = HEAD;
			}
			else
			{
				prev_head->next = curr_head->next;
				free(curr_head);
				curr_head = prev_head->next;
			}
			
		}
		else
		{
			prev_head = curr_head;
			curr_head = prev_head->next;
		}
	}
}


static void signal_handler(int signal_number)
{
	if ((signal_number == SIGINT) || (signal_number == SIGTERM))
	{
		server_is_running = false;
		cleanup_socket(true);
		close(sockfd);
		remove(TMP_FILE);
		syslog(LOG_DEBUG, "Killed aesdsocket");
		exit(EXIT_SUCCESS);
	}
	else
	{
		syslog(LOG_DEBUG, "Failed to kill aesdsocket");
		exit(EXIT_FAILURE);
	}
}

// adapted from https://stackoverflow.com/questions/17954432/creating-a-daemon-in-linux
static void start_daemon()
{
	pid_t pid;
	
	pid = fork();
	
	if (pid < 0)
	{
		exit(EXIT_FAILURE);
	}
	else if (pid > 0)
	{
		exit(EXIT_SUCCESS);
	}
	
	if (setsid() < 0)
	{
		exit(EXIT_FAILURE);
	}
	
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
	
	pid = fork();
	if (pid < 0)
	{
		exit(EXIT_FAILURE);
	}
	else if (pid > 0)
	{
		exit(EXIT_SUCCESS);
	}
	
	umask(0);
	
	chdir("/");
	int x;
	for ( x = 2; x>=0; x--)
	{
		close(x);
	}
}


void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET)
	{
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void *socket_thread(void *socket_param)
{
	syslog(LOG_DEBUG, "aesdsocket in accepted connection");
	struct SocketData* socket = (struct SocketData*) socket_param;
	int msg_len = 0;
	int bytes_received = 0;
	char msg_buffer[MSG_BUFFER_SIZE];
	char output_buffer[MSG_BUFFER_SIZE];
	int bytes_read;
	bool is_receiving_message = true;
	FILE *fp;
	
	syslog(LOG_DEBUG, "aesdsocket in connection: variables assigned");
	
	while (is_receiving_message)
	{
		bytes_received = recv(socket->accepted_fd, msg_buffer, MSG_BUFFER_SIZE, 0);
		if (bytes_received < 0)
		{
			perror("Bytes received: ");
			break;
		}
		else if (bytes_received == 0)
		{
			is_receiving_message = false;
		}
		else
		{
			
			socket->msg = realloc(socket->msg, msg_len + bytes_received);
		
			if (NULL == socket->msg)
			{
				printf("Not enough memory\n\r");
			}
			else
			{
				for (int i = 0; i<bytes_received;i++)
				{
					socket->msg[msg_len] = msg_buffer[i];
					++msg_len;
					if (msg_buffer[i] == '\n')
					{
						pthread_mutex_lock(socket->mutex);
						fp = fopen(TMP_FILE, "a+");
						fwrite(socket->msg, sizeof(char), msg_len, fp);
						rewind(fp);
						while ((bytes_read = fread(output_buffer, 1, MSG_BUFFER_SIZE, fp)) > 0)
						{
							send(socket->accepted_fd, output_buffer, bytes_read, 0);
						}
						fclose(fp);
						pthread_mutex_unlock(socket->mutex);
						msg_len = 0;
					}
				}
			}
		}
	}
	
	return NULL;
}


int main(int argc, char *argv[])
{
	struct sockaddr_storage received_addr;
	socklen_t addr_size;
	struct addrinfo send_conf, *complete_conf;
	int status;
	char received_IP[INET6_ADDRSTRLEN];
	bool do_start_daemon = false;
	int opt;
	
	struct itimerval delay;
	
	delay.it_value.tv_sec = 10;
	delay.it_value.tv_usec = 0;
	delay.it_interval.tv_sec = 10;
	delay.it_interval.tv_usec = 0;
	
	
	
	while ((opt = getopt(argc, argv, "d")) != -1)
	{
		if (opt == 'd')
		{
			do_start_daemon = true;
		}
	}
	
	memset(&send_conf, 0 , sizeof(send_conf));
	send_conf.ai_family = AF_UNSPEC;
	send_conf.ai_socktype = SOCK_STREAM;
	send_conf.ai_flags = AI_PASSIVE;
	
	status = getaddrinfo(NULL, SOCKET_TARGET_PORT, &send_conf, &complete_conf);
	
	if (status == -1)
	{
		perror("getaddrinfo failed: ");
		return -1;
	}
	
	sockfd = socket(complete_conf->ai_family, complete_conf->ai_socktype, complete_conf->ai_protocol);
	
	if (sockfd == -1)
	{
		perror("socket failed: ");
		return -1;
	}
	
	status = bind(sockfd, complete_conf->ai_addr, complete_conf->ai_addrlen);
	
	if (status == -1)
	{
		perror("bind failed: ");
		return -1;
	}
	
	if (do_start_daemon)
	{
		start_daemon();
	}
	else
	{
		signal(SIGINT, signal_handler);
		signal(SIGTERM, signal_handler);
	}
	
	syslog(LOG_DEBUG, "aesdsocket started daemon");
	
	signal(SIGALRM, timer_10sec);
	setitimer(ITIMER_REAL, &delay, NULL);
	
	freeaddrinfo(complete_conf);
	
	
	
	status = listen(sockfd, BACKLOG);
	
	if (status == -1)
	{
		perror("listen failed: ");
		return -1;
	}

	
	syslog(LOG_DEBUG, "aesdsocket starting server");
	
	while (server_is_running)
	{
		
		addr_size = sizeof(received_addr);
		
		syslog(LOG_DEBUG, "aesdsocket allocating structs");
		
		struct LinkedList *node = (struct LinkedList *) malloc(sizeof(struct LinkedList));
		node->next = NULL;
		node->thread_id = 0;
		node->socket = (struct SocketData *) malloc(sizeof(struct SocketData));
		node->socket->socket_complete = false;
		node->socket->mutex = &MUTEX;
		node->socket->msg = NULL;
		node->socket->accepted_fd = -1;
		linked_list_add_node(node);
		
		node->socket->accepted_fd = accept(sockfd, (struct sockaddr *)&received_addr, &addr_size);
		
		syslog(LOG_DEBUG, "aesdsocket allocating structs finished");
		
		if (node->socket->accepted_fd == -1)
		{
			syslog(LOG_DEBUG, "aesdsocket failed to accept connection");
			perror("accept failed: ");
			continue;
		}
		
		inet_ntop(received_addr.ss_family, get_in_addr((struct sockaddr *) &received_addr), received_IP, sizeof(received_IP));
		syslog(LOG_DEBUG, "Accepted connection from %s\n", received_IP);
		
		syslog(LOG_DEBUG, "aesdsocket adding node");
		
		
		syslog(LOG_DEBUG, "aesdsocket adding node finished");
		
		pthread_create(&(node->thread_id), NULL, socket_thread, (void *) node->socket);
		
		cleanup_socket(false);
			
	}
	return 0;
}