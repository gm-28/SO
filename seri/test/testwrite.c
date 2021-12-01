#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

int main() {
    int in_fd,n;
    char *str;
    
    in_fd = open("/dev/seri", O_RDWR);

    if (in_fd < 0 ){
        printf("-Open error\n!!");
        exit(1);
    }
    
    printf("Number o characters:");
    scanf("%d",&n);
    str=(char* )malloc(n*sizeof(char));

    printf("Write something: ");
    scanf("%str",str);

    printf("-Write result:\n");
    printf("-Return write: %d\n",write(in_fd,str,strlen(str)+1));
   
    free(str);
    close(in_fd);
    exit(2);
}
