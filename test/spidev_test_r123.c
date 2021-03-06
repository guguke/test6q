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
//static int sum123=0x7a1;///   00.01.02   sum0-251
static int sum123=0x7994;///   02.03.04.05   sum4-251

static int sum55=0x7a2;
static int frame0=-1;
static int gi=0;

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

	if(noprint==0){
	//for (ret = 0; ret < ARRAY_SIZE(tx); ret++) {
	for (ret = 0; ret < zz; ret++) {
		//if(ret>5 && ret <zz-6)continue;
		printf(" %02x", rx[ret] & 0x0ff);
	}
	puts("");
	}
	else{/////// print
		if(pi[0]==1){
			printf("\n   head ============");
			for(ret=0;ret<zz;ret++){
				printf(" %02x",rx[ret] & 0x0ff);
			}
			printf("\n");
		}
		sum0=0;
		for(ret=4; ret<zz; ret++){
			sum0+=rx[ret];
		}
		if(sum0==sum123 && pi[0]==1){// head
			printf("\n ================   gi:%d    sn:%08x sn[1]:%08x    sum 0x%x   num.correct:%d  num.zero:%d\n",gi,pi[0],frame0,sum0,n_7a1,n_0);
			n_7a1=1;
			n_0=0;
			sum=sum0;
			frame0=pi[0];
		}
		else{ // not head
			if(sum0==0 || sum0==0x5258 || sum0==0x0f708) n_0++;//       0x55 * (252-4)
			else{/// not zero pkt
				if(sum!=sum0){
					sum=sum0;
					printf("\n     gi:%d  sum 0x%x   num.correct:%d  num.zero:%d\n",gi,sum0,n_7a1,n_0);
					if(sum0!=0 && sum0!=sum123){
						for(ret=0;ret<zz;ret++){
							printf(" %02x",rx[ret] & 0x0ff);
						}
						printf("\n");
					}
				}
				else{
					if((pi[0] & 0xffff0000) != (0xffff0000 & (frame0+0x10000))){
						printf("\n   gi:%d    serial.number error, sn:%08x sn[1]:%08x    sum 0x%x   num.correct:%d  num.zero:%d\n",gi,pi[0],frame0,sum0,n_7a1,n_0);
						if(sum0!=0 && sum0!=sum123){
							for(ret=0;ret<zz;ret++){
								printf(" %02x",rx[ret] & 0x0ff);
							}
							printf("\n");
						}
					}
					else n_7a1++;
				}
				frame0=pi[0];
			}
		}
	}
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
	for(i=0;i<ll;i++,gi++){
		if(delayus!=0) usleep(delayus);
	transfer(fd,i);
		printf("\r rceived %d   num.correct:%d  num.zero:%d",gi+1,n_7a1,n_0);
	}
	printf("\n");

	close(fd);

	return ret;
}
