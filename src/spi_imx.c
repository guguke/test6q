/*
 * Copyright (C) 2004-2007, 2012 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (C) 2008 Juergen Beisert
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation
 * 51 Franklin Street, Fifth Floor
 * Boston, MA  02110-1301, USA.
 */

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_bitbang.h>
#include <linux/types.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>

#include <mach/spi.h>

#define DRIVER_NAME "spi_imx"

#define MXC_CSPIRXDATA		0x00
#define MXC_CSPITXDATA		0x04
#define MXC_CSPICTRL		0x08
#define MXC_CSPIINT		0x0c
#define MXC_RESET		0x1c

#define SPI_IMX2_3_CTRL		0x08
#define SPI_IMX2_3_CTRL_ENABLE		(1 <<  0)
#define SPI_IMX2_3_CTRL_XCH		(1 <<  2)
#define SPI_IMX2_3_CTRL_MODE_MASK	(0xf << 4)
#define SPI_IMX2_3_CTRL_POSTDIV_OFFSET	8
#define SPI_IMX2_3_CTRL_PREDIV_OFFSET	12
#define SPI_IMX2_3_CTRL_CS(cs)		((cs) << 18)
#define SPI_IMX2_3_CTRL_BL_OFFSET	20

#define SPI_IMX2_3_CONFIG	0x0c
#define SPI_IMX2_3_CONFIG_SCLKPHA(cs)	(1 << ((cs) +  0))
#define SPI_IMX2_3_CONFIG_SCLKPOL(cs)	(1 << ((cs) +  4))
#define SPI_IMX2_3_CONFIG_SBBCTRL(cs)	(1 << ((cs) +  8))
#define SPI_IMX2_3_CONFIG_SSBPOL(cs)	(1 << ((cs) + 12))

#define SPI_IMX2_3_INT		0x10
#define SPI_IMX2_3_INT_TEEN		(1 <<  0)
#define SPI_IMX2_3_INT_RREN		(1 <<  3)
#define SPI_IMX2_3_INT_RDREN		(1 <<  4)

#define MX3_CSPISTAT		0x14
#define MX3_CSPISTAT_RR		(1 << 3)

/* generic defines to abstract from the different register layouts */
#define MXC_INT_RR	(1 << 0) /* Receive data ready interrupt */
#define MXC_INT_TE	(1 << 1) /* Transmit FIFO empty interrupt */
#define MXC_INT_RDR	(1 << 2) /* Transmit FIFO empty interrupt */

#define SPI_IMX2_3_STAT		0x18
#define SPI_IMX2_3_STAT_RR		(1 <<  3)
#define SPI_IMX2_3_STAT_RDR		(1 <<  4)
#define SPI_IMX2_3_STAT_TC		(1 <<  7)
#define SPI_IMX2_3_DMAREG		0x14
#define SPI_IMX2_3_TESTREG		0x20
struct spi_imx_config {
	unsigned int speed_hz;
	unsigned int bpw;
	unsigned int mode;
	unsigned int len;
	u8 cs;
};

enum spi_imx_devtype {
	SPI_IMX_VER_IMX1,
	SPI_IMX_VER_0_0,
	SPI_IMX_VER_0_4,
	SPI_IMX_VER_0_5,
	SPI_IMX_VER_0_7,
	SPI_IMX_VER_2_3,
};

struct spi_imx_data;

struct spi_imx_devtype_data {
	void (*intctrl)(struct spi_imx_data *, int);
	int (*config)(struct spi_imx_data *, struct spi_imx_config *);
	void (*trigger)(struct spi_imx_data *);
	int (*rx_available)(struct spi_imx_data *);
	void (*reset)(struct spi_imx_data *);
	unsigned int fifosize;
};
static struct hrtimer timer5ms;
static int gnSending=0;
static int gnTimerS=0,gnTimerE=0;
static int gnSent=0;
static int gnRDYint=0;
#define RX_1000 0x100000
#define RX_FFF (RX_1000-1)
struct spi_imx_data {
	struct spi_bitbang bitbang;

	struct completion xfer_done;
	void *base;
	int irq;
	struct clk *clk;
	unsigned long spi_clk;
	int *chipselect;

	unsigned int count;
	void (*tx)(struct spi_imx_data *);
	void (*rx)(struct spi_imx_data *);
	void *rx_buf;
	const void *tx_buf;
	unsigned int txfifo; /* number of words pushed in tx FIFO */

	struct spi_imx_devtype_data devtype_data;
	// slave mode
	int slave;
	int rxbuf[RX_1000>>2];
	int rxin;
	int rxout;
	int rxcount;
	int disable;
	unsigned int speed_now;   //  same speed_tx ???? 
	unsigned int bpw_now;
	unsigned int len_now;        // len == bpw ,  no use
	unsigned int mode_now;
	int init;
	//int len2send;
	// master mode
	int txbuf[RX_1000>>2];
	int txin;
	int txout;
	int txcount;
	// status
	int firstTX;
	int retcfg;// 0:not config   1:config.ok    2:config.error
	int retsetup;
	int retxfer;
	int pkgSent;// init 0
	int txrcv;
};
#if 0
	void *p;								\
	int c;								\
	if(spi_imx->slave){						\
		c = 0x0ffff & ( 0x10000 + spi_imx->rxin - spi_imx->rxout);			\
		if(c<4080){						\
			p = (void*)spi_imx->rxbuf;					\
			p += spi_imx->rxin;					\
			*(type*)p = val;					\
			spi_imx->rxin += sizeof(type);				\
		}							\
		else{							\
			spi_imx->devtype_data.intctrl(spi_imx, 0);	\
			printk("    rx overflow , slave %d \n",spi_imx->slave);		\
		}							\
	}								\

		printk(" rx_type 1  c:%d\n",c);				\
	printk("   spi.slave:%d   buf_rx_type    rx.read TC flag : 0x%08X   0x%08X    val: 0x%08X\n",spi_imx->slave,reg,reg1,val);					\
	printk("   spi.slave:%d   buf_rx_type    rx.read TC flag : 0x%08X val: 0x%08X   test;%08X\n",spi_imx->slave,reg,val,treg);					\
	printk(KERN_DEBUG"   spi.slave:%d   buf_rx_type stat: 0x%08X val: 0x%08X  test;%08X ,%08X\n",spi_imx->slave,reg,val,treg,dreg);					\
			printk(" rx_type 2  c:%d   0x%08x\n",c,val);			\
	if(reg){								\
		writel(reg,spi_imx->base + SPI_IMX2_3_STAT);			\
	}									\
	
#endif

#define MXC_SPI_BUF_RX(type)						\
static void spi_imx_buf_rx_##type(struct spi_imx_data *spi_imx)		\
{									\
	unsigned int val = readl(spi_imx->base + MXC_CSPIRXDATA);	\
	void *p;								\
	int c,reg,reg1,treg,dreg,creg;								\
	if(spi_imx->slave){						\
	reg=readl(spi_imx->base + SPI_IMX2_3_STAT);	\
	reg1 = reg & SPI_IMX2_3_STAT_TC;	\
	treg=readl(spi_imx->base + SPI_IMX2_3_TESTREG);	\
	dreg=readl(spi_imx->base + SPI_IMX2_3_DMAREG);	\
	creg=readl(spi_imx->base + SPI_IMX2_3_CTRL);	\
						\
		writel(0x80,spi_imx->base + SPI_IMX2_3_STAT);			\
	}								\
	if(spi_imx->slave){						\
		if(spi_imx->disable==0){				\
		c = RX_FFF & ( RX_1000 + spi_imx->rxin - spi_imx->rxout);			\
		if(c<(RX_1000-800)){						\
			p = (void*)spi_imx->rxbuf;					\
			p += spi_imx->rxin;					\
			*(type*)p = val;					\
			spi_imx->rxin = RX_FFF & ( spi_imx->rxin +  sizeof(type));				\
		}							\
		else{							\
			spi_imx->disable = 1;				\
			spi_imx->devtype_data.intctrl(spi_imx, 0);	\
			spi_imx->rxin = 0;							\
			spi_imx->rxout = 0;							\
			printk(KERN_DEBUG"    rx overflow , slave %d \n",spi_imx->slave);		\
		}							\
		}							\
	}								\
	else{								\
	if (spi_imx->rx_buf) {						\
		*(type *)spi_imx->rx_buf = val;				\
		spi_imx->rx_buf += sizeof(type);			\
	reg=readl(spi_imx->base + SPI_IMX2_3_STAT);	\
	reg1 = reg & SPI_IMX2_3_STAT_TC;	\
	treg=readl(spi_imx->base + SPI_IMX2_3_TESTREG);	\
	dreg=readl(spi_imx->base + SPI_IMX2_3_DMAREG);	\
	creg=readl(spi_imx->base + SPI_IMX2_3_CTRL);	\
						\
		writel(0x80,spi_imx->base + SPI_IMX2_3_STAT);			\
	}								\
	}												\
}

