/*
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

#include "fbtft.h"

#define DRVNAME "fbtft_device"

#define MAX_GPIOS 32

struct spi_device *spi_device = NULL;
struct platform_device *p_device = NULL;

static char *name = NULL;
module_param(name, charp, 0);
MODULE_PARM_DESC(name, "Devicename (required). name=list => list all supported devices.");

static unsigned rotate = 0;
module_param(rotate, uint, 0);
MODULE_PARM_DESC(rotate, "Rotate display 0=normal, 1=clockwise, 2=upside down, 3=counterclockwise (not supported by all drivers)");

static unsigned busnum = 0;
module_param(busnum, uint, 0);
MODULE_PARM_DESC(busnum, "SPI bus number (default=0)");

static unsigned cs = 0;
module_param(cs, uint, 0);
MODULE_PARM_DESC(cs, "SPI chip select (default=0)");

static unsigned speed = 0;
module_param(speed, uint, 0);
MODULE_PARM_DESC(speed, "SPI speed (override device default)");

static int mode = -1;
module_param(mode, int, 0);
MODULE_PARM_DESC(mode, "SPI mode (override device default)");

static char *gpios[MAX_GPIOS] = { NULL, };
static int gpios_num = 0;
module_param_array(gpios, charp, &gpios_num, 0);
MODULE_PARM_DESC(gpios, "List of gpios. Comma separated with the form: reset:23,dc:24 (when overriding the default, all gpios must be specified)");

static unsigned fps = 0;
module_param(fps, uint, 0);
MODULE_PARM_DESC(fps, "Frames per second (override driver default)");

static int txbuflen = 0;
module_param(txbuflen, int, 0);
MODULE_PARM_DESC(txbuflen, "txbuflen (override driver default)");

static bool bgr = false;
module_param(bgr, bool, 0);
MODULE_PARM_DESC(bgr, "Use if Red and Blue color is swapped (supported by some drivers).");

static unsigned startbyte = 0;
module_param(startbyte, uint, 0);
MODULE_PARM_DESC(startbyte, "Sets the Start byte used by some SPI displays.");

static unsigned verbose = 3;
module_param(verbose, uint, 0);
MODULE_PARM_DESC(verbose, "0=silent, 0< show gpios, 1< show devices, 2< show devices before (default=3)");


/* supported SPI displays */
static struct spi_board_info fbtft_device_spi_displays[] = {
	{
		.modalias = "spidev",
		.max_speed_hz = 500000,
		.bus_num = 0,
		.chip_select = 0,
		.mode = SPI_MODE_0,
		.platform_data = &(struct fbtft_platform_data) {  /* this is so the module won't break */
			.gpios = (const struct fbtft_gpio []) {
				{},
			},
		}
	}, {
		.modalias = "flexfb",
		.max_speed_hz = 32000000,
		.mode = SPI_MODE_0,
		.platform_data = &(struct fbtft_platform_data) {
			.gpios = (const struct fbtft_gpio []) {
				{ "reset", 25 },
				{ "dc", 24 },
				{},
			},
		}
	}, {
		.modalias = "ili9341fb",
		.max_speed_hz = 32000000,
		.mode = SPI_MODE_0,
		.platform_data = &(struct fbtft_platform_data) {
			.gpios = (const struct fbtft_gpio []) {
				{ "reset", 23 },
				{ "led", 24 },
				{},
			},
		}
	}, {
		.modalias = "r61505ufb",
		.max_speed_hz = 32000000,
		.mode = SPI_MODE_0,
		.platform_data = &(struct fbtft_platform_data) {
			.gpios = (const struct fbtft_gpio []) {
				{ "reset", 23 },
				{ "led", 24 },
				{ "dc", 7 },
				{},
			},
		}
	}, {
		.modalias = "adafruit22fb",
		.max_speed_hz = 32000000,
		.mode = SPI_MODE_0,
		.platform_data = &(struct fbtft_platform_data) {
			.gpios = (const struct fbtft_gpio []) {
				{ "reset", 25 },
				{ "led", 23 },
				{},
			},
		}
	}, {
		.modalias = "adafruit18fb",
		.max_speed_hz = 4000000,
		.mode = SPI_MODE_0,
		.platform_data = &(struct fbtft_platform_data) {
			.gpios = (const struct fbtft_gpio []) {
				{ "reset", 25 },
				{ "dc", 24 },
				{ "led", 23 },
				{},
			},
		}
	}, {
		.modalias = "adafruit18greenfb",
		.max_speed_hz = 4000000,
		.mode = SPI_MODE_0,
		.platform_data = &(struct fbtft_platform_data) {
			.gpios = (const struct fbtft_gpio []) {
				{ "reset", 25 },
				{ "dc", 24 },
				{ "led", 23 },
				{},
			},
		}
	}, {
		.modalias = "sainsmart18fb",
		.max_speed_hz = 32000000,
		.mode = SPI_MODE_0,
		.platform_data = &(struct fbtft_platform_data) {
			.gpios = (const struct fbtft_gpio []) {
				{ "reset", 25 },
				{ "dc", 24 },
				{},
			},
		}
	}, {
		.modalias = "sainsmart32spifb",
		.max_speed_hz = 16000000,
		.mode = SPI_MODE_0,
		.platform_data = &(struct fbtft_platform_data) {
			.gpios = (const struct fbtft_gpio []) {
				{ "reset", 25 },
				{ "dc", 24 },
				{},
			},
		}
	}, {
		.modalias = "nokia3310fb",
		.max_speed_hz = 4000000,
		.mode = SPI_MODE_0,
		.platform_data = &(struct fbtft_platform_data) {
			.gpios = (const struct fbtft_gpio []) {
				{ "reset", 25 },
				{ "dc", 24 },
				{ "led", 23 },
				{},
			},
		}
	}, {
		.modalias = "itdb28spifb",
		.max_speed_hz = 32000000,
		.mode = SPI_MODE_0,
		.platform_data = &(struct fbtft_platform_data) {
			.gpios = (const struct fbtft_gpio []) {
				{ "reset", 25 },
				{ "dc", 24 },
				{},
			},
		}
	}, {
		.modalias = "hy28afb",
		.max_speed_hz = 32000000,
		.mode = SPI_MODE_3,
		.platform_data = &(struct fbtft_platform_data) {
			.gpios = (const struct fbtft_gpio []) {
				{ "reset", 25 },
				{ "led", 18 },
				{},
			},
		}
	}, {
		.modalias = "ssd1351fb",
		.max_speed_hz = 20000000,
		.mode = SPI_MODE_0,
		.platform_data = &(struct fbtft_platform_data) {
			.gpios = (const struct fbtft_gpio []) {
				{ "reset", 24 },
				{ "dc", 25 },
				{},
			},
		}
	}
};

