#include <syslog.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <time.h>
#include <stdbool.h>
#include <pthread.h>
#include "queue.h"

#define USE_AESD_CHAR_DEVICE 1

#if USE_AESD_CHAR_DEVICE
#define ENABLE_TIMESTAMP 0
#define ENABLE_SYS_FILE 0
#else
#define ENABLE_TIMESTAMP 1
#define ENABLE_SYS_FILE 1
#endif

#define PORT 9000
#define MEM_SIZE 1024

#if ENABLE_TIMESTAMP
typedef struct {
	pthread_mutex_t *mutex;
	pthread_t tid;
	int fd;
}thread_time;
#endif

#if ENABLE_SYS_FILE
#define FILE_PATH "/var/tmp/aesdsocketdata"
#else
#define FILE_PATH "/dev/aesdchar"
#endif

typedef struct {
	pthread_mutex_t *mutex;
	pthread_t tid;
	int fd;
	int nsfd;
	char cln_addr[INET6_ADDRSTRLEN];
	bool thread_complete;
}thread_client;

struct entry {
	thread_client thread_client_param;
	SLIST_ENTRY(entry) entries;
};

SLIST_HEAD(slisthead, entry) head;

#if ENABLE_TIMESTAMP
thread_time *thread_time_param=NULL;
#endif
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
int sfd=-1, fd=-1;
bool srv_terminated=false;

void cleanup(){

	srv_terminated=true;

	while (!SLIST_EMPTY(&head)) {  
		struct entry *checkData=NULL;
		checkData = SLIST_FIRST(&head);
		pthread_join(checkData->thread_client_param.tid,NULL);
		SLIST_REMOVE_HEAD(&head, entries);
		free(checkData);
	}
	SLIST_INIT(&head);

#if ENABLE_TIMESTAMP
	if(thread_time_param!=NULL){
		if(pthread_cancel(thread_time_param->tid)){
			syslog(LOG_ERR,"pthread_canel() failed\n");
		}
		free(thread_time_param);	
	}
#endif
	if(sfd!=-1){
		shutdown(sfd,SHUT_RDWR);
		close(sfd);
	}
	if(fd!=-1){
		close(fd);
#if ENABLE_SYS_FILE
		unlink(FILE_PATH);
#endif
	}
	if(pthread_mutex_destroy(&mutex)){
		syslog(LOG_ERR,"pthread_mutex_destroy() failed\n");
	}

	closelog();

}

void signalHandler(int signal){

	if(signal==2||signal==15){
		cleanup();
		syslog(LOG_DEBUG,"caught signal, exiting\n");
		exit(0);
	}

}

void *clientThread(void *thread_param){

	sigset_t sigset;
	sigfillset(&sigset);
	sigprocmask(SIG_BLOCK, &sigset, NULL);

	thread_client* thread_args = (thread_client *) thread_param;
	thread_args->thread_complete=false;
	thread_args->tid=pthread_self();
	char *packet=NULL;
	int packetSize=MEM_SIZE,currPackLen=0,offset=0;

	while(!srv_terminated){

		packet=(char*)realloc( packet, packetSize);
		if(packet==NULL){
			syslog(LOG_ERR,"realloc() failed\n");
			thread_args->thread_complete=true;
			break;
		}

		int recvLen=recv( thread_args->nsfd, packet+offset, MEM_SIZE, 0);
		if(recvLen==0||recvLen==-1){
			syslog(LOG_DEBUG,"Closed connection from %s\n", thread_args->cln_addr);
			thread_args->thread_complete=true;
			break;
		}

		for( int i=offset;i<offset+recvLen;i++){
			if(packet[i]=='\n'){
				currPackLen=i+1;
				break;
			}
		}

		offset=offset+recvLen;
		if(currPackLen){

			if(pthread_mutex_lock(thread_args->mutex)==0){

				int writeLen=write(thread_args->fd, packet, currPackLen);
				if(writeLen==-1){
					syslog(LOG_DEBUG,"write() packet failed\n");
					thread_args->thread_complete=true;
					break;
				}
#if ENABLE_SYS_FILE
				if(lseek(thread_args->fd,0,SEEK_SET)==-1){
					syslog(LOG_DEBUG,"lseek() failed\n");
					thread_args->thread_complete=true;
					break;
				}
#endif
				char buf[MEM_SIZE+1];
				int readLen=0,sendLen=0;
				while((readLen=read(thread_args->fd,buf,MEM_SIZE))){
					sendLen=send(thread_args->nsfd, buf, readLen, 0);
					if(sendLen==-1){
						syslog(LOG_DEBUG,"send() packets failed\n");
						thread_args->thread_complete=true;
						break;
					}
				}
				if(sendLen==-1){
					thread_args->thread_complete=true;
					break;
				}

				if(pthread_mutex_unlock(thread_args->mutex))
					syslog(LOG_DEBUG,"pthread_mutex_unlock() failed\n");

			}
			else{
				syslog(LOG_DEBUG,"pthread_mutex_lock() failed\n");
			}

			offset=offset-currPackLen;
			if(offset!=0){
				strncpy(packet,packet+currPackLen+1,offset);
			}
			memset(packet+offset,0,packetSize-offset);
			currPackLen=0;
			packetSize=offset+MEM_SIZE;
		}
		else{
			packetSize=packetSize+MEM_SIZE;
		}
	}


	if(packet!=NULL){
		free(packet);
		packet=NULL;
	}

	shutdown(thread_args->nsfd,SHUT_RDWR);
	close(thread_args->nsfd);

	thread_args->thread_complete=true;
	pthread_exit(thread_args);
}


