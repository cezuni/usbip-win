#include "vhci.h"

#include <wdmsec.h> // for IoCreateDeviceSecure

#include "vhci_dev.h"
#include "usbip_vhci_api.h"

// This guid is used in IoCreateDeviceSecure call to create vpdos. The idea is to
// allow the administrators to control access to the child device, in case the
// device gets enumerated as a raw device - no function driver, by modifying the 
// registry. If a function driver is loaded for the device, the system will override
// the security descriptor specified in the call to IoCreateDeviceSecure with the 
// one specifyied for the setup class of the child device.
//
DEFINE_GUID(GUID_SD_USBIP_VHCI_VPDO,
	0x9d3039dd, 0xcca5, 0x4b4d, 0xb3, 0x3d, 0xe2, 0xdd, 0xc8, 0xa8, 0xc5, 0x2e);
// {9D3039DD-CCA5-4b4d-B33D-E2DDC8A8C52E}

extern PAGEABLE void
vhci_init_vpdo(pusbip_vpdo_dev_t vpdo);

PAGEABLE NTSTATUS
vhci_plugin_dev(ioctl_usbip_vhci_plugin *plugin, pusbip_vhub_dev_t vhub, PFILE_OBJECT fo)
{
	PDEVICE_OBJECT		devobj;
	pusbip_vpdo_dev_t	vpdo, devpdo_old;
	PLIST_ENTRY	entry;
	NTSTATUS	status;

	PAGED_CODE();

	DBGI(DBG_IOCTL, "Plugin vpdo: port: %hhd, vendor:product: %04hx:%04hx\n", plugin->port, plugin->vendor, plugin->product);

	if (plugin->port <= 0)
		return STATUS_INVALID_PARAMETER;

	ExAcquireFastMutex(&vhub->Mutex);

	for (entry = vhub->head_vpdo.Flink; entry != &vhub->head_vpdo; entry = entry->Flink) {
		vpdo = CONTAINING_RECORD(entry, usbip_vpdo_dev_t, Link);
		if ((ULONG)plugin->port == vpdo->port &&
			vpdo->common.DevicePnPState != SurpriseRemovePending) {
			ExReleaseFastMutex(&vhub->Mutex);
			return STATUS_INVALID_PARAMETER;
		}
	}

	ExReleaseFastMutex(&vhub->Mutex);

	// Create the vpdo
	DBGI(DBG_PNP, "vhub->NextLowerDriver = 0x%p\n", vhub->NextLowerDriver);

	// vpdo must have a name. You should let the system auto generate a
	// name by specifying FILE_AUTOGENERATED_DEVICE_NAME in the
	// DeviceCharacteristics parameter. Let us create a secure deviceobject,
	// in case the child gets installed as a raw device (RawDeviceOK), to prevent
	// an unpriviledged user accessing our device. This function is avaliable
	// in a static WDMSEC.LIB and can be used in Win2k, XP, and Server 2003
	// Just make sure that  the GUID specified here is not a setup class GUID.
	// If you specify a setup class guid, you must make sure that class is
	// installed before enumerating the vpdo.

	status = IoCreateDeviceSecure(vhub->common.Self->DriverObject, sizeof(usbip_vpdo_dev_t), NULL,
		FILE_DEVICE_BUS_EXTENDER, FILE_AUTOGENERATED_DEVICE_NAME | FILE_DEVICE_SECURE_OPEN,
		FALSE, &SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RWX_RES_RWX, // allow normal users to access the devices
		(LPCGUID)&GUID_SD_USBIP_VHCI_VPDO, &devobj);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	vpdo = (pusbip_vpdo_dev_t)devobj->DeviceExtension;

	vpdo->vendor = plugin->vendor;
	vpdo->product = plugin->product;
	vpdo->revision = plugin->version;
	vpdo->usbclass = plugin->class;
	vpdo->subclass = plugin->subclass;
	vpdo->protocol = plugin->protocol;
	vpdo->inum = plugin->inum;
	vpdo->instance = plugin->instance;

	devpdo_old = (pusbip_vpdo_dev_t)InterlockedCompareExchangePointer(&(fo->FsContext), vpdo, 0);
	if (devpdo_old) {
		DBGI(DBG_GENERAL, "you can't plugin again");
		IoDeleteDevice(devobj);
		return STATUS_INVALID_PARAMETER;
	}
	vpdo->port = plugin->port;
	vpdo->fo = fo;
	vpdo->devid = plugin->devid;
	vpdo->speed = plugin->speed;

	vpdo->common.is_vhub = FALSE;
	vpdo->common.Self = devobj;
	vpdo->vhub = vhub;

	vhci_init_vpdo(vpdo);

	// Device Relation changes if a new vpdo is created. So let
	// the PNP system now about that. This forces it to send bunch of pnp
	// queries and cause the function driver to be loaded.
	IoInvalidateDeviceRelations(vhub->UnderlyingPDO, BusRelations);

	return status;
}
