#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <syslog.h>



int main(int argc, char *argv[]) {

    if(argc != 3){
        syslog(LOG_ERR, "Usage: %s <writefile> <writestr>\n", argv[0]);
        return 1;
    }

    char *writefile = argv[1];
    char *writestr = argv[2];

    char *dir = strdup(writefile);
    char *last_slash = strrchr(dir,'/');
    if (last_slash != NULL) {
        *last_slash = '\0';
        mkdir(dir, 0777);
    }

    int fd = open(writefile, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd == -1) {
        syslog(LOG_ERR,"Error: Could not open file for writing");
        return 1;
    }

    int bytes_written = write(fd, writestr, strlen(writestr));
    if (bytes_written == -1) {
        syslog(LOG_ERR,"Error: Could not write to file");
        close(fd);
        return 1;
    }
    close(fd);


    return 0;
}