#define MXC_SPI_BUF_TX(type)						\
static void spi_imx_buf_tx_##type(struct spi_imx_data *spi_imx)		\
{									\
	type val = 0;							\
									\
	if (spi_imx->tx_buf) {						\
		val = *(type *)spi_imx->tx_buf;				\
		spi_imx->tx_buf += sizeof(type);			\
	}								\
									\
	spi_imx->count -= sizeof(type);					\
									\
	writel(val, spi_imx->base + MXC_CSPITXDATA);			\
}

MXC_SPI_BUF_RX(u8)
MXC_SPI_BUF_TX(u8)
MXC_SPI_BUF_RX(u16)
MXC_SPI_BUF_TX(u16)
MXC_SPI_BUF_RX(u32)
MXC_SPI_BUF_TX(u32)

static struct spi_imx_data *gpspi[32];
static int gnspi=0;

/* First entry is reserved, second entry is valid only if SDHC_SPIEN is set
 * (which is currently not the case in this driver)
 */
static int mxc_clkdivs[] = {0, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192,
	256, 384, 512, 768, 1024};

/* MX21, MX27 */
static unsigned int spi_imx_clkdiv_1(unsigned int fin,
		unsigned int fspi)
{
	int i, max;

	if (cpu_is_mx21())
		max = 18;
	else
		max = 16;

	for (i = 2; i < max; i++)
		if (fspi * mxc_clkdivs[i] >= fin)
			return i;

	return max;
}

/* MX1, MX31, MX35, MX51 CSPI */
static unsigned int spi_imx_clkdiv_2(unsigned int fin,
		unsigned int fspi)
{
	int i, div = 4;

	for (i = 0; i < 7; i++) {
		if (fspi * div >= fin)
			return i;
		div <<= 1;
	}

	return 7;
}
#if 0
#define SPI_IMX2_3_CTRL		0x08
#define SPI_IMX2_3_CTRL_ENABLE		(1 <<  0)
#define SPI_IMX2_3_CTRL_XCH		(1 <<  2)
#define SPI_IMX2_3_CTRL_MODE_MASK	(0xf << 4)
#define SPI_IMX2_3_CTRL_POSTDIV_OFFSET	8
#define SPI_IMX2_3_CTRL_PREDIV_OFFSET	12
#define SPI_IMX2_3_CTRL_CS(cs)		((cs) << 18)
#define SPI_IMX2_3_CTRL_BL_OFFSET	20

#define SPI_IMX2_3_CONFIG	0x0c
#define SPI_IMX2_3_CONFIG_SCLKPHA(cs)	(1 << ((cs) +  0))
#define SPI_IMX2_3_CONFIG_SCLKPOL(cs)	(1 << ((cs) +  4))
#define SPI_IMX2_3_CONFIG_SBBCTRL(cs)	(1 << ((cs) +  8))
#define SPI_IMX2_3_CONFIG_SSBPOL(cs)	(1 << ((cs) + 12))

#define SPI_IMX2_3_INT		0x10
#define SPI_IMX2_3_INT_TEEN		(1 <<  0)
#define SPI_IMX2_3_INT_RREN		(1 <<  3)
#define SPI_IMX2_3_INT_RDREN		(1 <<  4)

#define SPI_IMX2_3_STAT		0x18
#define SPI_IMX2_3_STAT_RR		(1 <<  3)
#define SPI_IMX2_3_STAT_TC		(1 <<  7)
#endif
/* MX51 eCSPI */
static unsigned int spi_imx2_3_clkdiv(unsigned int fin, unsigned int fspi)
{
	/*
	 * there are two 4-bit dividers, the pre-divider divides by
	 * $pre, the post-divider by 2^$post
	 */
	unsigned int pre, post;

	if (unlikely(fspi > fin))
		return 0;

	post = fls(fin) - fls(fspi);
	if (fin > fspi << post)
		post++;

	/* now we have: (fin <= fspi << post) with post being minimal */

	post = max(4U, post) - 4;
	if (unlikely(post > 0xf)) {
		pr_err("%s: cannot set clock freq: %u (base freq: %u)\n",
				__func__, fspi, fin);
		return 0xff;
	}

	pre = DIV_ROUND_UP(fin, fspi << post) - 1;

	pr_debug("%s: fin: %u, fspi: %u, post: %u, pre: %u\n",
			__func__, fin, fspi, post, pre);
	return (pre << SPI_IMX2_3_CTRL_PREDIV_OFFSET) |
		(post << SPI_IMX2_3_CTRL_POSTDIV_OFFSET);
}

static void __maybe_unused spi_imx2_3_intctrl(struct spi_imx_data *spi_imx, int enable)
{
	unsigned val = 0;

	if (enable & MXC_INT_TE)
		val |= SPI_IMX2_3_INT_TEEN;

	if (enable & MXC_INT_RR)
		val |= SPI_IMX2_3_INT_RREN;

	if (enable & MXC_INT_RDR)
		val |= SPI_IMX2_3_INT_RDREN;

	writel(val, spi_imx->base + SPI_IMX2_3_INT);
}

static int gpio_set(int level)
{
#if 0
	//struct spi_imx_data *spi_imx = gpspi[1];
	int gpio = 59;//spi_imx->chipselect[spi->chip_select];   gpio(2,27)  , 32+27

	if (gpio >= 0)
		gpio_direction_output(gpio, level);
#endif
	return 0;
}
static void __maybe_unused spi_imx2_3_trigger(struct spi_imx_data *spi_imx)
{
	u32 reg;
	ktime_t ktime;
	unsigned long delay_in_ns=5000000L;

	ktime = ktime_set(0,delay_in_ns);
	gnTimerS++;
	hrtimer_start(&timer5ms,ktime,HRTIMER_MODE_REL);
	gpio_set(0);

	reg = readl(spi_imx->base + SPI_IMX2_3_CTRL);
	reg |= SPI_IMX2_3_CTRL_XCH;
        //printk("   spidev(trigger) ctrl reg: 0x%08X\n",reg);
	writel(reg, spi_imx->base + SPI_IMX2_3_CTRL);
}

static int __maybe_unused spi_imx2_3_config(struct spi_imx_data *spi_imx,
		struct spi_imx_config *config)
{
	u32 ctrl = SPI_IMX2_3_CTRL_ENABLE, cfg = 0;
	//int w=4;
        //ctrl |= 0x00010000;

	/*
	 * The hardware seems to have a race condition when changing modes. The
	 * current assumption is that the selection of the channel arrives
	 * earlier in the hardware than the mode bits when they are written at
	 * the same time.
	 * So set master mode for all channels as we do not support slave mode.
	 */
	//return 0;
	//if(config->cs==0) spi_imx->slave=0;
	//else spi_imx->slave = 1;
        //printk("   func _config           slave : %d %x ***************************************************\n",spi_imx->slave,spi_imx->rxcount);
	//printk("  cs cs : %d ********************** \n",config->cs);
	//if(config->cs==0) ctrl |= SPI_IMX2_3_CTRL_MODE_MASK;
	if(spi_imx->slave==0) ctrl |= SPI_IMX2_3_CTRL_MODE_MASK;

	//printk(KERN_DEBUG"%s  speed_hz:%d\n",__FUNCTION__,config->speed_hz);
	/* set clock speed */
	ctrl |= spi_imx2_3_clkdiv(spi_imx->spi_clk, config->speed_hz << 2 );

