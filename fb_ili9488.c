#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>
#include "fbtft.h"
#if defined(CONFIG_ARCH_BCM2709) || defined (CONFIG_ARCH_BCM2708)
#include <mach/platform.h>
#endif

#define BYPASS_GPIOLIB /* Speed up gpio access by directly writing to io address */

#define DRVNAME		"fb_ili9488"
#define WIDTH	    320
#define HEIGHT	    480



#ifdef BYPASS_GPIOLIB
#define GPIOSET(no, ishigh)           \
do {                                  \
	if (ishigh)                   \
		set |= (1 << (no));   \
	else                          \
		reset |= (1 << (no)); \
} while (0)
#endif

static int init_display_ili9488(struct fbtft_par *par)
{

	fbtft_par_dbg(DEBUG_INIT_DISPLAY, par, "%s()\n", __func__);

	par->fbtftops.reset(par);

	if (par->gpio.cs != -1)
		gpio_set_value(par->gpio.cs, 0);  /* Activate chip */

	/* Interface Mode Control */
	write_reg(par, 0xe9, 0x20);

	/* Sleep OUT  */
	write_reg(par, 0x11);

	mdelay(100);

	/* Pixel Format */
	write_reg(par, 0x36, 0x48);

	/* 16 bit pixels  */
	write_reg(par, 0x3a, 0x05);

	/* Display mode */
	write_reg(par, 0x13);

	/* Gamma control  */
	write_reg(par, 0xc0, 0x08, 0x01);

	/* CABC control 2 */
	write_reg(par, 0xc8, 0xb0);


	/* DISPON - Display On */
	write_reg(par, 0x29);

	return 0;
}

/*
 * Option to use direct gpio access to speed up display refresh
 */
static int fbtft_ili9488_write_gpio8_wr(struct fbtft_par *par, void *buf, size_t len)
{
	u8 data;
	static u8 prev_data = 0xff;
#ifdef BYPASS_GPIOLIB
	unsigned int set = 0;
	unsigned int reset = 0;
#else
	int i;
#endif

	fbtft_par_dbg_hex(DEBUG_WRITE, par, par->info->device, u8, buf, len,
		"%s(len=%d): ", __func__, len);
#ifdef BYPASS_GPIOLIB
	while (len--) {
		data = *(u8 *) buf;

		if (data != prev_data)
		{
			/* Set data */
			GPIOSET(par->gpio.db[0], (data&0x01));
			GPIOSET(par->gpio.db[1], (data&0x02));
			GPIOSET(par->gpio.db[2], (data&0x04));
			GPIOSET(par->gpio.db[3], (data&0x08));
			GPIOSET(par->gpio.db[4], (data&0x10));
			GPIOSET(par->gpio.db[5], (data&0x20));
			GPIOSET(par->gpio.db[6], (data&0x40));
			GPIOSET(par->gpio.db[7], (data&0x80));
			writel(set, __io_address(GPIO_BASE+0x1C));
			writel(reset, __io_address(GPIO_BASE+0x28));
		}

		/* Pulse /WR low */
		writel((1<<par->gpio.wr),  __io_address(GPIO_BASE+0x28));
		writel(0,  __io_address(GPIO_BASE+0x28)); /* used as a delay */
		writel((1<<par->gpio.wr),  __io_address(GPIO_BASE+0x1C));

		set = 0;
		reset = 0;
		prev_data = data;
		buf++;

	}
#else
	while (len--) {
		data = *(u8 *) buf;

		/* Start writing by pulling down /WR */
		gpio_set_value(par->gpio.wr, 0);

		/* Set data */
#ifndef DO_NOT_OPTIMIZE_FBTFT_WRITE_GPIO
		if (data == prev_data) {
			gpio_set_value(par->gpio.wr, 0); /* used as delay */
		} else {
			for (i = 0; i < 8; i++) {
				if ((data & 1) != (prev_data & 1))
					gpio_set_value(par->gpio.db[i],
								data & 1);
				data >>= 1;
				prev_data >>= 1;
			}
		}
#else
		for (i = 0; i < 8; i++) {
			gpio_set_value(par->gpio.db[i], data & 1);
			data >>= 1;
		}
#endif

		/* Pullup /WR */
		gpio_set_value(par->gpio.wr, 1);

#ifndef DO_NOT_OPTIMIZE_FBTFT_WRITE_GPIO
		prev_data = *(u8 *) buf;
#endif
		buf++;
	}
#endif

	return 0;
}

static struct fbtft_display display = {
	.regwidth = 8,
	.width = WIDTH,
	.height = HEIGHT,
	/*.init_sequence = default_init_sequence,*/
	.fbtftops = {
		.init_display = init_display_ili9488,
		.write = fbtft_ili9488_write_gpio8_wr,
	},
};

FBTFT_REGISTER_DRIVER(DRVNAME, "mcufriend,ili9488", &display);

MODULE_ALIAS("platform:" DRVNAME);
MODULE_ALIAS("platform:ili9488");

MODULE_DESCRIPTION("ILI9488 TFT Driver");
MODULE_AUTHOR("Sachin Surendran");
MODULE_LICENSE("GPL");

