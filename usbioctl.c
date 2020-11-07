/*
 * Copyright (c) 2020 Kenji Aoyama <aoyama@nk-home.net>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * usbioctl: set/reset USB-IO I/O pins individually
 */

#include <err.h>	/* err() */
#include <fcntl.h>	/* open() */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>	/* memset() */
#include <unistd.h>	/* close(), read(), write() */

#include <dev/usb/usb.h>

/*
 * USB-IO(1.0) commands (not complete list)
 *   see: https://web.archive.org/web/20070922153407/http://www.fexx.org/usbio/spec-ja.html
 */

#define USBIO_WRITE_PORT0	0x01
#define USBIO_WRITE_PORT1	0x02
#define USBIO_READ_PORT0	0x03
#define USBIO_READ_PORT1	0x04

#define USBIO_PORT1_MASK	0xf0

#define DEBUG
#ifdef DEBUG
#define DPRINTF(x...)	do { fprintf(stderr, x); } while (0)
#else
#define DPRINTF(x...)
#endif

/* USB-IO vendor and product ID */
struct {
	uint16_t	vendor;
	uint16_t	product;
} usbio_models [] = {
	0x0bfe, 0x1003,		/* Morphy Planning */
	0x1352, 0x0100,		/* Km2Net */
};

/* prototypes */
int usbio_open(void);
int usbio_check(int);
int usbio_read(int, int, unsigned char *);
int usbio_write(int, int, unsigned char *);

/*
 * check vendor/product IDs on an opened file descriptor
 *   return the index of product list (>= 0) if found
 *   return -1 if not found
 */
int
usbio_check(int fd) {
	int i, ret;
	int n = sizeof(usbio_models) / sizeof(usbio_models[0]);
	struct usb_device_info udi;

	ret = ioctl(fd, USB_GET_DEVICEINFO, &udi);
	if (ret == -1)
		err(1, "ioctl");

	DPRINTF("Vendor:0x%04x, Product:0x%04x, Release:0x%04x\n",
		udi.udi_vendorNo, udi.udi_productNo, udi.udi_releaseNo);

	for (i = 0; i < n; i++)
		if ((udi.udi_vendorNo == usbio_models[i].vendor) &&
			(udi.udi_productNo == usbio_models[i].product))
				return i;

	return -1;	/* not match */
}

/*
 * look for and open USB-IO device
 *   return file descriptor if found
 */
int
usbio_open(void) {
	int fd, i;
	char devname[256];

	for (int i = 0; i < 10; i++) {
		snprintf(devname, sizeof(devname), "/dev/uhid%d", i);
		fd = open(devname, O_RDWR);
		if (fd != -1) {
			if (usbio_check(fd) != -1)
				return fd;
			close(fd);
		}
	}

	/* exit if we can not find/open */
	fprintf(stderr, "can not find/open USB-IO device\n");
	exit(1);
}

int
usbio_read(int fd, int port, unsigned char *data) {
	int ret;
	unsigned char buf[8];

	buf[0] = (port == 0 ? USBIO_READ_PORT0 : USBIO_READ_PORT1);
	ret = write(fd, buf, 8);
	if (ret == -1)
		err(1, "write");

	ret = read(fd, buf, 8);
	if (ret == -1)
		err(1, "read");
	else if (ret != 0) {
		DPRINTF("read : %02x %02x %02x %02x %02x %02x %02x %02x\n",
			buf[0], buf[1], buf[2], buf[3],
			buf[4], buf[5], buf[6], buf[7]);
	}
	*data = buf[1];
	return ret;
}

int
usbio_write(int fd, int port, unsigned char *data) {
	static unsigned char seq = 0;
	int ret;
	unsigned char buf[8];

	buf[0] = (port == 0 ? USBIO_WRITE_PORT0 : USBIO_WRITE_PORT1);
	buf[1] = *data;
	buf[7] = seq;

	ret = write(fd, buf, 8);
	if (ret == -1)
		err(1, "write");
	else if (ret != 0) {
		DPRINTF("wrote: %02x %02x %02x %02x %02x %02x %02x %02x\n",
			buf[0], buf[1], buf[2], buf[3],
			buf[4], buf[5], buf[6], buf[7]);
	}

	buf[0] = (port == 0 ? USBIO_READ_PORT0 : USBIO_READ_PORT1);

	ret = write(fd, buf, 8);
	if (ret == -1)
		err(1, "write");

	for(;;) {
		ret = read(fd, buf, 8);
		if (ret == -1)
			err(1, "read");
		if (ret == 0)
			break;
		if (buf[7] == seq) {
			DPRINTF("read : %02x %02x %02x %02x %02x %02x %02x %02x\n",
				buf[0], buf[1], buf[2], buf[3],
				buf[4], buf[5], buf[6], buf[7]);
			break;
		}
	}

	seq++;
	return ret;
}

int port = 1;

int
main(int argc, char *argv[]) {
	int fd, ret;
	uint8_t data;

	fd = usbio_open();

	/* read current status */
	usbio_read(fd, port, &data);

	/* write */
	data = (char)(0x0d | USBIO_PORT1_MASK);
	usbio_write(fd, port, &data);

	sleep(1);	/* wait for 1 second */

	/* write again */
	data = (char)(0x0f | USBIO_PORT1_MASK);
	usbio_write(fd, port, &data);

	close(fd);
	exit(0);
}