	/* set chip select to use */
	ctrl |= SPI_IMX2_3_CTRL_CS(config->cs);
#if 0
	if(config->bpw<=8) w=1;
	else if(config->bpw<=16) w=2;
	else if(config->bpw<=32) w = 4;
	else w = config->bpw & 0x0ff;
#endif
	//printk("func config   len : %d ********************** \n",spi_imx->len2send);
	//ctrl |= ((config->bpw) - 1) << SPI_IMX2_3_CTRL_BL_OFFSET;
	//printk(KERN_DEBUG"%s  config.bpw %d(%x  x8:%d %x \n",__FUNCTION__,config->bpw,config->bpw,8*config->bpw,8*config->bpw);
	ctrl |= ((config->len<<3)-1) << SPI_IMX2_3_CTRL_BL_OFFSET;

	//printk("  cs cs : %d ********************** \n",config->cs);
	// burst 1 
	//if(spi_imx->slave==0)cfg |= SPI_IMX2_3_CONFIG_SBBCTRL(config->cs);

	if (config->mode & SPI_CPHA)
		cfg |= SPI_IMX2_3_CONFIG_SCLKPHA(config->cs);

	if (config->mode & SPI_CPOL)
		cfg |= SPI_IMX2_3_CONFIG_SCLKPOL(config->cs);

	if (config->mode & SPI_CS_HIGH)
		cfg |= SPI_IMX2_3_CONFIG_SSBPOL(config->cs);

        //printk("   spidev ctrl reg: 0x%08X   cfg:0x%08x\n",ctrl,cfg);
	writel(ctrl, spi_imx->base + SPI_IMX2_3_CTRL);
	writel(cfg, spi_imx->base + SPI_IMX2_3_CONFIG);
	writel(0x00100030, spi_imx->base + SPI_IMX2_3_DMAREG);

	if(spi_imx->slave == 1 )
		spi_imx->devtype_data.intctrl(spi_imx, MXC_INT_RR);
	//else 
		//spi_imx->devtype_data.intctrl(spi_imx, MXC_INT_RR | MXC_INT_TE);
	return 0;
}

static int __maybe_unused spi_imx2_3_rx_available(struct spi_imx_data *spi_imx)
{
	return readl(spi_imx->base + SPI_IMX2_3_STAT) & SPI_IMX2_3_STAT_RR;
}

static void __maybe_unused spi_imx2_3_reset(struct spi_imx_data *spi_imx)
{
	/* drain receive buffer */
	while (spi_imx2_3_rx_available(spi_imx))
		readl(spi_imx->base + MXC_CSPIRXDATA);
	//writel(0x0c0,spi_imx->base + SPI_IMX2_3_STAT);
}

#define MX31_INTREG_TEEN	(1 << 0)
#define MX31_INTREG_RREN	(1 << 3)

#define MX31_CSPICTRL_ENABLE	(1 << 0)
#define MX31_CSPICTRL_MASTER	(1 << 1)
#define MX31_CSPICTRL_XCH	(1 << 2)
#define MX31_CSPICTRL_POL	(1 << 4)
#define MX31_CSPICTRL_PHA	(1 << 5)
#define MX31_CSPICTRL_SSCTL	(1 << 6)
#define MX31_CSPICTRL_SSPOL	(1 << 7)
#define MX31_CSPICTRL_BC_SHIFT	8
#define MX35_CSPICTRL_BL_SHIFT	20
#define MX31_CSPICTRL_CS_SHIFT	24
#define MX35_CSPICTRL_CS_SHIFT	12
#define MX31_CSPICTRL_DR_SHIFT	16

#define MX31_CSPISTATUS		0x14
#define MX31_STATUS_RR		(1 << 3)

/* These functions also work for the i.MX35, but be aware that
 * the i.MX35 has a slightly different register layout for bits
 * we do not use here.
 */
static void __maybe_unused mx31_intctrl(struct spi_imx_data *spi_imx, int enable)
{
	unsigned int val = 0;

	if (enable & MXC_INT_TE)
		val |= MX31_INTREG_TEEN;
	if (enable & MXC_INT_RR)
		val |= MX31_INTREG_RREN;

	writel(val, spi_imx->base + MXC_CSPIINT);
}

static void __maybe_unused mx31_trigger(struct spi_imx_data *spi_imx)
{
	unsigned int reg;

	reg = readl(spi_imx->base + MXC_CSPICTRL);
	reg |= MX31_CSPICTRL_XCH;
	writel(reg, spi_imx->base + MXC_CSPICTRL);
}

static int __maybe_unused spi_imx0_4_config(struct spi_imx_data *spi_imx,
		struct spi_imx_config *config)
{
	unsigned int reg = MX31_CSPICTRL_ENABLE | MX31_CSPICTRL_MASTER;
	int cs = spi_imx->chipselect[config->cs];

	reg |= spi_imx_clkdiv_2(spi_imx->spi_clk, config->speed_hz) <<
		MX31_CSPICTRL_DR_SHIFT;

	reg |= (config->bpw - 1) << MX31_CSPICTRL_BC_SHIFT;

	if (config->mode & SPI_CPHA)
		reg |= MX31_CSPICTRL_PHA;
	if (config->mode & SPI_CPOL)
		reg |= MX31_CSPICTRL_POL;
	if (config->mode & SPI_CS_HIGH)
		reg |= MX31_CSPICTRL_SSPOL;
	if (cs < 0)
		reg |= (cs + 32) << MX31_CSPICTRL_CS_SHIFT;

	writel(reg, spi_imx->base + MXC_CSPICTRL);

	return 0;
}

static int __maybe_unused spi_imx0_7_config(struct spi_imx_data *spi_imx,
		struct spi_imx_config *config)
{
	unsigned int reg = MX31_CSPICTRL_ENABLE | MX31_CSPICTRL_MASTER;
	int cs = spi_imx->chipselect[config->cs];

	reg |= spi_imx_clkdiv_2(spi_imx->spi_clk, config->speed_hz) <<
		MX31_CSPICTRL_DR_SHIFT;

	reg |= (config->bpw - 1) << MX35_CSPICTRL_BL_SHIFT;
	reg |= MX31_CSPICTRL_SSCTL;

	if (config->mode & SPI_CPHA)
		reg |= MX31_CSPICTRL_PHA;
	if (config->mode & SPI_CPOL)
		reg |= MX31_CSPICTRL_POL;
	if (config->mode & SPI_CS_HIGH)
		reg |= MX31_CSPICTRL_SSPOL;
	if (cs < 0)
		reg |= (cs + 32) << MX35_CSPICTRL_CS_SHIFT;

	writel(reg, spi_imx->base + MXC_CSPICTRL);

	return 0;
}

static int __maybe_unused mx31_rx_available(struct spi_imx_data *spi_imx)
{
	return readl(spi_imx->base + MX31_CSPISTATUS) & MX31_STATUS_RR;
}

static void __maybe_unused spi_imx0_4_reset(struct spi_imx_data *spi_imx)
{
	/* drain receive buffer */
	while (readl(spi_imx->base + MX3_CSPISTAT) & MX3_CSPISTAT_RR)
		readl(spi_imx->base + MXC_CSPIRXDATA);
}

#define MX27_INTREG_RR		(1 << 4)
#define MX27_INTREG_TEEN	(1 << 9)
#define MX27_INTREG_RREN	(1 << 13)

#define MX27_CSPICTRL_POL	(1 << 5)
#define MX27_CSPICTRL_PHA	(1 << 6)
#define MX27_CSPICTRL_SSPOL	(1 << 8)
#define MX27_CSPICTRL_XCH	(1 << 9)
#define MX27_CSPICTRL_ENABLE	(1 << 10)
#define MX27_CSPICTRL_MASTER	(1 << 11)
#define MX27_CSPICTRL_DR_SHIFT	14
#define MX27_CSPICTRL_CS_SHIFT	19

