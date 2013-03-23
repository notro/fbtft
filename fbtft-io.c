
#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include "fbtft.h"

int fbtft_write_spi(struct fbtft_par *par, void *buf, size_t len)
{
	fbtft_fbtft_dev_dbg(DEBUG_WRITE, par, par->info->device, "%s(len=%d)\n", __func__, len);
	if (!par->spi) {
		dev_err(par->info->device, "%s: par->spi is unexpectedly NULL\n", __func__);
		return -1;
	}
	return spi_write(par->spi, buf, len);
}

int fbtft_write_gpio8(struct fbtft_par *par, void *buf, size_t len)
{
	dev_err(par->info->device, "%s: function not implemented\n", __func__);
	return -1;
}

int fbtft_write_gpio16(struct fbtft_par *par, void *buf, size_t len)
{
	dev_err(par->info->device, "%s: function not implemented\n", __func__);
	return -1;
}