void socketServer(){

	struct sockaddr_in srv,cln;
	sfd=socket(AF_INET,SOCK_STREAM,0);
	if(sfd==-1){
		syslog(LOG_ERR,"socket() failed\n");
		closelog();
		exit(1);
	}

	syslog(LOG_DEBUG,"Socket created....\n");
	srv.sin_family=AF_INET;
	srv.sin_port=htons(PORT);
	srv.sin_addr.s_addr=inet_addr("0.0.0.0");

	int yes=1;
	if(setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR|SO_REUSEPORT, &yes, sizeof(int)) == -1) {
		syslog(LOG_ERR,"setsockopt() failed\n");
		cleanup();
		exit(1);
	}

	if(bind(sfd,(struct sockaddr *)&srv, sizeof(srv))){
		syslog(LOG_ERR,"bind() failed\n");
		cleanup();
		exit(1);
	}

	if(listen(sfd,5)){
		syslog(LOG_ERR,"listen() failed\n");
		cleanup();
		exit(1);
	}


	while(1){

		unsigned int len=sizeof(cln);
		syslog(LOG_DEBUG,"Waiting for client....\n");
		int nsfd=accept(sfd,(struct sockaddr*)&cln, &len);
		if(nsfd==-1){
			syslog(LOG_ERR,"accept() failed\n");
			cleanup();
			exit(1);
		}
		syslog(LOG_DEBUG,"Accepted connection from %s\n", inet_ntoa(cln.sin_addr));

		struct entry *data=(struct entry*)malloc(sizeof(struct entry));

		if(data!=NULL){

			data->thread_client_param.mutex=&mutex;
			data->thread_client_param.fd=fd;
			data->thread_client_param.nsfd=nsfd;
			strcpy(data->thread_client_param.cln_addr, inet_ntoa(cln.sin_addr));
			data->thread_client_param.thread_complete=false;

			if(pthread_create(&(data->thread_client_param.tid), NULL, clientThread, &(data->thread_client_param))){
				syslog(LOG_ERR,"pthread_create() client failed\n");
			}

			SLIST_INSERT_HEAD(&head, data, entries);

		}
		else{
			syslog(LOG_ERR,"malloc() failed for clent thread param\n");
		}

		struct entry *checkData=NULL, *checkList=NULL;
		checkData = SLIST_FIRST(&head);
		SLIST_FOREACH_SAFE(checkData, &head, entries, checkList){
			if(checkData->thread_client_param.thread_complete){
				pthread_join(checkData->thread_client_param.tid,NULL);
				SLIST_REMOVE(&head, checkData, entry, entries);
				free(checkData);
			}
		}

	}

}