static void __maybe_unused mx27_intctrl(struct spi_imx_data *spi_imx, int enable)
{
	unsigned int val = 0;

	if (enable & MXC_INT_TE)
		val |= MX27_INTREG_TEEN;
	if (enable & MXC_INT_RR)
		val |= MX27_INTREG_RREN;

	writel(val, spi_imx->base + MXC_CSPIINT);
}

static void __maybe_unused mx27_trigger(struct spi_imx_data *spi_imx)
{
	unsigned int reg;

	reg = readl(spi_imx->base + MXC_CSPICTRL);
	reg |= MX27_CSPICTRL_XCH;
	writel(reg, spi_imx->base + MXC_CSPICTRL);
}

static int __maybe_unused mx27_config(struct spi_imx_data *spi_imx,
		struct spi_imx_config *config)
{
	unsigned int reg = MX27_CSPICTRL_ENABLE | MX27_CSPICTRL_MASTER;
	int cs = spi_imx->chipselect[config->cs];

	reg |= spi_imx_clkdiv_1(spi_imx->spi_clk, config->speed_hz) <<
		MX27_CSPICTRL_DR_SHIFT;
	reg |= config->bpw - 1;

	if (config->mode & SPI_CPHA)
		reg |= MX27_CSPICTRL_PHA;
	if (config->mode & SPI_CPOL)
		reg |= MX27_CSPICTRL_POL;
	if (config->mode & SPI_CS_HIGH)
		reg |= MX27_CSPICTRL_SSPOL;
	if (cs < 0)
		reg |= (cs + 32) << MX27_CSPICTRL_CS_SHIFT;

	writel(reg, spi_imx->base + MXC_CSPICTRL);

	return 0;
}

static int __maybe_unused mx27_rx_available(struct spi_imx_data *spi_imx)
{
	return readl(spi_imx->base + MXC_CSPIINT) & MX27_INTREG_RR;
}

static void __maybe_unused spi_imx0_0_reset(struct spi_imx_data *spi_imx)
{
	writel(1, spi_imx->base + MXC_RESET);
}

#define MX1_INTREG_RR		(1 << 3)
#define MX1_INTREG_TEEN		(1 << 8)
#define MX1_INTREG_RREN		(1 << 11)

#define MX1_CSPICTRL_POL	(1 << 4)
#define MX1_CSPICTRL_PHA	(1 << 5)
#define MX1_CSPICTRL_XCH	(1 << 8)
#define MX1_CSPICTRL_ENABLE	(1 << 9)
#define MX1_CSPICTRL_MASTER	(1 << 10)
#define MX1_CSPICTRL_DR_SHIFT	13

static void __maybe_unused mx1_intctrl(struct spi_imx_data *spi_imx, int enable)
{
	unsigned int val = 0;

	if (enable & MXC_INT_TE)
		val |= MX1_INTREG_TEEN;
	if (enable & MXC_INT_RR)
		val |= MX1_INTREG_RREN;

	writel(val, spi_imx->base + MXC_CSPIINT);
}

static void __maybe_unused mx1_trigger(struct spi_imx_data *spi_imx)
{
	unsigned int reg;

	reg = readl(spi_imx->base + MXC_CSPICTRL);
	reg |= MX1_CSPICTRL_XCH;
	writel(reg, spi_imx->base + MXC_CSPICTRL);
}

static int __maybe_unused mx1_config(struct spi_imx_data *spi_imx,
		struct spi_imx_config *config)
{
	unsigned int reg = MX1_CSPICTRL_ENABLE | MX1_CSPICTRL_MASTER;

	reg |= spi_imx_clkdiv_2(spi_imx->spi_clk, config->speed_hz) <<
		MX1_CSPICTRL_DR_SHIFT;
	reg |= config->bpw - 1;

	if (config->mode & SPI_CPHA)
		reg |= MX1_CSPICTRL_PHA;
	if (config->mode & SPI_CPOL)
		reg |= MX1_CSPICTRL_POL;

	writel(reg, spi_imx->base + MXC_CSPICTRL);

	return 0;
}

static int __maybe_unused mx1_rx_available(struct spi_imx_data *spi_imx)
{
	return readl(spi_imx->base + MXC_CSPIINT) & MX1_INTREG_RR;
}

static void __maybe_unused mx1_reset(struct spi_imx_data *spi_imx)
{
	writel(1, spi_imx->base + MXC_RESET);
}

/*
 * These version numbers are taken from the Freescale driver.  Unfortunately it
 * doesn't support i.MX1, so this entry doesn't match the scheme. :-(
 */
static struct spi_imx_devtype_data spi_imx_devtype_data[] __devinitdata = {
#ifdef CONFIG_SPI_IMX_VER_IMX1
	[SPI_IMX_VER_IMX1] = {
		.intctrl = mx1_intctrl,
		.config = mx1_config,
		.trigger = mx1_trigger,
		.rx_available = mx1_rx_available,
		.reset = mx1_reset,
		.fifosize = 8,
	},
#endif
#ifdef CONFIG_SPI_IMX_VER_0_0
	[SPI_IMX_VER_0_0] = {
		.intctrl = mx27_intctrl,
		.config = mx27_config,
		.trigger = mx27_trigger,
		.rx_available = mx27_rx_available,
		.reset = spi_imx0_0_reset,
		.fifosize = 8,
	},
#endif
#ifdef CONFIG_SPI_IMX_VER_0_4
	[SPI_IMX_VER_0_4] = {
		.intctrl = mx31_intctrl,
		.config = spi_imx0_4_config,
		.trigger = mx31_trigger,
		.rx_available = mx31_rx_available,
		.reset = spi_imx0_4_reset,
		.fifosize = 8,
	},
#endif
#ifdef CONFIG_SPI_IMX_VER_0_7
	[SPI_IMX_VER_0_7] = {
		.intctrl = mx31_intctrl,
		.config = spi_imx0_7_config,
		.trigger = mx31_trigger,
		.rx_available = mx31_rx_available,
		.reset = spi_imx0_4_reset,
		.fifosize = 8,
	},
#endif
#ifdef CONFIG_SPI_IMX_VER_2_3
	[SPI_IMX_VER_2_3] = {
		.intctrl = spi_imx2_3_intctrl,
		.config = spi_imx2_3_config,
		.trigger = spi_imx2_3_trigger,
		.rx_available = spi_imx2_3_rx_available,
		.reset = spi_imx2_3_reset,
		.fifosize = 64,
	},
#endif
};

static void spi_imx_chipselect(struct spi_device *spi, int is_active)
{
	struct spi_imx_data *spi_imx = spi_master_get_devdata(spi->master);
	int gpio = spi_imx->chipselect[spi->chip_select];
	int active = is_active != BITBANG_CS_INACTIVE;
	int dev_is_lowactive = !(spi->mode & SPI_CS_HIGH);

	if (gpio < 0)
		return;

	gpio_set_value(gpio, dev_is_lowactive ^ active);
}

static void spi_imx_push_slave(struct spi_imx_data *spi_imx)
{
	int reg;								
	for(;;){
		reg=readl(spi_imx->base + SPI_IMX2_3_STAT);	
		reg &= 0x40;
		if( reg == 1 ) break;
		writel(0x55aa55aa, spi_imx->base + MXC_CSPITXDATA);			\
	}

}
static void spi_imx_push(struct spi_imx_data *spi_imx)
{
	int i;
	//printk("  func push : txfifo: %d     fifosize: %d \n",   spi_imx->txfifo, spi_imx->devtype_data.fifosize);
	while (spi_imx->txfifo < spi_imx->devtype_data.fifosize) {
	//for (i=0;i<20 && spi_imx->txfifo < spi_imx->devtype_data.fifosize;i++) {
		//if (!spi_imx->count)
		if (spi_imx->count<1)
			break;
		spi_imx->tx(spi_imx);
		spi_imx->txfifo++;
	}

	spi_imx->devtype_data.trigger(spi_imx);
}

