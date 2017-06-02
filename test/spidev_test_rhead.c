/*
 * SPI testing utility (using spidev driver)
 *
 * Copyright (c) 2007  MontaVista Software, Inc.
 * Copyright (c) 2007  Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 * Cross-compile with cross-gcc -I/path/to/cross-kernel/include
 */

#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static void pabort(const char *s)
{
	perror(s);
	abort();
}

#define PKT_HEAD 0x0fffff0f
//// (252-8)* 0x0f
#define SUM0F252 0x0e4c
static int gerr252[300];
static int gnbuf63[200];
static int gnLen63=0;
static int gnzLen=0;// num char , 
static int gsumZero=0;  // sum char 
static int gnHead=-1;
static int gsn1=0;
static int gsn0=0;

static const char *device = "/dev/spidev1.1";
static uint8_t mode;
static uint8_t bits = 32;
static uint32_t speed = 2000000;
static uint32_t zz = 252;// tx size
static uint32_t ll = 1;// tx size
static uint16_t delay=0;
static uint32_t delayus=0;   //usec
static int noprint=1;
static int sum=-1;
static int n_7a1=0; // sum 0,1,2,... 3e
static int n_0=0; // sum 0,1,2,... 3e
static int n_err=0;
static int n_e1=0;
static int n_ok=0;

static void pCount(int n)
{
	printf("\r  sn: 0x%x -- 0x%x(%d)(%d)   correct:%d zero:%d err:%d e1:%d",gsn1,gsn0,gsn0,gsn0-gsn1,n_ok,n_0,n_err,n_e1);
	if(n>0) printf("\n");
}
static void searchHead()
{
	gnHead=-1;
	int i;
	for(i=0;i<gnLen63;i++){
		if(PKT_HEAD != gnbuf63[i] ) continue;
		gnHead = i;
		break;
	}
}
static void move2buf63(int *pi)
{
	int i;
	for(i=0;i<63;i++)gnbuf63[gnLen63++]=pi[i];
}
static void pErr252()
{
	int i;
	printf("\n  error packet ( sum: 0x%x len:%d ) ::: ",gsumZero,gnzLen);
	for(i=0;i<252;i++) printf("%02x ",0x0ff & gerr252[i]);
	printf("\n");
}
static void printZ()
{
	if(gnzLen<=0) return;
	if(gnzLen!=252){
		n_err++;
		pErr252();
		pCount(1);
	}
	else{
	if(gsumZero==0){
		n_0++;
		pCount(0);
	}
	else{
		n_err++;
		pErr252();
		pCount(1);
	}
	}
	gsumZero = 0;
	gnzLen = 0;
	
}
// 
static void countZ()
{
	char *p=(char*)gnbuf63;
	int i,j,k;

	for(i=0;i<gnLen63;i++){
		if(gnbuf63[i]==PKT_HEAD){
			printZ();
			if(i!=0){
				for(k=0,j=i;j<gnLen63;j++,k++) gnbuf63[k]=gnbuf63[j];
				gnLen63 -= i;
			}
			break;
		}
		else{
			gsumZero += 0x0ff & p[(i<<2)];
			gsumZero += 0x0ff & p[(i<<2)+1];
			gsumZero += 0x0ff & p[(i<<2)+2];
			gsumZero += 0x0ff & p[(i<<2)+3];
			gerr252[gnzLen++] = p[(i<<2)];
			gerr252[gnzLen++] = p[(i<<2)+1];
			gerr252[gnzLen++] = p[(i<<2)+2];
			gerr252[gnzLen++] = p[(i<<2)+3];
			if(gnzLen>=252) printZ();
		}
	}
	if(gnLen63>0 && gnbuf63[0]!=PKT_HEAD){
		gnLen63 = 0;
	}
}
static void ppkt()
{
	char *p=(char*)gnbuf63;
	int i;
	printf("\n");
	for(i=0;i<252;i++)printf("%02x ",*p++);
	printf("\n");

}
static void count1()
{
	char *p=(char*)gnbuf63;
	int i,j,k;
	int sum=0;
	
	if(gnLen63<63) return;
	if(gnbuf63[0]!=PKT_HEAD) return;

	gsn1 = gsn0;
	gsn0 = gnbuf63[1];
	for(i=8;i<252;i++)sum+= 0x0ff & p[i];
	if(sum!=SUM0F252){
		ppkt();
		printf("\n sum.err: 0x%x\n",sum);
		n_err++;
		pCount(1);
	}
	else{  // checksum ok
		if(gnbuf63[1]==0){
			pCount(1);
			n_ok=1;
			n_err=0;
			n_0 = 0;
			n_e1 = 0;
			printf("============================================================\n");
			pCount(0);
		}
		else if(gsn0!=gsn1+1){
			n_e1++;//err++;
			printf("\nsn+1.err:\n");
			pCount(1);
		}
		else{
			n_ok++;
			pCount(0);
		}
	}
	for(k=0,j=63;j<gnLen63;j++,k++) gnbuf63[k]=gnbuf63[j];
	gnLen63 -= 63;
}
static void count(int *pi)
{
	move2buf63(pi);
	countZ();
	if(gnLen63>=63 && gnbuf63[0]==PKT_HEAD) count1();
}

