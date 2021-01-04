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
 *   see: https://web.archive.org/web/20070922153407/ \
 *     http://www.fexx.org/usbio/spec-ja.html
 */

#define USBIO_WRITE_PORT0	0x01
#define USBIO_WRITE_PORT1	0x02
#define USBIO_READ_PORT0	0x03
#define USBIO_READ_PORT1	0x04

/*
 * USB-IO(2.0) commands (not complete list)
 */
#define USBIO2_RW		0x20

#define USBIO_PORT1_MASK	0x0f

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
	int		protocol_version;	/* protocol version */
} usbio_models [] = {
	0x0bfe, 0x1003, 1,	/* Morphy Planning USB-IO 1.0 */
	0x1352, 0x0100, 1,	/* Km2Net USB-IO 1.0 */
	0x1352, 0x0120, 2,	/* Km2Net USB-IO 2.0 */
	0x1352, 0x0121, 2,	/* Km2Net USB-IO 2.0(AKI) */
};

/* global variables */
unsigned char seqno = 0;

/* prototypes */
int usbio_open(void);
int usbio_check(int);
int usbio_write1(int, int, unsigned char *);
int usbio_write2(int, int, unsigned char *);

/*
 * check vendor/product IDs on an opened file descriptor
 *   return its protocol version (1 or 2) if found
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
				return usbio_models[i].protocol_version;

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

/*
 * protocol version 1
 */
int
usbio_write1(int fd, int port, unsigned char *data) {
	int ret;
	unsigned char buf[64];

	buf[0] = (port == 0 ? USBIO_WRITE_PORT0 : USBIO_WRITE_PORT1);
	buf[1] = *data;
	buf[7] = seqno;

	ret = write(fd, buf, 8);
	if (ret == -1)
		err(1, "write");
	else if (ret != 0) {
		DPRINTF("write: %02x:%02x %02x %02x %02x %02x %02x:%02x\n",
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
		if (buf[7] == seqno) {
			DPRINTF("read : %02x:%02x %02x %02x %02x %02x %02x:%02x\n",
				buf[0], buf[1], buf[2], buf[3],
				buf[4], buf[5], buf[6], buf[7]);
			break;
		}
	}

	seqno++;
	return ret;
}

/*
 * protocol version 2
 */
int
usbio_write2(int fd, int port, unsigned char *data) {
	int ret;
	unsigned char buf[64];

	buf[0] = USBIO2_RW;
	buf[1] = (unsigned char)(port + 1);
	buf[2] = *data;
	buf[3] = buf[4] = buf[5] = buf[6] = buf[7] = buf[8] = 0;
	buf[63] = seqno;

	ret = write(fd, buf, 64);
	if (ret == -1)
		err(1, "write");
	else if (ret != 0) {
		DPRINTF("write: %02x:%02x %02x %02x %02x"
			" %02x %02x %02x %02x:%02x\n",
			buf[0], buf[1], buf[2], buf[3], buf[4],
			buf[5], buf[6], buf[7], buf[8], buf[63]);
	}

	for(;;) {
		ret = read(fd, buf, 64);
		if (ret == -1)
			err(1, "read");
		if (ret == 0)
			break;
		if (buf[63] == seqno) {
			DPRINTF("read : %02x:%02x %02x %02x %02x"
				" %02x %02x %02x %02x:%02x\n",
				buf[0], buf[1], buf[2], buf[3], buf[4],
				buf[5], buf[6], buf[7], buf[8], buf[63]);
			break;
		}
	}

	seqno++;
	return ret;
}

int port = 1;

/*
 * main
 */
int
main(int argc, char *argv[]) {
	int fd, ret;
	unsigned char data;

	if (argc != 2) {
		fprintf(stderr, "need output data\n");
		exit(1);
	}

	data = (unsigned char)atoi(argv[1]);	/* XXX: error check! */

	fd = usbio_open();

	/* write */
	data = data & USBIO_PORT1_MASK;
#if 0
	data = ~data;
	usbio_write1(fd, port, &data);
#else
	usbio_write2(fd, port, &data);
#endif

	sleep(3);	/* wait for 3 second */

	/* write again */
	data = 0x00 & USBIO_PORT1_MASK;
#if 0
	data = ~data;
	usbio_write1(fd, port, &data);
#else
	usbio_write2(fd, port, &data);
#endif

	close(fd);
	exit(0);
}
