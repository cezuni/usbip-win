/*
 * Copyright (C) 2005-2007 Takahiro Hirofuchi
 */

#ifndef _USBIP_COMMON_H
#define _USBIP_COMMON_H

#include <stdint.h>
#include <stdio.h>

/* Defines for op_code status in server/client op_common PDUs */
#define ST_OK	0x00
#define ST_NA	0x01
	/* Device requested for import is not available */
#define ST_DEV_BUSY	0x02
	/* Device requested for import is in error state */
#define ST_DEV_ERR	0x03
#define ST_NODEV	0x04
#define ST_ERROR	0x05

/* error codes for userspace tools and library */
#define ERR_GENERAL	(-1)
#define ERR_INVARG	(-2)
#define ERR_NETWORK	(-3)
#define ERR_VERSION	(-4)
#define ERR_PROTOCOL	(-5)
#define ERR_STATUS	(-6)
#define ERR_EXIST	(-7)
#define ERR_NOTEXIST	(-8)
#define ERR_DRIVER	(-9)
#define ERR_PORTFULL	(-10)
#define ERR_ACCESS	(-11)
#define ERR_CERTIFICATE	(-12)

/* FIXME: how to sync with drivers/usbip_common.h ? */
enum usbip_device_status{
	/* dev status unknown. */
	DEV_ST_UNKNOWN = 0x00,

	/* sdev is available. */
	SDEV_ST_AVAILABLE = 0x01,
	/* sdev is now used. */
	SDEV_ST_USED,
	/* sdev is unusable because of a fatal error. */
	SDEV_ST_ERROR,

	/* vdev does not connect a remote device. */
	VDEV_ST_NULL,
	/* vdev is used, but the USB address is not assigned yet */
	VDEV_ST_NOTASSIGNED,
	VDEV_ST_USED,
	VDEV_ST_ERROR
};

#define USBIP_DEV_PATH_MAX		256
#define USBIP_BUS_ID_SIZE		32

extern int usbip_use_stderr;
extern int usbip_use_debug ;

extern const char	*usbip_progname;

#define pr_fmt(fmt)	"%s: %s: " fmt "\n", usbip_progname
#define dbg_fmt(fmt)	pr_fmt("%s:%d:[%s] " fmt), "debug",	\
		        strrchr(__FILE__, '\\') + 1, __LINE__, __func__

#define err(fmt, ...)								\
	do {									\
		if (usbip_use_stderr) {						\
			fprintf(stderr, pr_fmt(fmt), "error", ##__VA_ARGS__);	\
		}								\
	} while (0)

#define info(fmt, ...)								\
	do {									\
		if (usbip_use_stderr) {						\
			fprintf(stderr, pr_fmt(fmt), "info", ##__VA_ARGS__);	\
		}								\
	} while (0)

#define dbg(fmt, ...)								\
	do {									\
		if (usbip_use_debug) {						\
			if (usbip_use_stderr) {					\
				fprintf(stderr, dbg_fmt(fmt), ##__VA_ARGS__);	\
			}							\
		}								\
	} while (0)

#define BUG()						\
	do {						\
		  err("sorry, it's a bug");		\
		  abort();				\
	} while (0)

#pragma pack(push, 1)

struct usbip_usb_interface {
	uint8_t bInterfaceClass;
	uint8_t bInterfaceSubClass;
	uint8_t bInterfaceProtocol;
	uint8_t padding;	/* alignment */
};

struct usbip_usb_device {
	char path[USBIP_DEV_PATH_MAX];
	char busid[USBIP_BUS_ID_SIZE];

	uint32_t busnum;
	uint32_t devnum;
	uint32_t speed;

	uint16_t idVendor;
	uint16_t idProduct;
	uint16_t bcdDevice;

	uint8_t bDeviceClass;
	uint8_t bDeviceSubClass;
	uint8_t bDeviceProtocol;
	uint8_t bConfigurationValue;
	uint8_t bNumConfigurations;
	uint8_t bNumInterfaces;
};

#pragma pack(pop)

#define to_string(s)	#s

void dump_usb_interface(struct usbip_usb_interface *);
void dump_usb_device(struct usbip_usb_device *);

const char *usbip_speed_string(int num);
const char *usbip_status_string(int32_t status);

int usbip_names_init(void);
void usbip_names_free(void);
void usbip_names_get_product(char *buff, size_t size, uint16_t vendor, uint16_t product);
void usbip_names_get_class(char *buff, size_t size, uint8_t class, uint8_t subclass, uint8_t protocol);

#endif