static void transfer(int fd,int vStart)
{
	int ret;
	int ntx[1000];
	uint8_t *tx=(uint8_t *)ntx;//
	int i;
	int *pi;
	int sum0=0;
#if 0
 = {
		0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF,
		0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF,
		0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF,
		0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF,
		0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF,
		0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF,
		0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF,
		0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF,
		0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF,
		0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF,
		0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF,
		0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF,
//		0x40, 0x00, 0x00, 0x00, 0x00, 0x95,
//		0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
//		0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
//		0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
//		0xDE, 0xAD, 0xBE, 0xEF, 0xBA, 0xAD,
//		0xF0, 0x0D,
	};
#endif
	//uint8_t rx[ARRAY_SIZE(tx)] = {0, };
	uint8_t rx[1000];
	struct spi_ioc_transfer tr = {
		.tx_buf = (unsigned long)tx,
		.rx_buf = (unsigned long)rx,
		.len = zz,//ARRAY_SIZE(tx),
		.delay_usecs = delay,
		.speed_hz = speed,
		.bits_per_word = bits,
	};
	pi = (int*)rx;
	for(i=0;i<256;i++){
		ntx[i]=vStart+i;
		//rx[i]=0x0f;
		pi[i]=0xdeadbeef;
	}

	ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
	if (ret < 1)
		pabort("can't send spi message");

	count(pi);
#if 0
	if(noprint==0){
	//for (ret = 0; ret < ARRAY_SIZE(tx); ret++) {
	for (ret = 0; ret < zz; ret++) {
		//if(ret>5 && ret <zz-6)continue;
		printf(" %02x", rx[ret] & 0x0ff);
	}
	puts("");
	}
	else{/////// print
		sum0=0;
		for(ret=0; ret<zz; ret++){
			sum0+=rx[ret];
		}
		if(sum0==0x7a1) n_7a1++;
		if(sum0==0) n_0++;
		if(sum!=sum0){
			sum=sum0;
			printf("\n sum 0x%x   num.correct:%d  num.zero:%d\n",sum0,n_7a1,n_0);
			if(sum0!=0 && sum0!=0x7a1){
			for(ret=0;ret<zz;ret++){
				printf(" %02x",rx[ret] & 0x0ff);
			}
			printf("\n");
			}
		}
	}
#endif
}

static void print_usage(const char *prog)
{
	printf("Usage: %s [-DsbdlHOLC3]\n", prog);
	puts("  -D --device   device to use (default /dev/spidev1.1)\n"
	     "  -s --speed    max speed (Hz)\n"
	     "  -d --delay    delay (usec)\n"
	     "  -b --bpw      bits per word \n"
	     "  -l --loop     loopback\n"
	     "  -z --size     tx size bytes(default:4)\n"
	     "  -H --cpha     clock phase\n"
	     "  -O --cpol     clock polarity\n"
	     "  -L --lsb      least significant bit first\n"
	     "  -C --cs-high  chip select active high\n"
	     "  -3 --3wire    SI/SO signals shared\n");
	exit(1);
}