static void fbtft_device_pdev_release(struct device *dev);

static struct platform_device fbtft_device_pdev_displays[] = {
	{
		.name = "itdb28fb",
		.id = 0,
		.dev = {
					.release = fbtft_device_pdev_release,
					.platform_data = &(struct fbtft_platform_data) {
						.gpios = (const struct fbtft_gpio []) {
							{ "reset", 17 },
							{ "dc", 1 },
							{ "wr", 0 },
							{ "cs", 21 },
							{ "db00", 9 },
							{ "db01", 11 },
							{ "db02", 18 },
							{ "db03", 23 },
							{ "db04", 24 },
							{ "db05", 25 },
							{ "db06", 8 },
							{ "db07", 7 },
							{ "led", 4 },
							{},
						},
					},
				},
	}, {
		.name = "flexpfb",
		.id = 0,
		.dev = {
					.release = fbtft_device_pdev_release,
					.platform_data = &(struct fbtft_platform_data) {
						.gpios = (const struct fbtft_gpio []) {
							{ "reset", 17 },
							{ "dc", 1 },
							{ "wr", 0 },
							{ "cs", 21 },
							{ "db00", 9 },
							{ "db01", 11 },
							{ "db02", 18 },
							{ "db03", 23 },
							{ "db04", 24 },
							{ "db05", 25 },
							{ "db06", 8 },
							{ "db07", 7 },
							{ "led", 4 },
							{},
						},
					},
				},
	}, {
		.name = "sainsmart32fb",
		.id = 0,
		.dev = {
					.release = fbtft_device_pdev_release,
					.platform_data = &(struct fbtft_platform_data) {
						.gpios = (const struct fbtft_gpio []) {
							{},
						},
					},
				},
	}
};