static void spi_imx_mypush(struct spi_imx_data *spi_imx)
{
	while (spi_imx->txfifo < spi_imx->devtype_data.fifosize) {
		spi_imx->tx(spi_imx);
		spi_imx->txfifo++;
	}
}
static int blank2rx(struct spi_imx_data *spi_imx)
{
	int reg;
	if(spi_imx->rxin!=spi_imx->rxout) return 0;
	reg=8 & readl(spi_imx->base + SPI_IMX2_3_STAT);
	if(reg) return 0;
	return 1;// blank 
}
static int blank2tx(struct spi_imx_data *spi_imx)
{
	int reg;
	if(spi_imx->txin!=spi_imx->txout) return 0;
	reg=1 & readl(spi_imx->base + SPI_IMX2_3_STAT);
	if(reg==0) return 0;
	return 1;// blank 
}
static irqreturn_t spi_imx_isr_slave(int irq, void *dev_id)
{
	struct spi_imx_data *spi_imx = dev_id;
	int c,reg,reg1;
	int i;

	//printk(KERN_DEBUG"%s  slave:         sp_imx_data->slave : %d ********************** \n",__FUNCTION__,spi_imx->slave);
	//reg=readl(spi_imx->base + SPI_IMX2_3_STAT);
	//reg1=readl(spi_imx->base + SPI_IMX2_3_TESTREG);
	//printk(" isr slave , stat flag : 0x%08X    test.reg: 0x%08X\n",reg,reg1);
	//printk("   isr: %d     spi_imx->count : %d \n",spi_imx->slave,spi_imx->count);
	reg = spi_imx->devtype_data.rx_available(spi_imx);
		//printk("   isr slave: %d     reg_stat: 0x%08X \n",spi_imx->slave,reg);
	while ( reg  ) {
	//if ( reg & 0x10) {
	//if ( reg ) {
		//for(c=0;c<0x20;c++) 
			spi_imx->rx(spi_imx);
		reg = spi_imx->devtype_data.rx_available(spi_imx);
		//printk("   isr slave: %d     reg_stat: 0x%08X \n",spi_imx->slave,reg);
	//while (spi_imx->devtype_data.rx_available(spi_imx)) {
		//printk("   isr: %d     itxfifo_return   spi_imx->txfifo : %d \n",spi_imx->slave,spi_imx->txfifo);
		//spi_imx->rx(spi_imx);
	}
#if 0
	reg=readl(spi_imx->base + SPI_IMX2_3_STAT) & SPI_IMX2_3_STAT_TC;
	printk(" isr TC flag : 0x%08X\n",reg);
	if(reg){
		writel(reg,spi_imx->base + SPI_IMX2_3_STAT);
	}
#endif
			c = RX_FFF & ( RX_1000 + spi_imx->rxin - spi_imx->rxout);			\
			if(c>=spi_imx->txfifo){															\
				complete(&spi_imx->xfer_done);								\
			}																\

		//spi_imx->devtype_data.intctrl( spi_imx, MXC_INT_RR);
		//printk("   isr: %d     itxfifo_return   spi_imx->txfifo : %d \n",spi_imx->slave,spi_imx->txfifo);

	//if( spi_imx->slave == 0 ) spi_imx->devtype_data.intctrl(spi_imx, 0);
	//printk("   isr: %d   complete    done  \n",spi_imx->slave);
	//complete(&spi_imx->xfer_done);

	return IRQ_HANDLED;
}
static int buf2fifo(struct spi_imx_data *spi_imx)
{
	void *p = (void*)spi_imx->txbuf;
	int *pi;
	int nByte=0;
	int c;
	unsigned int s;
	for(;;){
	c = RX_FFF & ( RX_1000 + spi_imx->txin - spi_imx->txout);
	//printk(KERN_DEBUG"%s   len.txbuf: %d   txout: 0x%08x\n",__FUNCTION__,c,spi_imx->txout);
	if(spi_imx->txin == spi_imx->txout) break;
	s= 0x4 & readl(spi_imx->base + SPI_IMX2_3_STAT);// tx.fifo.full
	//printk(KERN_DEBUG"%s   stat.bit2:%d\n",__FUNCTION__,s);
	if( s ) break;
	p = (void*)spi_imx->txbuf;
	p += spi_imx->txout;
	pi = (int *)p;
	writel(*pi, spi_imx->base + MXC_CSPITXDATA);
	spi_imx->txout = (spi_imx->txout + 4) & RX_FFF;
	nByte+=4;
	}
	//printk(KERN_DEBUG"%s    return  txin: 0x%x  txout: 0x%x\n",__FUNCTION__,spi_imx->txin,spi_imx->txout);
	return nByte;
}
static int txReadFifo(struct spi_imx_data *spi_imx)
{
	int n63;
	n63 = ( spi_imx->len_now + 3 ) >> 2;
	while (spi_imx->devtype_data.rx_available(spi_imx)) {
	//if (spi_imx->devtype_data.rx_available(spi_imx)) {
		readl(spi_imx->base + MXC_CSPIRXDATA);
		spi_imx->txrcv++;
		//printk(KERN_DEBUG"%s   txrcv:%d   len:%d  /4:%d\n",__FUNCTION__,spi_imx->txrcv,spi_imx->len_now,n63);
		if(spi_imx->txrcv == n63 ){
			//printk(KERN_DEBUG"%s   txrcv:%d ====   len:%d  /4:%d\n",__FUNCTION__,spi_imx->txrcv,spi_imx->len_now,n63);
			spi_imx->txrcv = 0;
			spi_imx->pkgSent++;
			gnSent++;
		}
	}
	return 0;
}

