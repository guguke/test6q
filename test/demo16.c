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
static char *gfb;// 256x64   

void zeroFB()
{
    int i;
    char *p;
    p=gfb;
    for(i=0;i<(256x64/2);i++){
        *p++=0;
    }
}
void draw1(int x,int y,int c)
{
    int offset=0;
    int s;
    char *p;
    char ch;

    offset=(y-1)*128;
    offset+=(x/4)*2;
    switch(x%4){
        case 0:
          offset+=1;
          s=0;
          break;
        case 1:
          offset+=1;
          s=4;
          break;
        case 2:
          s=0;
          break;
        default:
          s=4;
          break;
    }
    p=gfb + offset;
    ch = *p;
    switch(s){
        case 0:
          c=0x0f & c;
          ch = (ch & 0x0ff)|c;
          break;
        case 4:
          c =(c<<4)&0x0f0;
          ch = (ch & 0x0f0)| c;
          break;
        default:
          break;
    }
    *p=ch;

}

void showHZ16(char *p,int x,int y)
{
    char ch;
    int c1;
    int i,j,k,m;
    for(i=0;i<16;i++){
        for(j=0;j<2;j++){
            ch = *p++;
            for(k=0;k<8;k++){
                m = 7-k;
                c1 = (1<<m)&ch;
                if(c1)
                   draw1(x+((j-1)<<3)+k,y+i,0x0f);
                else 
                   draw1(x+((j-1)<<3)+k,y+i,0x00);
            }
        }
    }

}
void showHZdemo()
{
    char *pdot;
    int i,j;
    pdot = gp16+(15*94*32);
    for(i=0;i<16;i++){
        for(j=0;j<4;j++){
            showHZ16(pdot,i<<4;j<<4);
            pdot+=32;
        }
    }
    Fill_BlockP(gfb,28,28+64-1,0,63);
}

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
    gfb=malloc(40000);

    showHZdemo();

    free(gp12);
    free(gp16);
    free(gp24);

    free(gfb);

    return 0;
}
