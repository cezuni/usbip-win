/*
 * Copyright (C) 2011 matt mooney <mfm@muteddisk.com>
 *               2005-2007 Takahiro Hirofuchi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "usbip_windows.h"

#include "usbip_common.h"
#include "usbip_setupdi.h"
#include "usbip_stub.h"

static const char usbip_bind_usage_string[] =
	"usbip bind <args>\n"
	"    -b, --busid=<busid>    Bind usbip stub to device "
	"on <busid>\n";

void usbip_bind_usage(void)
{
	printf("usage: %s", usbip_bind_usage_string);
}

static int
walker_bind(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data, devno_t devno, void *ctx)
{
	devno_t	*pdevno = (devno_t *)ctx;

	if (devno == *pdevno) {
		int	ret;

		ret = attach_stub_driver(devno);
		if (ret == 0) {
			if (!restart_device(dev_info, pdev_info_data))
				return ERR_DRIVER;
			return 1;
		}
		return ret;
	}
	return 0;
}

static int
bind_device(const char *busid)
{
	unsigned char	devno;
	int	rc;

	devno = get_devno_from_busid(busid);
	if (devno == 0) {
		err("invalid bus id: %s", busid);
		return 1;
	}
	rc = traverse_usbdevs(walker_bind, TRUE, (void *)&devno);
	if (rc != 1) {
		switch (rc) {
		case 0:
		case ERR_NOTEXIST:
			err("no such device on busid %s", busid);
			return 2;
		case ERR_CERTIFICATE:
			err("\"USBIP Test\" certficiate not installed or properly located in certificate store");
			return 2;
		default:
			err("failed to bind device on busid %s", busid);
			return 3;
		}
	}
	info("bind device on busid %s: complete", busid);
	return 0;
}

int usbip_bind(int argc, char *argv[])
{
	static const struct option opts[] = {
		{ "busid", required_argument, NULL, 'b' },
		{ NULL,    0,                 NULL,  0  }
	};

	int opt;

	for (;;) {
		opt = getopt_long(argc, argv, "b:", opts, NULL);

		if (opt == -1)
			break;

		switch (opt) {
		case 'b':
			return bind_device(optarg);
		default:
			break;
		}
	}

	err("empty busid");
	usbip_bind_usage();
	return 1;
}
