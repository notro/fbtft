/*
 * Adds a ads7846 device
 *
 * Copyright (C) 2013, Noralf Tronnes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spi/spi.h>
#include <linux/spi/ads7846.h>
#include <asm/irq.h>

#define DRVNAME "ads7846_device"


static unsigned int verbose = 0;
module_param(verbose, uint, 0);
MODULE_PARM_DESC(verbose, "0-2");

static unsigned busnum = 0;
module_param(busnum, uint, 0);
MODULE_PARM_DESC(busnum, "SPI bus number (default=0)");

static unsigned cs = 1;
module_param(cs, uint, 0);
MODULE_PARM_DESC(cs, "SPI chip select (default=1)");

static unsigned speed = 2000000;
module_param(speed, uint, 0);
MODULE_PARM_DESC(speed, "SPI speed (default 2MHz)");

static int mode = SPI_MODE_0;
module_param(mode, int, 0);
MODULE_PARM_DESC(mode, "SPI mode (default: SPI_MODE_0)");

static int irq = 0;
module_param(irq, int, 0);
MODULE_PARM_DESC(mode, "SPI irq. (default: irq=GPIO_IRQ_START+gpio_pendown)");



static unsigned int	model = 7846;
module_param(model, int, 0);
MODULE_PARM_DESC(model, "Touch Controller model: 7843, 7845, 7846, 7873 (default=7846)");

static int gpio_pendown = -1;
module_param(gpio_pendown, int, 0);
MODULE_PARM_DESC(gpio_pendown, "The GPIO used to decide the pendown state (required)");

static unsigned int	x_plate_ohms = 400;
module_param(x_plate_ohms, uint, 0);
MODULE_PARM_DESC(x_plate_ohms, "Used to calculate pressure");

static bool swap_xy = 0;
module_param(swap_xy, bool, 0);
MODULE_PARM_DESC(swap_xy, "Swap x and y axes");

static unsigned int	x_min = 0;
module_param(x_min, uint, 0);
MODULE_PARM_DESC(x_min, "Minimum value for x-axis");

static unsigned int x_max = 4095;
module_param(x_max, uint, 0);
MODULE_PARM_DESC(x_max, "Maximum value for x-axis");

static unsigned int	y_min = 0;
module_param(y_min, uint, 0);
MODULE_PARM_DESC(y_min, "Minimum value for y-axis");

static unsigned int y_max = 4095;
module_param(y_max, uint, 0);
MODULE_PARM_DESC(y_max, "Maximum value for x-axis");

static unsigned int	pressure_min = 0;
module_param(pressure_min, uint, 0);

static unsigned int pressure_max = ~0;
module_param(pressure_max, uint, 0);

static bool keep_vref_on = true;
module_param(keep_vref_on, bool, 0);
MODULE_PARM_DESC(keep_vref_on, "Keep vref on for differential measurements as well (default=true)");

static unsigned int	vref_delay_usecs = 0;
module_param(vref_delay_usecs, int, 0);

static unsigned int	vref_mv = 0;
module_param(vref_mv, int, 0);

static unsigned int	settle_delay_usecs = 0;
module_param(settle_delay_usecs, uint, 0);

static unsigned int	penirq_recheck_delay_usecs = 0;
module_param(penirq_recheck_delay_usecs, uint, 0);

static unsigned int	y_plate_ohms = 0;
module_param(y_plate_ohms, uint, 0);
MODULE_PARM_DESC(y_plate_ohms, "Not used by driver");

static unsigned int	debounce_max = 0;
module_param(debounce_max, uint, 0);
MODULE_PARM_DESC(debounce_max, "Max number of additional readings per sample (0,1,2)");

static unsigned int	debounce_tol = 0;
module_param(debounce_tol, uint, 0);
MODULE_PARM_DESC(debounce_tol, "Tolerance used for filtering");

static unsigned int	debounce_rep = 0;
module_param(debounce_rep, uint, 0);

static unsigned long irq_flags = 0;
module_param(irq_flags, ulong, 0);


static int spi_device_found(struct device *dev, void *data)
{
	struct spi_device *spi = container_of(dev, struct spi_device, dev);

	pr_info(DRVNAME":    %s %s %dkHz %d bits mode=0x%02X\n", spi->modalias, dev_name(dev), spi->max_speed_hz/1000, spi->bits_per_word, spi->mode);

	return 0;
}

static void pr_spi_devices(void)
{
	pr_info(DRVNAME": SPI devices registered:\n");
	bus_for_each_dev(&spi_bus_type, NULL, NULL, spi_device_found);
	pr_info(DRVNAME":\n");
}

static void spidevices_delete(struct spi_master *master, unsigned cs)
{
	struct device *dev;
	char str[32];

	snprintf(str, sizeof(str), "%s.%u", dev_name(&master->dev), cs);

	dev = bus_find_device_by_name(&spi_bus_type, NULL, str);
	if (dev) {
		pr_err(DRVNAME": Deleting %s\n", str);
		device_del(dev);
	}
}

#define pr_pdata(sym)  pr_info(DRVNAME":   "#sym" = %d\n", pdata->sym)

static struct ads7846_platform_data pdata_ads7846_device = { 0, };

static struct spi_board_info spi_ads7846_device = {
	.modalias = "ads7846",
	.platform_data = &pdata_ads7846_device,
};

struct spi_device *ads7846_spi_device = NULL;

static int __init ads7846_device_init(void)
{
	struct spi_master *master;
	struct ads7846_platform_data *pdata = &pdata_ads7846_device;

	if (verbose)
		pr_info("\n\n"DRVNAME": %s()\n", __func__);

	if (gpio_pendown < 0) {
		pr_err(DRVNAME": Argument required: 'gpio_pendown'\n");
		return -EINVAL;
	}

	if (verbose > 1)
		pr_spi_devices(); /* print list of registered SPI devices */

	/* set SPI values */
	spi_ads7846_device.max_speed_hz = speed;
	spi_ads7846_device.bus_num = busnum;
	spi_ads7846_device.chip_select = cs;
	spi_ads7846_device.mode = mode;
	irq = irq ? : (GPIO_IRQ_START + gpio_pendown);
	spi_ads7846_device.irq = irq;

	/* set platform_data values */
	pdata->model = model;
	pdata->vref_delay_usecs = vref_delay_usecs;
	pdata->vref_mv = vref_mv;
	pdata->keep_vref_on = keep_vref_on;
	pdata->swap_xy = swap_xy;
	pdata->settle_delay_usecs = settle_delay_usecs;
	pdata->penirq_recheck_delay_usecs = penirq_recheck_delay_usecs;
	pdata->x_plate_ohms = x_plate_ohms;
	pdata->y_plate_ohms = y_plate_ohms;
	pdata->x_min = x_min;
	pdata->x_max = x_max;
	pdata->y_min = y_min;
	pdata->y_max = y_max;
	pdata->pressure_min = pressure_min;
	pdata->pressure_max = pressure_max;
	pdata->debounce_max = debounce_max;
	pdata->debounce_tol = debounce_tol;
	pdata->debounce_rep = debounce_rep;
	pdata->gpio_pendown = gpio_pendown;
	pdata->irq_flags = irq_flags;

	if (verbose) {
		pr_info(DRVNAME": Settings:\n");
		pr_pdata(model);
		pr_pdata(gpio_pendown);
		pr_pdata(swap_xy);
		pr_pdata(x_min);
		pr_pdata(x_max);
		pr_pdata(y_min);
		pr_pdata(y_max);
		pr_pdata(x_plate_ohms);
		pr_pdata(pressure_min);
		pr_pdata(pressure_max);
		pr_pdata(keep_vref_on);
		pr_pdata(vref_delay_usecs);
		pr_pdata(vref_mv);
		pr_pdata(settle_delay_usecs);
		pr_pdata(penirq_recheck_delay_usecs);
		pr_pdata(y_plate_ohms);
		pr_pdata(debounce_max);
		pr_pdata(debounce_tol);
		pr_pdata(debounce_rep);
	}

	master = spi_busnum_to_master(spi_ads7846_device.bus_num);
	if (!master) {
		pr_err(DRVNAME": spi_busnum_to_master(%d) returned NULL.\n", spi_ads7846_device.bus_num);
		return -EINVAL;
	}

	spidevices_delete(master, spi_ads7846_device.chip_select);      /* make sure it's available */

	ads7846_spi_device = spi_new_device(master, &spi_ads7846_device);
	put_device(&master->dev);
	if (!ads7846_spi_device) {
		pr_err(DRVNAME": spi_new_device() returned NULL\n");
		return -EPERM;
	}

	if (verbose)
		pr_spi_devices();

	return 0;
}

static void __exit ads7846_device_exit(void)
{
	if (verbose)
		pr_info(DRVNAME": %s()\n", __func__);

	if (ads7846_spi_device) {
		device_del(&ads7846_spi_device->dev);
		kfree(ads7846_spi_device);
	}
}

module_init(ads7846_device_init);
module_exit(ads7846_device_exit);

MODULE_DESCRIPTION("Adds a ADS7846 device");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
