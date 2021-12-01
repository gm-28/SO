#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#define BUF_SIZE 32

int main() {
    int in_fd;
    char str[]="Hello ";

    in_fd = open("/dev/serp", O_RDWR);
    
     if (in_fd < 0 ){
        printf("-Open error\n!!");
        exit(1);
    }

    printf("-Write result:\n");
    printf("-Return write: %d\n",write(in_fd,str,strlen(str)+1));

    close(in_fd);
    exit(2);
}