static irqreturn_t spi_imx_isr_master(int irq, void *dev_id)
{
	struct spi_imx_data *spi_imx = dev_id;
	unsigned int s;
	int nByte;
	u32 ctrl;
	//ktime_t ktime;
	//unsigned long delay_in_ns=5000000L;

	//ktime = ktime_set(0,delay_in_ns);
	//printk(KERN_DEBUG"%s  master.isr           sp_imx_data->slave : %d ********************** \n",__FUNCTION__,spi_imx->slave);

	if(spi_imx->pkgSent==-2) return IRQ_HANDLED;

	if(spi_imx->pkgSent==-1){
		//printk(KERN_DEBUG"%s   pkgSent==-1\n",__FUNCTION__,spi_imx->slave);
		buf2fifo(spi_imx);
		s= 0x1 & readl(spi_imx->base + SPI_IMX2_3_STAT);// tx.fifo.blank
		if( s ) return IRQ_HANDLED;
		printk(KERN_DEBUG"%s   pkgSent: -1 ==> 0\n",__FUNCTION__);
		spi_imx->pkgSent=0;
#if 0
		//ctrl = readl(spi_imx->base + SPI_IMX2_3_CTRL);
		//ctrl &= ~0x00030000;
		//writel(ctrl, spi_imx->base + SPI_IMX2_3_CTRL);
#endif
		spi_imx->devtype_data.trigger(spi_imx);
		return IRQ_HANDLED;
	}
	txReadFifo(spi_imx);
	nByte = buf2fifo(spi_imx);

	//printk(KERN_DEBUG"%s   txrcv: %d  n.buf2fifo:%d\n",__FUNCTION__,spi_imx->txrcv,nByte);
	if(spi_imx->txrcv==0){
		gnSending=0;
		//gnTimerE++;
		//hrtimer_cancel(&timer5ms);
		//printk(KERN_DEBUG"%s   txrcv==0  pkgSent:%d\n",__FUNCTION__,spi_imx->pkgSent);
		s= 0x1 & readl(spi_imx->base + SPI_IMX2_3_STAT);// tx.fifo.blank
		if(s){
			spi_imx->pkgSent=-2;
			spi_imx->devtype_data.intctrl(spi_imx, 0);// disable int
			printk(KERN_DEBUG"%s   txrcv==0 numSent:%d\n",__FUNCTION__,gnSent);
		}
#if 0
		else{
			if(spi_imx->pkgSent==1){
			//printk(KERN_DEBUG"%s   pkgSent: 1 ==> 2\n",__FUNCTION__);
			ctrl = readl(spi_imx->base + SPI_IMX2_3_CTRL);
			//ctrl |= 0x00020000;//low level
			ctrl |= 0x00010000;// falling edge
			writel(ctrl, spi_imx->base + SPI_IMX2_3_CTRL);
			}
			spi_imx->devtype_data.trigger(spi_imx);
		}
#endif
		return IRQ_HANDLED;
	}

	return IRQ_HANDLED;
}
// isr_main
static irqreturn_t spi_imx_isr(int irq, void *dev_id)
{
	struct spi_imx_data *spi_imx = dev_id;
	struct spi_imx_data *pspi;
	int i;

	//printk(KERN_DEBUG"%s  isr.main           sp_imx_data->slave : %d ***** \n",__FUNCTION__,spi_imx->slave);
#if 0
	if(spi_imx->slave) return spi_imx_isr_slave(irq,dev_id);
	else return spi_imx_isr_master(irq,dev_id);
#endif
	for(i=0;i<gnspi;i++){
		pspi=gpspi[i];
		if(pspi->retcfg==0)continue;
		if(pspi->slave) spi_imx_isr_slave(irq,pspi);
		else spi_imx_isr_master(irq,pspi);
	}
	return IRQ_HANDLED;
	
}
#if 0
static int gpio_set(int level)
{
	struct spi_imx_data *spi_imx = gpspi[1];
	int gpio = spi_imx->chipselect[spi->chip_select];

	if (gpio >= 0)
		gpio_direction_output(gpio, level);

	return 0;
}
#endif
enum hrtimer_restart timer5ms_callback(struct hrtimer *timer)
{
	gpio_set(1);
	printk(KERN_DEBUG"%s  timerout start:%d cancel:%d   pkgSent:%d\n",__FUNCTION__,gnTimerS,gnTimerE,gnSent);
	return HRTIMER_NORESTART;
}
static irqreturn_t master_rdy_isr(int irq, void *dev_id)
{
	//ktime_t ktime;
	//unsigned long delay_in_ns=5000000L;

	struct spi_imx_data *spi_imx = dev_id;
	unsigned int s;

	//ktime = ktime_set(0,delay_in_ns);
	//hrtimer_init(&timer5ms,CLOCK_MONOTONIC,HRTIMER_MODE_REL);
	//timer5ms.function = &timer5ms_callback;
	gnTimerE++;
	hrtimer_cancel(&timer5ms);

	gnRDYint++;
	s= 0x1 & readl(spi_imx->base + SPI_IMX2_3_STAT);// tx.fifo.blank
	if(s==0){
		//gnTimerS++;
		//hrtimer_start(&timer5ms,ktime,HRTIMER_MODE_REL);
		//printk(KERN_DEBUG"%s  trigger    \n",__FUNCTION__);
		if(0==gnSending){
			spi_imx->devtype_data.trigger(spi_imx);
			gnSending=1;
		}
	}
	else{
		printk(KERN_DEBUG"%s  txfifo.blank, num.rdy.interrupt:%d  timer.s:%d .e:%d  \n",__FUNCTION__,gnRDYint,gnTimerS,gnTimerE);
		//hrtimer_start(&timer5ms,ktime,HRTIMER_MODE_REL);
	}
	return IRQ_HANDLED;
}
static int sameCFG(struct spi_imx_config *pcfg, struct spi_imx_data *spi_imx)
{
	if(pcfg->speed_hz != spi_imx->speed_now) return 0;
	if(pcfg->bpw != spi_imx->bpw_now ) return 0;
	if( pcfg->mode != spi_imx->mode_now ) return 0;
	if( pcfg->len != spi_imx->len_now ) return 0;

	return 1;
}
static int do_config(struct spi_imx_data *spi_imx,struct spi_imx_config *config)
{
	int ret=0;
	/* Initialize the functions for transfer */
	if (config->bpw <= 8) {
		spi_imx->rx = spi_imx_buf_rx_u8;
		spi_imx->tx = spi_imx_buf_tx_u8;
	} else if (config->bpw <= 16) {
		spi_imx->rx = spi_imx_buf_rx_u16;
		spi_imx->tx = spi_imx_buf_tx_u16;
	} else if (config->bpw <= 32) {
		spi_imx->rx = spi_imx_buf_rx_u32;
		spi_imx->tx = spi_imx_buf_tx_u32;
	} else {
		spi_imx->rx = spi_imx_buf_rx_u32;
		spi_imx->tx = spi_imx_buf_tx_u32;
	} 

	//clk_enable(spi_imx->clk);
	//spi_imx->init = 1;
	spi_imx->speed_now = config->speed_hz;
	spi_imx->bpw_now = config->bpw;
	spi_imx->len_now = config->len;
	spi_imx->mode_now = config->mode;
	spi_imx->firstTX = 1;
	//spi_imx->pkgSent = -2;

	//config->bpw  = config->len;
	spi_imx->devtype_data.config(spi_imx, config);

	return ret;
}
static int spi_imx_setupxfer_master(struct spi_device *spi,
				 struct spi_transfer *t)
{
	struct spi_imx_data *spi_imx = spi_master_get_devdata(spi->master);
	struct spi_imx_config config;
	//printk(KERN_DEBUG"%s   len:%d \n",__FUNCTION__,t->len);

	config.bpw = t ? t->bits_per_word : spi->bits_per_word;
	config.speed_hz  = t ? t->speed_hz : spi->max_speed_hz;
	config.mode = spi->mode;
	config.cs = spi->chip_select;

	if (!config.speed_hz)
		config.speed_hz = spi->max_speed_hz;
	if (!config.bpw)
		config.bpw = spi->bits_per_word;
	if (!config.speed_hz)
		config.speed_hz = spi->max_speed_hz;
	config.len = t->len;

	switch(spi_imx->retcfg){
	case 2:
	case 1:///  config.ok
		if(sameCFG(&config,spi_imx)){
			spi_imx->retcfg=1;
			break;
		}
		else{
			if(blank2tx){// not same,blank
				spi_imx->retcfg=1;// cfg.ok
				do_config(spi_imx,&config);
				break;
			}
			else{// not same, not blank
				spi_imx->retcfg=2;// cfg.error
				break;
			}
		}
		break;
	default: // config.notcfg
		spi_imx->retcfg = 1;
		spi_imx->init = 1;
		clk_enable(spi_imx->clk);
		do_config(spi_imx,&config);
		break;
	}

	return 0;
}
static int spi_imx_setupxfer_slave(struct spi_device *spi, struct spi_transfer *t)
{
	struct spi_imx_data *spi_imx = spi_master_get_devdata(spi->master);
	struct spi_imx_config config;
	//printk(KERN_DEBUG"%s   len:%d \n",__FUNCTION__,t->len);

	config.bpw = t ? t->bits_per_word : spi->bits_per_word;
	config.speed_hz  = t ? t->speed_hz : spi->max_speed_hz;
	config.mode = spi->mode;
	config.cs = spi->chip_select;

	if (!config.speed_hz)
		config.speed_hz = spi->max_speed_hz;
	if (!config.bpw)
		config.bpw = spi->bits_per_word;
	if (!config.speed_hz)
		config.speed_hz = spi->max_speed_hz;
	config.len = t->len;

	switch(spi_imx->retcfg){
	case 2:
	case 1:///  config.ok
		if(sameCFG(&config,spi_imx)){
			spi_imx->retcfg=1;
			break;
		}
		else{
			if(blank2rx){// not same,blank
				spi_imx->retcfg=1;// cfg.ok
				do_config(spi_imx,&config);
				break;
			}
			else{// not same, not blank
				spi_imx->retcfg=2;// cfg.error
				break;
			}
		}
		break;
	default: // config.notcfg
		spi_imx->retcfg = 1;
		spi_imx->init = 1;
		clk_enable(spi_imx->clk);
		do_config(spi_imx,&config);
		break;
	}

	return 0;
}
static int spi_imx_setupxfer(struct spi_device *spi, struct spi_transfer *t)
{
	struct spi_imx_data *spi_imx = spi_master_get_devdata(spi->master);
	if(spi_imx->slave) spi_imx_setupxfer_slave(spi,t);
	else spi_imx_setupxfer_master(spi,t);

	return 0;
}
static int tx2buf(void *p,int len,struct spi_imx_data *spi_imx)
{
	int c,c1;
	void *pdes;

	//printk(KERN_DEBUG"%s     len: %d ======================================\n",__FUNCTION__,len);
	c = RX_FFF & ( RX_1000 + spi_imx->txin - spi_imx->txout);
	if(c>RX_1000-800){
		printk(KERN_DEBUG"%s     txbuf overflow\n",__FUNCTION__);
		return 0;// error overflow
	}
	c1 = spi_imx->txin + len;
	pdes = (void*)spi_imx->txbuf;
	//printk(KERN_DEBUG"%s   buf.len:0x%8x   xin+len:0x%8x   xin:%8x\n",__FUNCTION__,c,c1,spi_imx->txin);
	if(c1<=RX_1000){
		//printk(KERN_DEBUG"%s c1<=  buf.len:0x%x   xin+len:0x%x\n",__FUNCTION__,c,c1);
		memcpy(pdes+spi_imx->txin,p,len);					
	}
	else{
		//printk(KERN_DEBUG"%s else  buf.len:0x%x   xin+len:0x%x\n",__FUNCTION__,c,c1);
		memcpy(pdes+spi_imx->txin,p,RX_1000 - spi_imx->txin);					
		memcpy(pdes, p + RX_1000 - spi_imx->txin ,len - RX_1000 + spi_imx->txin);					
	}
	spi_imx->txin = RX_FFF & (spi_imx->txin + len);
	//printk(KERN_DEBUG"%s   txin: 0x%x      txout: 0x%x\n",__FUNCTION__,spi_imx->txin,spi_imx->txout);
	return len;
}
static int spi_imx_transfer_master(struct spi_device *spi,
				struct spi_transfer *transfer)
{
	int c,c1;
	void *p;
	int *pi;
	struct spi_imx_data *spi_imx = spi_master_get_devdata(spi->master);
	//printk(KERN_DEBUG"%s     len: %d ======================================\n",__FUNCTION__,transfer->len);
	//return transfer->len;

