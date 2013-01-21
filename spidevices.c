#define DEBUG
/*
 *
 * Copyright (C) 2013, Noralf Tronnes
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of this archive for
 * more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
//#include <linux/errno.h>
//#include <linux/init.h>
#include <linux/spi/spi.h>

//#include <linux/fbtft.h>
#include "fbtft.h"


#define DRVNAME "spidevices"

struct spi_device *spi_device0;
struct spi_device *spi_device1;


static int spi_device_found(struct device *dev, void *data)
{
	struct spi_device *spi = container_of(dev, struct spi_device, dev);

	pr_debug("  %s %s %dkHz %d bits mode=0x%02X\n", spi->modalias, dev_name(dev), spi->max_speed_hz/1000, spi->bits_per_word, spi->mode);


	return 0;
}


static void pr_spi_devices(void)
{
	pr_debug("SPI devices:\n");
	bus_for_each_dev(&spi_bus_type, NULL, NULL, spi_device_found);
}



static const struct fbtft_gpio adafruit22_gpios[] = {
	{ "reset", 25 },
	{ },
};

static struct fbtft_platform_data adafruit22_pdata = {
	.gpios = adafruit22_gpios,
};

static const struct fbtft_gpio sainsmart18_gpios[] = {
	{ "reset", 24 },
	{ "dc", 23 },
	{ },
};

static struct fbtft_platform_data sainsmart18_pdata = {
	.gpios = sainsmart18_gpios,
};



static struct spi_board_info chips[] = {
	{
		.modalias = "adafruit22fb",
		.max_speed_hz = 32000000,
		.bus_num = 0,
		.chip_select = 0,
		.mode = SPI_MODE_0,
		.platform_data = &adafruit22_pdata,
	}, {
		.modalias = "sainsmart18fb",
		.max_speed_hz = 16000000,
		.bus_num = 0,
		.chip_select = 1,
		.mode = SPI_MODE_0,
		.platform_data = &sainsmart18_pdata,
	}
};

#define SPI_BUS 0

static int __init spidevices_init(void)
{
	struct spi_master *spi_master;
	struct device *dev;
	int ret = 0;

	pr_debug("\n\n"DRVNAME" - init\n");
	pr_spi_devices();


	dev = bus_find_device_by_name(&spi_bus_type, NULL, "spi0.0");
	if (dev) {
		pr_err("Deleting spi0.0\n");
		device_del(dev);
	}

	dev = bus_find_device_by_name(&spi_bus_type, NULL, "spi0.1");
	if (dev) {
		pr_err("Deleting spi0.1\n");
		device_del(dev);
	}


	spi_master = spi_busnum_to_master(SPI_BUS);
	if (!spi_master) {
		pr_err("spi_busnum_to_master(%d) returned NULL\n", SPI_BUS);
		return -1;
	}

	spi_device0 = spi_new_device(spi_master, &chips[0]);
	if (!spi_device0) {
		pr_err("spi_new_device() returned NULL\n");
		ret = -1;
		goto init_out;
	}

	spi_device1 = spi_new_device(spi_master, &chips[1]);
	if (!spi_device1) {
		pr_err("spi_new_device() returned NULL\n");
		ret = -1;
		goto init_out;
	}


	pr_spi_devices();


init_out:
	put_device(&spi_master->dev);

	return ret;
}

static void __exit spidevices_exit(void)
{
	pr_debug(DRVNAME" - exit\n");

	device_del(&spi_device0->dev);
	kfree(spi_device0);

	device_del(&spi_device1->dev);
	kfree(spi_device1);
	
}

/* ------------------------------------------------------------------------- */

module_init(spidevices_init);
module_exit(spidevices_exit);

MODULE_DESCRIPTION("SPI device adder");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
