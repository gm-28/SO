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
    char buf[BUF_SIZE];

    in_fd = open("/dev/seri", O_RDWR);
    if (in_fd < 0 ){
        printf("-Open error\n!!");
        exit(1);
    }
    printf("Do ctr+c\n");
    printf("-Read\n");
    printf("-Return read: %d\n",read(in_fd,buf,BUF_SIZE));
   
    close(in_fd);
    exit(2);
}
