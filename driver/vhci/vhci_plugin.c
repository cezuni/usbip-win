#include "vhci.h"

#include <wdmsec.h> // for IoCreateDeviceSecure

#include "vhci_pnp.h"
#include "vhci_dev.h"
#include "usbip_vhci_api.h"

#include "usb_util.h"
#include "usbip_proto.h"

extern CHAR vhub_get_empty_port(pvhub_dev_t vhub);
extern void vhub_attach_vpdo(pvhub_dev_t vhub, pvpdo_dev_t vpdo);

extern void vhub_mark_unplugged_all_vpdos(pvhub_dev_t vhub);

static PAGEABLE void
vhci_init_vpdo(pvpdo_dev_t vpdo)
{
	PAGED_CODE();

	DBGI(DBG_PNP, "vhci_init_vpdo: 0x%p\n", vpdo);

	vpdo->plugged = TRUE;

	vpdo->current_intf_num = 0;
	vpdo->current_intf_alt = 0;

	INITIALIZE_PNP_STATE(vpdo);

	// vpdo usually starts its life at D3
	vpdo->common.DevicePowerState = PowerDeviceD3;
	vpdo->common.SystemPowerState = PowerSystemWorking;

	InitializeListHead(&vpdo->head_urbr);
	InitializeListHead(&vpdo->head_urbr_pending);
	InitializeListHead(&vpdo->head_urbr_sent);
	KeInitializeSpinLock(&vpdo->lock_urbr);

	TO_DEVOBJ(vpdo)->Flags |= DO_POWER_PAGABLE|DO_DIRECT_IO;

	InitializeListHead(&vpdo->Link);

	vhub_attach_vpdo(VHUB_FROM_VPDO(vpdo), vpdo);

	// This should be the last step in initialization.
	TO_DEVOBJ(vpdo)->Flags &= ~DO_DEVICE_INITIALIZING;
}

static void
setup_vpdo_with_dsc_dev(pvpdo_dev_t vpdo, PUSB_DEVICE_DESCRIPTOR dsc_dev)
{
	if (dsc_dev) {
		vpdo->vendor = dsc_dev->idVendor;
		vpdo->product = dsc_dev->idProduct;
		vpdo->revision = dsc_dev->bcdDevice;
		vpdo->usbclass = dsc_dev->bDeviceClass;
		vpdo->subclass = dsc_dev->bDeviceSubClass;
		vpdo->protocol = dsc_dev->bDeviceProtocol;
		vpdo->speed = (UCHAR)get_usb_speed(dsc_dev->bcdUSB);
		vpdo->num_configurations = dsc_dev->bNumConfigurations;
	} else {
		vpdo->vendor = 0;
		vpdo->product = 0;
		vpdo->revision = 0;
		vpdo->usbclass = 0;
		vpdo->subclass = 0;
		vpdo->protocol = 0;
		vpdo->speed = USB_SPEED_LOW;
		vpdo->num_configurations = 0;
	}
}

static void
setup_vpdo_with_dsc_conf(pvpdo_dev_t vpdo, PUSB_CONFIGURATION_DESCRIPTOR dsc_conf)
{
	if (!dsc_conf) {
		vpdo->inum = 0;
		return;
	}

	vpdo->inum = dsc_conf->bNumInterfaces;

	/* Many devices have 0 usb class number in a device descriptor.
	 * 0 value means that class number is determined at interface level.
	 * USB class, subclass and protocol numbers should be setup before importing.
	 * Because windows vhci driver builds a device compatible id with those numbers.
	 */
	if (vpdo->usbclass || vpdo->subclass || vpdo->protocol) {
		return;
	}

	/* buf[4] holds the number of interfaces in USB configuration.
	 * Supplement class/subclass/protocol only if there exists only single interface.
	 * A device with multiple interfaces will be detected as a composite by vhci.
	 */
	if (vpdo->inum == 1) {
		PUSB_INTERFACE_DESCRIPTOR dsc_intf = dsc_find_first_intf(dsc_conf);
		if (dsc_intf) {
			vpdo->usbclass = dsc_intf->bInterfaceClass;
			vpdo->subclass = dsc_intf->bInterfaceSubClass;
			vpdo->protocol = dsc_intf->bInterfaceProtocol;
		}
	}
}

