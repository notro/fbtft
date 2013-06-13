
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include "fbtft.h"

int fbtft_write_spi(struct fbtft_par *par, void *buf, size_t len)
{
	fbtft_dev_dbg_hex(DEBUG_WRITE, par, par->info->device, u8, buf, len, "%s(len=%d): ", __func__, len);

	if (!par->spi) {
		dev_err(par->info->device, "%s: par->spi is unexpectedly NULL\n", __func__);
		return -1;
	}
	return spi_write(par->spi, buf, len);
}
EXPORT_SYMBOL(fbtft_write_spi);

int fbtft_read_spi(struct fbtft_par *par, void *buf, size_t len)
{
	int ret;
	u8 txbuf[32] = { 0, };
	struct spi_transfer	t = {
			.speed_hz = 2000000,
			.rx_buf		= buf,
			.len		= len,
		};
	struct spi_message	m;

	if (!par->spi) {
		dev_err(par->info->device, "%s: par->spi is unexpectedly NULL\n", __func__);
		return -ENODEV;
	}

	if (par->startbyte) {
		if (len > 32) {
			dev_err(par->info->device, "%s: len=%d can't be larger than 32 when using 'startbyte'\n", __func__, len);
			return -EINVAL;
		}
		txbuf[0] = par->startbyte | 0x3;
		t.tx_buf = txbuf;
		fbtft_dev_dbg_hex(DEBUG_READ, par, par->info->device, u8, txbuf, len, "%s(len=%d) txbuf => ", __func__, len);
	}

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	ret = spi_sync(par->spi, &m);
	fbtft_dev_dbg_hex(DEBUG_READ, par, par->info->device, u8, buf, len, "%s(len=%d) buf <= ", __func__, len);

	return ret;
}
EXPORT_SYMBOL(fbtft_read_spi);


#ifdef CONFIG_ARCH_BCM2708

/*  Raspberry Pi  -  writing directly to the registers is 40-50% faster than optimized use of gpiolib  */

#define GPIOSET(no, ishigh)	{ if (ishigh) set|=(1<<no); else reset|=(1<<no); } while(0)

int fbtft_write_gpio8_wr(struct fbtft_par *par, void *buf, size_t len)
{
	unsigned int set=0;
	unsigned int reset=0;
	u8 data;

	fbtft_dev_dbg_hex(DEBUG_WRITE, par, par->info->device, u8, buf, len, "%s(len=%d): ", __func__, len);

	while (len--) {
		data = *(u8 *) buf;
		buf++;

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

		//Pulse /WR low
		writel((1<<par->gpio.wr),  __io_address(GPIO_BASE+0x28));
		writel(0,  __io_address(GPIO_BASE+0x28)); //used as a delay
		writel((1<<par->gpio.wr),  __io_address(GPIO_BASE+0x1C));

		set = 0;
		reset = 0;
	}

	return 0;
}

int fbtft_write_gpio16_wr(struct fbtft_par *par, void *buf, size_t len)
{
	unsigned int set=0;
	unsigned int reset=0;
	u16 data;

	fbtft_dev_dbg_hex(DEBUG_WRITE, par, par->info->device, u8, buf, len, "%s(len=%d): ", __func__, len);

	while (len) {
		len -= 2;
		data = *(u16 *) buf;
		buf +=2;

		/* Set data */
		GPIOSET(par->gpio.db[0],  (data&0x0001));
		GPIOSET(par->gpio.db[1],  (data&0x0002));
		GPIOSET(par->gpio.db[2],  (data&0x0004));
		GPIOSET(par->gpio.db[3],  (data&0x0008));
		GPIOSET(par->gpio.db[4],  (data&0x0010));
		GPIOSET(par->gpio.db[5],  (data&0x0020));
		GPIOSET(par->gpio.db[6],  (data&0x0040));
		GPIOSET(par->gpio.db[7],  (data&0x0080));

		GPIOSET(par->gpio.db[8],  (data&0x0100));
		GPIOSET(par->gpio.db[9],  (data&0x0200));
		GPIOSET(par->gpio.db[10], (data&0x0400));
		GPIOSET(par->gpio.db[11], (data&0x0800));
		GPIOSET(par->gpio.db[12], (data&0x1000));
		GPIOSET(par->gpio.db[13], (data&0x2000));
		GPIOSET(par->gpio.db[14], (data&0x4000));
		GPIOSET(par->gpio.db[15], (data&0x8000));

		writel(set, __io_address(GPIO_BASE+0x1C));
		writel(reset, __io_address(GPIO_BASE+0x28));

		//Pulse /WR low
		writel((1<<par->gpio.wr),  __io_address(GPIO_BASE+0x28));
		writel(0,  __io_address(GPIO_BASE+0x28)); //used as a delay
		writel((1<<par->gpio.wr),  __io_address(GPIO_BASE+0x1C));

		set = 0;
		reset = 0;
	}

	return 0;
}

