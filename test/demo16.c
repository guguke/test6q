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

void zeroFB(char f)
{
    int i;
    char *p;
    p=gfb;
    for(i=0;i<(256*64/2);i++){
        *p++=f;
    }
}
void draw1(int x,int y,int c)
{
    int offset=0;
    int s;
    char *p;
    char ch;

    offset=y*128;
    offset+=(x/4)*2;
    switch(x%4){
#if 0
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
#endif
        case 0:
          s=4;
          break;
        case 1:
          s=0;
          break;
        case 2:
          offset+=1;
          s=4;
          break;
        default:
          offset+=1;
          s=0;
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
                   draw1(x+(j<<3)+k,y+i,0x0f);
                else 
                   draw1(x+(j<<3)+k,y+i,0x0);
            }
        }
    }

}
void showHZdemo()
{
    char *pdot;
    int i,j;
    pdot = gp16+(15*94*32);
    //pdot = gp16;
    for(i=0;i<4;i++){
        for(j=0;j<16;j++){
            showHZ16(pdot,j<<4,i<<4);
            pdot+=32;
        }
    }
    Fill_BlockP(gfb,0,63,0,63);
}
void lineXY()
{
    int i;
    for(i=0;i<64;i++)draw1(i,i,0x0f);
}
// draw rectangle
void col0()
{
    int i;
    for(i=0;i<64;i++) draw1(0,i,0x0f);
    for(i=0;i<64;i++) draw1(255,i,0x0f);
    for(i=0;i<256;i++) draw1(i,0,0x0f);
    for(i=0;i<256;i++) draw1(i,63,0x0f);
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

    OLED_Init();
    zeroFB(0x0);
    //lineXY();

    //col0();
    //Fill_BlockP(gfb,0,64-1,0,63);

    showHZdemo();

    free(gp12);
    free(gp16);
    free(gp24);

    free(gfb);

    return 0;
}
