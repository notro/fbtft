/*
 * FB driver for the Beneq EL320.240.36-HB SPI Display
 *
 * Based on the HX8347D FB driver
 * Copyright (C) 2013 Christian Vogelgsang
 *
 * Based on driver code found here: https://github.com/watterott/r61505u-Adapter
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
 */

#ifndef __EL32024036_H__
#define __EL32024036_H__

// framebuffer setup for this driver:
// width = 40
// height = 240
// bits per pixel = 1
//
// Note:
// Linux framebuffer should be setup for 40x240, 1 bpp.  Driver code (elsewhere) must deal with turning the 320x240 data
// into 40x240 and write it to the framebuffer device (this packs all 320 bits into 40 bytes, 320/8 = 40).

//#define HARDWARE_REV_1
#define HARDWARE_REV_A

#define BPP		1
#define WIDTH	( 320 / (BPP * 8) ) // framebuffer has 320 bits of data per line packed into 40 bytes
#define HEIGHT	240

#if defined(HARDWARE_REV_1)
	#define TOTAL_BYTES	(WIDTH * HEIGHT)
#elif defined(HARDWARE_REV_A)
	// extra 6 bytes needed per row due to mark bit in each byte
	// extra 4 bytes for start_of_frame, cmd, xor and end_of_frame bytes
	#define TOTAL_BYTES ((WIDTH + 6) * HEIGHT) + 4
#endif

static int el32024036_write_vmem(struct fbtft_par *par, size_t offset, size_t len);
static void set_addr_win(struct fbtft_par *par, int xs, int ys, int xe, int ye);
static int verify_gpios_dc(struct fbtft_par *par);

#endif /* __EL32024036_H__ */