/* used if gpios parameter is present */
static struct fbtft_gpio fbtft_device_param_gpios[MAX_GPIOS+1] = { };
static struct fbtft_platform_data fbtft_device_param_pdata = {
	.gpios = fbtft_device_param_gpios,
};


static void fbtft_device_pdev_release(struct device *dev)
{
 /* Used to silence this message:  Device 'xxx' does not have a release() function, it is broken and must be fixed. */
}

static int spi_device_found(struct device *dev, void *data)
{
	struct spi_device *spi = container_of(dev, struct spi_device, dev);

	pr_info(DRVNAME":      %s %s %dkHz %d bits mode=0x%02X\n", spi->modalias, dev_name(dev), spi->max_speed_hz/1000, spi->bits_per_word, spi->mode);

	return 0;
}


static void pr_spi_devices(void)
{
	pr_info(DRVNAME":  SPI devices registered:\n");
	bus_for_each_dev(&spi_bus_type, NULL, NULL, spi_device_found);
}

static int p_device_found(struct device *dev, void *data)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);

	if (strstr(pdev->name, "fb"))
		pr_info(DRVNAME":      %s id=%d pdata? %s\n", pdev->name, pdev->id, pdev->dev.platform_data ? "yes" : "no");

	return 0;
}


static void pr_p_devices(void)
{
	pr_info(DRVNAME":  'fb' Platform devices registered:\n");
	bus_for_each_dev(&platform_bus_type, NULL, NULL, p_device_found);
}


static void fbtft_device_delete(struct spi_master *master, unsigned cs)
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


