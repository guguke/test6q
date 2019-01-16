/* 
 * udpclient.c - A simple UDP client
 * usage: udpclient <host> <port>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <fcntl.h>

#define BUFSIZE 1024

static char *gp12,*gp16,*gp24;

void loaddot()
{
	int fd;
	fd=open("song12.dot",O_RDONLY);
	if(fd!=-1){
	read(fd,gp12,300000);
	close(fd);
    }
	fd=open("song16.dot",O_RDONLY);
	if(fd!=-1){
	read(fd,gp16,300000);
	close(fd);
    }
	fd=open("song24.dot",O_RDONLY);
	if(fd!=-1){
	read(fd,gp24,650000);
	close(fd);
    }

}
int demo16() {
    int i,j,k;
    int r,c;
    char *p;

    gp12=malloc(300000);
    gp16=malloc(300000);
    gp24=malloc(700000);
    loaddot();

    //p = gp16+(15*94*32);
    p = gp16+(15*94*32);
    for(r=0;r<4;r++){
        for(c=0;c<16;c++){
            show16(r,c,p);
            p+=32;
        }
    }

    free(gp12);
    free(gp16);
    free(gp24);

    return 0;
}