int fbtft_write_gpio16_wr_latched(struct fbtft_par *par, void *buf, size_t len)
{
	unsigned int set=0;
	unsigned int reset=0;
	u16 data;

	fbtft_dev_dbg_hex(DEBUG_WRITE, par, par->info->device, u8, buf, len, "%s(len=%d): ", __func__, len);

	while (len) {
		len -= 2;
		data = *(u16 *) buf;
		buf +=2;

		/* Low byte */
		GPIOSET(par->gpio.db[0],  (data&0x0001));
		GPIOSET(par->gpio.db[1],  (data&0x0002));
		GPIOSET(par->gpio.db[2],  (data&0x0004));
		GPIOSET(par->gpio.db[3],  (data&0x0008));
		GPIOSET(par->gpio.db[4],  (data&0x0010));
		GPIOSET(par->gpio.db[5],  (data&0x0020));
		GPIOSET(par->gpio.db[6],  (data&0x0040));
		GPIOSET(par->gpio.db[7],  (data&0x0080));
		writel(set, __io_address(GPIO_BASE+0x1C));
		writel(reset, __io_address(GPIO_BASE+0x28));

		//Pulse 'latch' high
		writel((1<<par->gpio.latch),  __io_address(GPIO_BASE+0x1C));
		writel(0,  __io_address(GPIO_BASE+0x28)); //used as a delay
		writel((1<<par->gpio.latch),  __io_address(GPIO_BASE+0x28));

		/* High byte */
		GPIOSET(par->gpio.db[0], (data&0x0100));
		GPIOSET(par->gpio.db[1], (data&0x0200));
		GPIOSET(par->gpio.db[2], (data&0x0400));
		GPIOSET(par->gpio.db[3], (data&0x0800));
		GPIOSET(par->gpio.db[4], (data&0x1000));
		GPIOSET(par->gpio.db[5], (data&0x2000));
		GPIOSET(par->gpio.db[6], (data&0x4000));
		GPIOSET(par->gpio.db[7], (data&0x8000));
		writel(set, __io_address(GPIO_BASE+0x1C));
		writel(reset, __io_address(GPIO_BASE+0x28));

		//Pulse /WR low
		writel((1<<par->gpio.wr),  __io_address(GPIO_BASE+0x28));
		writel(0,  __io_address(GPIO_BASE+0x28)); //used as a delay
		writel((1<<par->gpio.wr),  __io_address(GPIO_BASE+0x1C));

		set = 0;
		reset = 0;
	}

	return 0;
}

#undef GPIOSET

#else

/* Optimized use of gpiolib is twice as fast as no optimization */
/* only one driver can use the optimized version at a time */
int fbtft_write_gpio8_wr(struct fbtft_par *par, void *buf, size_t len)
{
	u8 data;
	int i;
#ifndef DO_NOT_OPTIMIZE_FBTFT_WRITE_GPIO
	static u8 prev_data = 0;
#endif

	fbtft_fbtft_dev_dbg(DEBUG_WRITE, par, par->info->device, "%s(len=%d)\n", __func__, len);

	while (len--) {
		data = *(u8 *) buf;

		/* Start writing by pulling down /WR */
		gpio_set_value(par->gpio.wr, 0);

		/* Set data */
#ifndef DO_NOT_OPTIMIZE_FBTFT_WRITE_GPIO
		if (data == prev_data) {
			gpio_set_value(par->gpio.wr, 0); /* used as delay */
		} else {
			for (i=0;i<8;i++) {
				if ((data & 1) != (prev_data & 1))
					gpio_set_value(par->gpio.db[i], (data & 1));
				data >>= 1;
				prev_data >>= 1;
			}
		}
#else
		for (i=0;i<8;i++) {
			gpio_set_value(par->gpio.db[i], (data & 1));
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

	return 0;
}

int fbtft_write_gpio16_wr(struct fbtft_par *par, void *buf, size_t len)
{
	dev_err(par->info->device, "%s: function not implemented\n", __func__);
	return -1;
}

int fbtft_write_gpio16_wr_latched(struct fbtft_par *par, void *buf, size_t len)
{
	dev_err(par->info->device, "%s: function not implemented\n", __func__);
	return -1;
}

#endif /* CONFIG_ARCH_BCM2708 */

EXPORT_SYMBOL(fbtft_write_gpio8_wr);
EXPORT_SYMBOL(fbtft_write_gpio16_wr);
EXPORT_SYMBOL(fbtft_write_gpio16_wr_latched);
