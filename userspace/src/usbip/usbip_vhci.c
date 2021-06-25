#define INITGUID

#include "usbip_common.h"
#include "usbip_windows.h"

#include <stdlib.h>

#include "usbip_setupdi.h"
#include "usbip_vhci_api.h"

#include "dbgcode.h"

static int
walker_devpath(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data, devno_t devno, void *ctx)
{
	char	**pdevpath = (char **)ctx;
	char	*id_hw;
	PSP_DEVICE_INTERFACE_DETAIL_DATA	pdev_interface_detail;

	id_hw = get_id_hw(dev_info, pdev_info_data);
	if (id_hw == NULL || (_stricmp(id_hw, "usbipwin\\vhci") != 0 && _stricmp(id_hw, "root\\vhci_ude") != 0)) {
		dbg("invalid hw id: %s", id_hw ? id_hw : "");
		if (id_hw != NULL)
			free(id_hw);
		return 0;
	}
	free(id_hw);

	pdev_interface_detail = get_intf_detail(dev_info, pdev_info_data, (LPCGUID)&GUID_DEVINTERFACE_VHCI_USBIP);
	if (pdev_interface_detail == NULL) {
		return 0;
	}

	*pdevpath = _strdup(pdev_interface_detail->DevicePath);
	free(pdev_interface_detail);
	return 1;
}

static char *
get_vhci_devpath(void)
{
	char	*devpath;

	if (traverse_intfdevs(walker_devpath, &GUID_DEVINTERFACE_VHCI_USBIP, &devpath) != 1) {
		return NULL;
	}

	return devpath;
}

HANDLE
usbip_vhci_driver_open(void)
{
	HANDLE	hdev;
	char	*devpath;

	devpath = get_vhci_devpath();
	if (devpath == NULL) {
		return INVALID_HANDLE_VALUE;
	}
	dbg("device path: %s", devpath);
	hdev = CreateFile(devpath, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
	free(devpath);
	return hdev;
}

void
usbip_vhci_driver_close(HANDLE hdev)
{
	CloseHandle(hdev);
}

static int
usbip_vhci_get_ports_status(HANDLE hdev, ioctl_usbip_vhci_get_ports_status *st)
{
	unsigned long	len;

	if (DeviceIoControl(hdev, IOCTL_USBIP_VHCI_GET_PORTS_STATUS,
		NULL, 0, st, sizeof(ioctl_usbip_vhci_get_ports_status), &len, NULL)) {
		if (len == sizeof(ioctl_usbip_vhci_get_ports_status))
			return 0;
	}
	return ERR_GENERAL;
}

int
usbip_vhci_get_free_port(HANDLE hdev)
{
	ioctl_usbip_vhci_get_ports_status	status;
	int	i;

	if (usbip_vhci_get_ports_status(hdev, &status))
		return -1;
	for (i = 0; i < status.n_max_ports; i++) {
		if (!status.port_status[i])
			return i;
	}
	return -1;
}

static int
get_n_max_ports(HANDLE hdev)
{
	ioctl_usbip_vhci_get_ports_status	status;
	int	res;

	res = usbip_vhci_get_ports_status(hdev, &status);
	if (res < 0)
		return res;
	return status.n_max_ports;
}

int
usbip_vhci_get_imported_devs(HANDLE hdev, pioctl_usbip_vhci_imported_dev_t *pidevs)
{
	ioctl_usbip_vhci_imported_dev	*idevs;
	int	n_max_ports;
	unsigned long	len_out, len_returned;

	n_max_ports = get_n_max_ports(hdev);
	if (n_max_ports < 0) {
		dbg("failed to get the number of used ports: %s", dbg_errcode(n_max_ports));
		return ERR_GENERAL;
	}

	len_out = sizeof(ioctl_usbip_vhci_imported_dev) * (n_max_ports + 1);
	idevs = (ioctl_usbip_vhci_imported_dev *)malloc(len_out);
	if (idevs == NULL) {
		dbg("out of memory");
		return ERR_GENERAL;
	}

	if (DeviceIoControl(hdev, IOCTL_USBIP_VHCI_GET_IMPORTED_DEVICES,
		NULL, 0, idevs, len_out, &len_returned, NULL)) {
		*pidevs = idevs;
		return 0;
	}
	else {
		dbg("failed to get imported devices: 0x%lx", GetLastError());
	}

	free(idevs);
	return ERR_GENERAL;
}

int
usbip_vhci_attach_device(HANDLE hdev, pvhci_pluginfo_t pluginfo)
{
	unsigned long	unused;

	if (!DeviceIoControl(hdev, IOCTL_USBIP_VHCI_PLUGIN_HARDWARE,
		pluginfo, pluginfo->size, pluginfo, sizeof(vhci_pluginfo_t), &unused, NULL)) {
		DWORD	err = GetLastError();
		if (err == ERROR_HANDLE_EOF)
			return ERR_PORTFULL;
		dbg("usbip_vhci_attach_device: DeviceIoControl failed: err: 0x%lx", GetLastError());
		return ERR_GENERAL;
	}

	return 0;
}

int
usbip_vhci_detach_device(HANDLE hdev, int port)
{
	ioctl_usbip_vhci_unplug  unplug;
	unsigned long	unused;
	DWORD	err;

	unplug.addr = (char)port;
	if (DeviceIoControl(hdev, IOCTL_USBIP_VHCI_UNPLUG_HARDWARE,
		&unplug, sizeof(unplug), NULL, 0, &unused, NULL))
		return 0;

	err = GetLastError();
	dbg("unplug error: 0x%lx", err);

	switch (err) {
	case ERROR_FILE_NOT_FOUND:
		return ERR_NOTEXIST;
	case ERROR_INVALID_PARAMETER:
		return ERR_INVARG;
	default:
		return ERR_GENERAL;
	}
}