static int __init fbtft_device_init(void)
{
	struct spi_master *master = NULL;
	struct spi_board_info *display = NULL;
	const struct fbtft_platform_data *pdata = NULL;
	const struct fbtft_gpio *gpio = NULL;
	char *p_name, *p_num;
	bool found = false;
	int i;
	long val;
	int ret = 0;

	pr_debug("\n\n"DRVNAME": init\n");

	/* parse module parameter: gpios */
	if (gpios_num > MAX_GPIOS) {
		pr_err(DRVNAME":  gpios parameter: exceeded max array size: %d\n", MAX_GPIOS);
		return -EINVAL;
	}
	if (gpios_num > 0) {
		for (i=0;i<gpios_num;i++) {
			if (strchr(gpios[i], ':') == NULL) {
				pr_err(DRVNAME":  error missing ':' in gpios parameter: %s\n", gpios[i]);
				return -EINVAL;
			}
			p_num = gpios[i];
			p_name = strsep(&p_num, ":");
			if (p_name == NULL || p_num == NULL) {
				pr_err(DRVNAME":  something bad happenned parsing gpios parameter: %s\n", gpios[i]);
				return -EINVAL;
			}
			ret = kstrtol(p_num, 10, &val);
			if (ret) {
				pr_err(DRVNAME":  could not parse number in gpios parameter: %s:%s\n", p_name, p_num);
				return -EINVAL;
			}
			strcpy(fbtft_device_param_gpios[i].name, p_name);
			fbtft_device_param_gpios[i].gpio = (int) val;
			pdata = &fbtft_device_param_pdata;
		}
	}

	if (verbose > 2)
		pr_spi_devices(); /* print list of registered SPI devices */

	if (verbose > 2)
		pr_p_devices(); /* print list of 'fb' platform devices */

	if (name == NULL) {
		pr_err(DRVNAME":  missing module parameter: 'name'\n");
		pr_err(DRVNAME":  Use 'modinfo -p "DRVNAME"' to get all parameters\n");
		return -EINVAL;
	}

	pr_debug(DRVNAME":  name='%s', busnum=%d, cs=%d\n", name, busnum, cs);

	if (rotate > 3) {
		pr_warning("argument 'rotate' illegal value: %d (0-3). Setting it to 0.\n", rotate);
		rotate = 0;
	}

	/* name=list lists all supported drivers */
	if (strncmp(name, "list", 32) == 0) {
		pr_info(DRVNAME":  Supported drivers:\n");
		for (i=0; i < ARRAY_SIZE(fbtft_device_spi_displays); i++) {
			pr_info(DRVNAME":      %s\n", fbtft_device_spi_displays[i].modalias);
		}
		for (i=0; i < ARRAY_SIZE(fbtft_device_pdev_displays); i++) {
			pr_info(DRVNAME":      %s\n", fbtft_device_pdev_displays[i].name);
		}
		return -ECANCELED;
	}

	/* see if it is a SPI device */
	for (i=0; i < ARRAY_SIZE(fbtft_device_spi_displays); i++) {
		if (strncmp(name, fbtft_device_spi_displays[i].modalias, 32) == 0) {
			master = spi_busnum_to_master(busnum);
			if (!master) {
				pr_err(DRVNAME":  spi_busnum_to_master(%d) returned NULL\n", busnum);
				return -EINVAL;
			}
			fbtft_device_delete(master, cs);      /* make sure it's available */
			display = &fbtft_device_spi_displays[i];
			display->chip_select = cs;
			display->bus_num = busnum;
			if (speed)
				display->max_speed_hz = speed;
			if (mode != -1)
				display->mode = mode;
			if (pdata)
				display->platform_data = pdata;
			pdata = display->platform_data;
			((struct fbtft_platform_data *)pdata)->rotate = rotate;
			((struct fbtft_platform_data *)pdata)->bgr = bgr;
			((struct fbtft_platform_data *)pdata)->startbyte = startbyte;
			if (fps)
				((struct fbtft_platform_data *)pdata)->fps = fps;
			if (txbuflen)
				((struct fbtft_platform_data *)pdata)->txbuflen = txbuflen;
			spi_device = spi_new_device(master, display);
			put_device(&master->dev);
			if (!spi_device) {
				pr_err(DRVNAME":    spi_new_device() returned NULL\n");
				return -EPERM;
			}
			found = true;
			break;
		}
	}

	if (!found) {
		/* see if it is a platform_device */
		for (i=0; i < ARRAY_SIZE(fbtft_device_pdev_displays); i++) {
			if (strncmp(name, fbtft_device_pdev_displays[i].name, 32) == 0) {
				p_device = &fbtft_device_pdev_displays[i];
				ret = platform_device_register(p_device);
				if (ret < 0) {
					pr_err(DRVNAME":    platform_device_register() returned %d\n", ret);
					return ret;
				}
				if (pdata)
					p_device->dev.platform_data = (void *)pdata;
				pdata = p_device->dev.platform_data;
				((struct fbtft_platform_data *)pdata)->rotate = rotate;
				((struct fbtft_platform_data *)pdata)->bgr = bgr;
				((struct fbtft_platform_data *)pdata)->startbyte = startbyte;
				if (fps)
					((struct fbtft_platform_data *)pdata)->fps = fps;
				if (txbuflen)
					((struct fbtft_platform_data *)pdata)->txbuflen = txbuflen;
				found = true;
				break;
			}
		}
	}

	if (!found) {
		pr_err(DRVNAME":  device not supported: '%s'\n", name);
		return -EINVAL;
	}

	if (verbose)
		pr_info(DRVNAME":  GPIOS used by '%s':\n", name);
	gpio = pdata->gpios;
	if (!gpio) {
		pr_err(DRVNAME":  gpio is unexspectedly empty\n");
		return -EINVAL;
	}
	while (verbose && gpio->name[0]) {
		pr_info(DRVNAME":    '%s' = GPIO%d\n", gpio->name, gpio->gpio);
		gpio++;
	}

	if (spi_device && (verbose > 1))
		pr_spi_devices();
	if (p_device && (verbose > 1))
		pr_p_devices();

	return 0;
}

static void __exit fbtft_device_exit(void)
{
	pr_debug(DRVNAME" - exit\n");

	if (spi_device) {
		device_del(&spi_device->dev);
		kfree(spi_device);
	}

	if (p_device)
		platform_device_unregister(p_device);

}


module_init(fbtft_device_init);
module_exit(fbtft_device_exit);

MODULE_DESCRIPTION("Add a FBTFT device.");
MODULE_AUTHOR("Noralf Tronnes");
MODULE_LICENSE("GPL");