#if ENABLE_TIMESTAMP
void *timestampThread(void *thread_param){

	int unused;
	if(pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &unused)){
		syslog(LOG_ERR,"pthread_setcancelstate() failed\n");
		kill(getpid(),2);
	}
	if(pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, &unused)){
		syslog(LOG_ERR,"pthread_setcancelstate() failed\n");
		kill(getpid(),2);
	}

	sigset_t sigset;
	sigfillset(&sigset);
	sigprocmask(SIG_BLOCK, &sigset, NULL);

	thread_time* thread_args = (thread_time *) thread_param;

	char outstr[100];
	memset(outstr,0,sizeof(outstr));
	time_t t;
	struct tm *tmp;

	while(!srv_terminated){

		t = time(NULL);
		tmp = localtime(&t);
		if (tmp == NULL) {
			syslog(LOG_ERR,"localtime() failed\n");
			kill(getpid(),2);
		}

		if (strftime(outstr, sizeof(outstr), "timestamp: %G %B %A %T %n", tmp) == 0) {
			syslog(LOG_ERR,"strftime() failed\n");
			kill(getpid(),2);
		}

		if(pthread_mutex_lock(thread_args->mutex)==0){

			int writeLen=write(thread_args->fd, outstr, strlen(outstr));
			if(writeLen==-1){
				syslog(LOG_DEBUG,"write() failed in timestamp\n");
				kill(getpid(),2);
			}

			if(pthread_mutex_unlock(thread_args->mutex)){
				syslog(LOG_DEBUG,"pthread_mutex_unlock() failed in timestamp\n");
				kill(getpid(),2);
			}

		}
		else{
			syslog(LOG_DEBUG,"pthread_mutex_lock() failed in timestamp\n");
			kill(getpid(),2);
		}

		sleep(10);

	}

	pthread_exit(thread_param);

}
#endif

int main( int argc, char *argv[]){

	openlog("aesdsocket", 0, LOG_USER);

	struct sigaction s;
	s.sa_handler=signalHandler;
	sigemptyset(&s.sa_mask);
	s.sa_flags=0;
	sigaction(2,&s,0);
	sigaction(15,&s,0);

	sigset_t sigset;
	sigfillset(&sigset);
	sigdelset(&sigset,2);
	sigdelset(&sigset,15);
	sigprocmask(SIG_BLOCK, &sigset, NULL);

	char *option="-d";
	int enableDaemon=0;

	if(argc>2){
		syslog(LOG_ERR,"More arguments than expected\n");
		closelog();
		exit(1);
	}

	if(argc==2){

		if(strcmp(option,argv[1])==0){
			enableDaemon=1;
			syslog(LOG_DEBUG,"Running as daemon\n");
		}
		else{
			syslog(LOG_ERR,"Invalid argument use -d\n");
			closelog();
			exit(1);
		}
	}

	if(enableDaemon){

		pid_t pid=0;
		pid_t sid=0;

		pid=fork();
		if(pid<0){
			syslog(LOG_ERR,"fork() failed\n");
			exit(1);
		}
		if(pid>0){
			exit(0); //exit the parent
		}

		umask(0);
		sid=setsid();

		if(sid<0){
			syslog(LOG_ERR,"setsid() failed\n");
			exit(1);
		}

		int ret=chdir("/");
		if(ret<0){
			syslog(LOG_ERR,"chdir() failed\n");
			exit(1);
		}

		close(0);
		close(1);
		close(2);
		int fdNull=open("/dev/null", O_RDWR, 0666);
		if(fdNull<0){
			syslog(LOG_ERR,"open() /dev/null failed\n");
			exit(1);
		}
		dup(0);
		dup(0);
		syslog(LOG_DEBUG,"Daemon PID=%d\n",getpid());

	}

	SLIST_INIT(&head);
	fd=open(FILE_PATH,O_CREAT|O_TRUNC|O_RDWR,0644);
	if(fd==-1){
		syslog(LOG_ERR,"open() "FILE_PATH" failed\n");
		exit(1);
	}

#if ENABLE_TIMESTAMP
	thread_time_param = (thread_time *) malloc(sizeof(thread_time));

	thread_time_param->mutex=&mutex;
	thread_time_param->fd=fd;

	if(thread_time_param!=NULL){
		if(pthread_create(&(thread_time_param->tid), NULL, timestampThread, thread_time_param)){
			syslog(LOG_ERR,"pthread_create() failed for timestamp\n");
			exit(1);
		}
	}
	else{
		syslog(LOG_ERR,"malloc() failed for timestamp thread param\n");
		exit(1);
	}
#endif

	socketServer();

}