	spi_imx->tx_buf = transfer->tx_buf;
	spi_imx->rx_buf = transfer->rx_buf;
	spi_imx->count = transfer->len;
	spi_imx->txfifo = 0;

	if(spi_imx->retcfg==2) return 0;// config.error
	// retcfg.ok
	if(spi_imx->pkgSent==-2){
		tx2buf(transfer->tx_buf,transfer->len,spi_imx);
		c = RX_FFF & ( RX_1000 + spi_imx->txin - spi_imx->txout);
		//printk(KERN_DEBUG"%s     txbuf.len: %d ======================================\n",__FUNCTION__,c);
		if(c > (transfer->len<<1) ) {
			spi_imx->pkgSent=-1;
			spi_imx->devtype_data.intctrl(spi_imx, MXC_INT_RR | MXC_INT_TE);
			return transfer->len;
		}
		else return transfer->len;
	}
	else{// sending 
		tx2buf(transfer->tx_buf,transfer->len,spi_imx);
		return transfer->len;
	}

	return transfer->len;
}
static int spi_imx_transfer_slave(struct spi_device *spi,
				struct spi_transfer *transfer)
{
	int c,c1;
	void *p;
	int *pi;
	struct spi_imx_data *spi_imx = spi_master_get_devdata(spi->master);
	//printk("    func transfer  len: %d ======================================\n",transfer->len);

	spi_imx->tx_buf = transfer->tx_buf;
	spi_imx->rx_buf = transfer->rx_buf;
	spi_imx->count = transfer->len;
	spi_imx->txfifo = 0;

	if(spi_imx->retcfg==2) return 0;// config.error
	//init_completion(&spi_imx->xfer_done);

	spi_imx->disable = 0;
	if(spi_imx->slave){
		c = RX_FFF & ( RX_1000 + spi_imx->rxin - spi_imx->rxout);
		//printk("   wait 1- c: %d \n",c);
		if(c>=transfer->len){			
#if 1
			p = (void*)spi_imx->rxbuf;					
			p += spi_imx->rxout;
			c1 = spi_imx->rxout + transfer->len;
			pi = (int*)spi_imx->rxbuf;					
			pi += spi_imx->rxout>>2;
			//printk("   wait rxout: %d ,0x%08x\n",spi_imx->rxout,*pi);
			if(c1<RX_1000){
			memcpy(spi_imx->rx_buf,p,transfer->len);					
			}
			else{
			memcpy(spi_imx->rx_buf,p,RX_1000 - spi_imx->rxout);					
			memcpy(spi_imx->rx_buf+RX_1000-spi_imx->rxout,spi_imx->rxbuf,transfer->len - RX_1000 + spi_imx->rxout);					
			}
#endif
			spi_imx->rxout = RX_FFF & (spi_imx->rxout + transfer->len);				
			return transfer->len;
		}
	}
	for(;;){
	init_completion(&spi_imx->xfer_done);
	//printk("   transfer : %d   wait 0      done  \n",spi_imx->slave);
	wait_for_completion_interruptible(&spi_imx->xfer_done);
	//printk("   transfer : %d   wait 1      done  \n",spi_imx->slave);
	if(spi_imx->slave){
		c = RX_FFF & ( RX_1000 + spi_imx->rxin - spi_imx->rxout);
		//printk("   wait  c: %d \n",c);
		if(c<transfer->len)continue;			
#if 1
			p = (void*)spi_imx->rxbuf;					
			p += spi_imx->rxout;
			c1 = spi_imx->rxout + transfer->len;
			pi = (int*)spi_imx->rxbuf;					
			pi += spi_imx->rxout>>2;
			//printk("   wait rxout: %d ,0x%08x\n",spi_imx->rxout,*pi);
			if(c1<RX_1000){
			memcpy(spi_imx->rx_buf,p,transfer->len);					
			}
			else{
			memcpy(spi_imx->rx_buf,p,RX_1000 - spi_imx->rxout);					
			memcpy(spi_imx->rx_buf+RX_1000-spi_imx->rxout,spi_imx->rxbuf,transfer->len - RX_1000 + spi_imx->rxout);					
			}
#endif
			spi_imx->rxout = RX_FFF & (spi_imx->rxout + transfer->len);				
			break;
	}
	}

	return transfer->len;
}
static int spi_imx_transfer(struct spi_device *spi,
				struct spi_transfer *transfer)
{
	struct spi_imx_data *spi_imx = spi_master_get_devdata(spi->master);
	if(spi_imx->slave) return spi_imx_transfer_slave(spi,transfer);
	else return spi_imx_transfer_master(spi,transfer);
}

static int spi_imx_setup(struct spi_device *spi)
{
	struct spi_imx_data *spi_imx = spi_master_get_devdata(spi->master);
	int gpio = spi_imx->chipselect[spi->chip_select];

	dev_dbg(&spi->dev, "%s: mode %d, %u bpw, %d hz\n", __func__,
		 spi->mode, spi->bits_per_word, spi->max_speed_hz);

	if (gpio >= 0)
		gpio_direction_output(gpio, spi->mode & SPI_CS_HIGH ? 0 : 1);

	spi_imx_chipselect(spi, BITBANG_CS_INACTIVE);

	return 0;
}

static void spi_imx_cleanup(struct spi_device *spi)
{
}

static struct platform_device_id spi_imx_devtype[] = {
	{
		.name = "imx1-cspi",
		.driver_data = SPI_IMX_VER_IMX1,
	}, {
		.name = "imx21-cspi",
		.driver_data = SPI_IMX_VER_0_0,
	}, {
		.name = "imx25-cspi",
		.driver_data = SPI_IMX_VER_0_7,
	}, {
		.name = "imx27-cspi",
		.driver_data = SPI_IMX_VER_0_0,
	}, {
		.name = "imx31-cspi",
		.driver_data = SPI_IMX_VER_0_4,
	}, {
		.name = "imx35-cspi",
		.driver_data = SPI_IMX_VER_0_7,
	}, {
		.name = "imx50-cspi",
		.driver_data = SPI_IMX_VER_0_7,
	}, {
		.name = "imx51-cspi",
		.driver_data = SPI_IMX_VER_0_7,
	}, {
		.name = "imx51-ecspi",
		.driver_data = SPI_IMX_VER_2_3,
	}, {
		.name = "imx53-cspi",
		.driver_data = SPI_IMX_VER_0_7,
	}, {
		.name = "imx53-ecspi",
		.driver_data = SPI_IMX_VER_2_3,
	}, {
		.name = "imx6q-ecspi",
		.driver_data = SPI_IMX_VER_2_3,
	}, {
		/* sentinel */
	}
};