PAGEABLE NTSTATUS
vhci_plugin_vpdo(pvhci_dev_t vhci, pvhci_pluginfo_t pluginfo, ULONG inlen, PFILE_OBJECT fo)
{
	PDEVICE_OBJECT	devobj;
	pvpdo_dev_t	vpdo, devpdo_old;
	PUSHORT		pdscr_fullsize;

	PAGED_CODE();

	if (inlen < sizeof(vhci_pluginfo_t)) {
		DBGE(DBG_IOCTL, "too small input length: %lld < %lld", inlen, sizeof(vhci_pluginfo_t));
		return STATUS_INVALID_PARAMETER;
	}
	pdscr_fullsize = (PUSHORT)pluginfo->dscr_conf + 1;
	if (inlen != sizeof(vhci_pluginfo_t) + *pdscr_fullsize - 9) {
		DBGE(DBG_IOCTL, "invalid pluginfo format: %lld != %lld", inlen, sizeof(vhci_pluginfo_t) + *pdscr_fullsize - 9);
		return STATUS_INVALID_PARAMETER;
	}
	pluginfo->port = vhub_get_empty_port(VHUB_FROM_VHCI(vhci));
	if (pluginfo->port < 0)
		return STATUS_END_OF_FILE;

	DBGI(DBG_VPDO, "Plugin vpdo: port: %hhd\n", pluginfo->port);

	if ((devobj = vdev_create(TO_DEVOBJ(vhci)->DriverObject, VDEV_VPDO)) == NULL)
		return STATUS_UNSUCCESSFUL;

	vpdo = DEVOBJ_TO_VPDO(devobj);
	vpdo->common.parent = &VHUB_FROM_VHCI(vhci)->common;

	setup_vpdo_with_dsc_dev(vpdo, (PUSB_DEVICE_DESCRIPTOR)pluginfo->dscr_dev);
	setup_vpdo_with_dsc_conf(vpdo, (PUSB_CONFIGURATION_DESCRIPTOR)pluginfo->dscr_conf);

	if (pluginfo->wserial[0] != L'\0')
		vpdo->winstid = libdrv_strdupW(pluginfo->wserial);
	else
		vpdo->winstid = NULL;

	devpdo_old = (pvpdo_dev_t)InterlockedCompareExchangePointer(&fo->FsContext, vpdo, 0);
	if (devpdo_old) {
		DBGI(DBG_GENERAL, "you can't plugin again");
		IoDeleteDevice(devobj);
		return STATUS_INVALID_PARAMETER;
	}
	vpdo->port = pluginfo->port;
	vpdo->fo = fo;
	vpdo->devid = pluginfo->devid;

	vhci_init_vpdo(vpdo);

	// Device Relation changes if a new vpdo is created. So let
	// the PNP system now about that. This forces it to send bunch of pnp
	// queries and cause the function driver to be loaded.
	IoInvalidateDeviceRelations(vhci->common.pdo, BusRelations);

	return STATUS_SUCCESS;
}

PAGEABLE NTSTATUS
vhci_unplug_port(pvhci_dev_t vhci, CHAR port)
{
	pvhub_dev_t	vhub = VHUB_FROM_VHCI(vhci);
	pvpdo_dev_t	vpdo;

	PAGED_CODE();

	if (vhub == NULL) {
		DBGI(DBG_PNP, "vhub has gone\n");
		return STATUS_NO_SUCH_DEVICE;
	}

	if (port < 0) {
		DBGI(DBG_PNP, "plugging out all the devices!\n");
		vhub_mark_unplugged_all_vpdos(vhub);
		return STATUS_SUCCESS;
	}

	DBGI(DBG_PNP, "plugging out device: port: %u\n", port);

	vpdo = vhub_find_vpdo(vhub, port);
	if (vpdo == NULL) {
		DBGI(DBG_PNP, "no matching vpdo: port: %u\n", port);
		return STATUS_NO_SUCH_DEVICE;
	}

	vhub_mark_unplugged_vpdo(vhub, vpdo);
	vdev_del_ref((pvdev_t)vpdo);

	return STATUS_SUCCESS;
}