static void parse_opts(int argc, char *argv[])
{
	while (1) {
		static const struct option lopts[] = {
			{ "device",  1, 0, 'D' },
			{ "speed",   1, 0, 's' },
			{ "delay",   1, 0, 'd' },
			{ "bpw",     1, 0, 'b' },
			{ "loop",    0, 0, 'l' },
			{ "cpha",    0, 0, 'H' },
			{ "cpol",    0, 0, 'O' },
			{ "lsb",     0, 0, 'L' },
			{ "cs-high", 0, 0, 'C' },
			{ "3wire",   0, 0, '3' },
			{ "no-cs",   0, 0, 'N' },
			{ "ready",   0, 0, 'R' },
			{ "size",     1, 0, 'z' },
			{ "times",     1, 0, 't' },
			{ NULL, 0, 0, 0 },
		};
		int c;

		c = getopt_long(argc, argv, "D:s:d:b:z:t:lHOLC3NRv", lopts, NULL);

		if (c == -1)
			break;

		switch (c) {
		case 'v':
			noprint=0;
			break;
		case 'D':
			device = optarg;
			break;
		case 'z':
			zz = atoi(optarg);
			if(zz>950)zz=900;
			break;
		case 's':
			//zz = atoi(optarg);
			speed = atoi(optarg);
			break;
		case 't':
			ll = atoi(optarg);
			//delay = atoi(optarg);
			break;
		case 'd':
			//ll = atoi(optarg);
			delayus = atoi(optarg);
			break;
		case 'b':
			bits = atoi(optarg);
			break;
		case 'l':
			//ll = atoi(optarg);
			
			mode |= SPI_LOOP;
			break;
		case 'H':
			mode |= SPI_CPHA;
			break;
		case 'O':
			mode |= SPI_CPOL;
			break;
		case 'L':
			mode |= SPI_LSB_FIRST;
			break;
		case 'C':
			mode |= SPI_CS_HIGH;
			break;
		case '3':
			mode |= SPI_3WIRE;
			break;
		case 'N':
			mode |= SPI_NO_CS;
			break;
		case 'R':
			mode |= SPI_READY;
			break;
		default:
			print_usage(argv[0]);
			break;
		}
	}
}

int main(int argc, char *argv[])
{
	int ret = 0;
	int fd;
	int i;

	parse_opts(argc, argv);

	fd = open(device, O_RDWR);
	if (fd < 0)
		pabort("can't open device");

	/*
	 * spi mode
	 */
	ret = ioctl(fd, SPI_IOC_WR_MODE, &mode);
	if (ret == -1)
		pabort("can't set spi mode");

	ret = ioctl(fd, SPI_IOC_RD_MODE, &mode);
	if (ret == -1)
		pabort("can't get spi mode");

	/*
	 * bits per word
	 */
	ret = ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
	if (ret == -1)
		pabort("can't set bits per word");

	ret = ioctl(fd, SPI_IOC_RD_BITS_PER_WORD, &bits);
	if (ret == -1)
		pabort("can't get bits per word");

	/*
	 * max speed hz
	 */
	ret = ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
	if (ret == -1)
		pabort("can't set max speed hz");

	ret = ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &speed);
	if (ret == -1)
		pabort("can't get max speed hz");

	printf("spi mode: %d\n", mode);
	printf("bits per word: %d\n", bits);
	printf("max speed: %d Hz (%d KHz)\n", speed, speed/1000);

	printf("\n");
	for(i=0;i<ll;i++){
		if(delayus!=0) usleep(delayus);
		//printf("\r wait  loop(1..... : %d   num.correct:%d  num.zero:%d",i+1,n_7a1,n_0);
	transfer(fd,i);
	}
	printf("\n");

	close(fd);

	return ret;
}