static int __devinit spi_imx_probe(struct platform_device *pdev)
{
	struct spi_imx_master *mxc_platform_info;
	struct spi_master *master;
	struct spi_imx_data *spi_imx;
	struct resource *res;
	int i, ret;

	mxc_platform_info = dev_get_platdata(&pdev->dev);
	if (!mxc_platform_info) {
		dev_err(&pdev->dev, "can't get the platform data\n");
		return -EINVAL;
	}

	master = spi_alloc_master(&pdev->dev, sizeof(struct spi_imx_data));
	if (!master)
		return -ENOMEM;

	platform_set_drvdata(pdev, master);

	master->bus_num = pdev->id;
	printk(KERN_DEBUG"%s   spi bus num:%d ===============\n",__FUNCTION__,master->bus_num);
	master->num_chipselect = mxc_platform_info->num_chipselect;

	spi_imx = spi_master_get_devdata(master);
	spi_imx->bitbang.master = spi_master_get(master);
	spi_imx->chipselect = mxc_platform_info->chipselect;
	spi_imx->rxcount=0;
	spi_imx->rxin=0;
	spi_imx->rxout=0;
	if(master->bus_num==1) spi_imx->slave = 1;
	else spi_imx->slave=0;
	spi_imx->disable = 1;
	spi_imx->speed_now = -1;
	spi_imx->bpw_now = -1;
	spi_imx->len_now = -1;
	spi_imx->mode_now = -1;
	spi_imx->init = 0;

	spi_imx->txin=0;
	spi_imx->txout=0;
	spi_imx->txcount=0;
	spi_imx->firstTX=0;// new cfg , first tx 
	spi_imx->retcfg = 0;
	spi_imx->pkgSent = -2;
	spi_imx->txrcv=0;

	for (i = 0; i < master->num_chipselect; i++) {
		if (spi_imx->chipselect[i] < 0)
			continue;
		ret = gpio_request(spi_imx->chipselect[i], DRIVER_NAME);
		if (ret) {
			while (i > 0) {
				i--;
				if (spi_imx->chipselect[i] >= 0)
					gpio_free(spi_imx->chipselect[i]);
			}
			dev_err(&pdev->dev, "can't get cs gpios\n");
			goto out_master_put;
		}
	}

	spi_imx->bitbang.chipselect = spi_imx_chipselect;
	spi_imx->bitbang.setup_transfer = spi_imx_setupxfer;
	spi_imx->bitbang.txrx_bufs = spi_imx_transfer;
	spi_imx->bitbang.master->setup = spi_imx_setup;
	spi_imx->bitbang.master->cleanup = spi_imx_cleanup;
	spi_imx->bitbang.master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH;

	//printk("   probe : %d   init      done  \n",spi_imx->slave);
	init_completion(&spi_imx->xfer_done);

	spi_imx->devtype_data =
		spi_imx_devtype_data[pdev->id_entry->driver_data];

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "can't get platform resource\n");
		ret = -ENOMEM;
		goto out_gpio_free;
	}

	if (!request_mem_region(res->start, resource_size(res), pdev->name)) {
		dev_err(&pdev->dev, "request_mem_region failed\n");
		ret = -EBUSY;
		goto out_gpio_free;
	}

	spi_imx->base = ioremap(res->start, resource_size(res));
	if (!spi_imx->base) {
		ret = -EINVAL;
		goto out_release_mem;
	}

	spi_imx->irq = platform_get_irq(pdev, 0);
	if (spi_imx->irq < 0) {
		ret = -EINVAL;
		goto out_iounmap;
	}

	ret = request_irq(spi_imx->irq, spi_imx_isr, 0, DRIVER_NAME, spi_imx);
	if (ret) {
		dev_err(&pdev->dev, "can't get irq%d: %d\n", spi_imx->irq, ret);
		goto out_iounmap;
	}
	gpspi[gnspi++]=spi_imx;
	printk(KERN_DEBUG"%s add gnspi: %d\n",__FUNCTION__,gnspi);

	ret = gpio_request_one(mxc_platform_info->rdy_gpio, GPIOF_IN, "spi.master.rdy");
	if (ret) {
		printk(KERN_DEBUG"%s request.rdy.gpio.error\n",__FUNCTION__);
	}
	else {
	ret = request_irq(gpio_to_irq(mxc_platform_info->rdy_gpio), master_rdy_isr,
				 //IRQF_TRIGGER_FALLING,//////////////////////////////////////////// | IRQF_TRIGGER_RISING,
				 IRQF_TRIGGER_RISING,
				 "spi.rdy", spi_imx);
		if (ret) {
			printk(KERN_DEBUG"%s request.rdy.gpio.irq.error\n",__FUNCTION__);
		}
	}
	spi_imx->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(spi_imx->clk)) {
		dev_err(&pdev->dev, "unable to get clock\n");
		ret = PTR_ERR(spi_imx->clk);
		goto out_free_irq;
	}

	clk_enable(spi_imx->clk);
	spi_imx->spi_clk = clk_get_rate(spi_imx->clk);

	spi_imx->devtype_data.reset(spi_imx);

	spi_imx->devtype_data.intctrl(spi_imx, 0);
	ret = spi_bitbang_start(&spi_imx->bitbang);
	if (ret) {
		dev_err(&pdev->dev, "bitbang start failed with %d\n", ret);
		goto out_clk_put;
	}
	clk_disable(spi_imx->clk);

	dev_info(&pdev->dev, "probed\n");

	return ret;

out_clk_put:
	clk_disable(spi_imx->clk);
	clk_put(spi_imx->clk);
out_free_irq:
	free_irq(spi_imx->irq, spi_imx);
out_iounmap:
	iounmap(spi_imx->base);
out_release_mem:
	release_mem_region(res->start, resource_size(res));
out_gpio_free:
	for (i = 0; i < master->num_chipselect; i++)
		if (spi_imx->chipselect[i] >= 0)
			gpio_free(spi_imx->chipselect[i]);
out_master_put:
	spi_master_put(master);
	kfree(master);
	platform_set_drvdata(pdev, NULL);
	return ret;
}

static int __devexit spi_imx_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	struct spi_imx_data *spi_imx = spi_master_get_devdata(master);
	int i;

	spi_bitbang_stop(&spi_imx->bitbang);
	clk_enable(spi_imx->clk);
	writel(0, spi_imx->base + MXC_CSPICTRL);
	clk_disable(spi_imx->clk);
	clk_put(spi_imx->clk);
	free_irq(spi_imx->irq, spi_imx);
	iounmap(spi_imx->base);

	for (i = 0; i < master->num_chipselect; i++)
		if (spi_imx->chipselect[i] >= 0)
			gpio_free(spi_imx->chipselect[i]);

	spi_master_put(master);

	release_mem_region(res->start, resource_size(res));

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver spi_imx_driver = {
	.driver = {
		   .name = DRIVER_NAME,
		   .owner = THIS_MODULE,
		   },
	.id_table = spi_imx_devtype,
	.probe = spi_imx_probe,
	.remove = __devexit_p(spi_imx_remove),
};

static int __init spi_imx_init(void)
{
	//ktime_t ktime;
	//unsigned long delay_in_ns=5000000L;

	//ktime = ktime_set(0,delay_in_ns);
	hrtimer_init(&timer5ms,CLOCK_MONOTONIC,HRTIMER_MODE_REL);
	timer5ms.function = &timer5ms_callback;
	printk(KERN_DEBUG"%s init hrtimer \n",__FUNCTION__);

	return platform_driver_register(&spi_imx_driver);
}

static void __exit spi_imx_exit(void)
{
	platform_driver_unregister(&spi_imx_driver);
}

subsys_initcall(spi_imx_init);
module_exit(spi_imx_exit);

MODULE_DESCRIPTION("SPI Master Controller driver");
MODULE_AUTHOR("Sascha Hauer, Pengutronix");
MODULE_LICENSE("GPL");
