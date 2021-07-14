#pragma once

#include <ntddk.h>
#include <wmilib.h>	// required for WMILIB_CONTEXT

#include "vhci_devconf.h"

#define IS_DEVOBJ_VHCI(devobj)	(((pvdev_t)(devobj)->DeviceExtension)->type == VDEV_VHCI)
#define IS_DEVOBJ_VPDO(devobj)	(((pvdev_t)(devobj)->DeviceExtension)->type == VDEV_VPDO)

#define DEVOBJ_TO_VDEV(devobj)	((pvdev_t)((devobj)->DeviceExtension))
#define DEVOBJ_VDEV_TYPE(devobj)	(((pvdev_t)((devobj)->DeviceExtension))->type)
#define DEVOBJ_TO_CPDO(devobj)	((pcpdo_dev_t)((devobj)->DeviceExtension))
#define DEVOBJ_TO_VHCI(devobj)	((pvhci_dev_t)((devobj)->DeviceExtension))
#define DEVOBJ_TO_HPDO(devobj)	((phpdo_dev_t)((devobj)->DeviceExtension))
#define DEVOBJ_TO_VHUB(devobj)	((pvhub_dev_t)((devobj)->DeviceExtension))
#define DEVOBJ_TO_VPDO(devobj)	((pvpdo_dev_t)((devobj)->DeviceExtension))

#define TO_DEVOBJ(vdev)		((vdev)->common.Self)

#define VHUB_FROM_VHCI(vhci)	((pvhub_dev_t)(vhci)->common.child_pdo ? (pvhub_dev_t)(vhci)->common.child_pdo->fdo: NULL)
#define VHUB_FROM_VPDO(vpdo)	((pvhub_dev_t)(vpdo)->common.parent)

#define IS_FDO(type)		((type) == VDEV_ROOT || (type) == VDEV_VHCI || (type) == VDEV_VHUB)

extern LPCWSTR devcodes[];

// These are the states a vpdo or vhub transition upon
// receiving a specific PnP Irp. Refer to the PnP Device States
// diagram in DDK documentation for better understanding.
typedef enum _DEVICE_PNP_STATE {
	NotStarted = 0,		// Not started yet
	Started,		// Device has received the START_DEVICE IRP
	StopPending,		// Device has received the QUERY_STOP IRP
	Stopped,		// Device has received the STOP_DEVICE IRP
	RemovePending,		// Device has received the QUERY_REMOVE IRP
	SurpriseRemovePending,	// Device has received the SURPRISE_REMOVE IRP
	Deleted,		// Device has received the REMOVE_DEVICE IRP
	UnKnown			// Unknown state
} DEVICE_PNP_STATE;

// Structure for reporting data to WMI
typedef struct _USBIP_BUS_WMI_STD_DATA
{
	// The error Count
	UINT32   ErrorCount;
} USBIP_BUS_WMI_STD_DATA, *PUSBIP_BUS_WMI_STD_DATA;

typedef enum {
	VDEV_ROOT,
	VDEV_CPDO,
	VDEV_VHCI,
	VDEV_HPDO,
	VDEV_VHUB,
	VDEV_VPDO
} vdev_type_t;

// A common header for the device extensions of the vhub and vpdo
typedef struct _vdev {
	// A back pointer to the device object for which this is the extension
	PDEVICE_OBJECT	Self;

	vdev_type_t		type;
	// reference count for maintaining vdev validity
	LONG	n_refs;

	// We track the state of the device with every PnP Irp
	// that affects the device through these two variables.
	DEVICE_PNP_STATE	DevicePnPState;
	DEVICE_PNP_STATE	PreviousPnPState;

	// Stores the current system power state
	SYSTEM_POWER_STATE	SystemPowerState;

	// Stores current device power state
	DEVICE_POWER_STATE	DevicePowerState;


	// root and vhci have cpdo and hpdo each
	struct _vdev	*child_pdo, *parent, *fdo;
	PDEVICE_OBJECT	pdo;
	PDEVICE_OBJECT	devobj_lower;
} vdev_t, *pvdev_t;

struct urb_req;
struct _cpdo;
struct _vhub;
struct _hpdo;

typedef struct
{
	vdev_t	common;
} root_dev_t, *proot_dev_t;

typedef struct _cpdo
{
	vdev_t	common;
} cpdo_dev_t, *pcpdo_dev_t;

typedef struct
{
	vdev_t	common;

	UNICODE_STRING	DevIntfVhci;
	UNICODE_STRING	DevIntfUSBHC;

	// WMI Information
	WMILIB_CONTEXT	WmiLibInfo;

	USBIP_BUS_WMI_STD_DATA	StdUSBIPBusData;
} vhci_dev_t, *pvhci_dev_t;

typedef struct _hpdo
{
	vdev_t	common;
} hpdo_dev_t, *phpdo_dev_t;

// The device extension of the vhub.  From whence vpdo's are born.
typedef struct _vhub
{
	vdev_t	common;

	// List of vpdo's created so far
	LIST_ENTRY	head_vpdo;

	ULONG		n_max_ports;

	// the number of current vpdo's
	ULONG		n_vpdos;
	ULONG		n_vpdos_plugged;

	// A synchronization for access to the device extension.
	FAST_MUTEX	Mutex;

	// The number of IRPs sent from the bus to the underlying device object
	LONG		OutstandingIO; // Biased to 1

	UNICODE_STRING	DevIntfRootHub;

	// On remove device plug & play request we must wait until all outstanding
	// requests have been completed before we can actually delete the device
	// object. This event is when the Outstanding IO count goes to zero
	KEVENT		RemoveEvent;
} vhub_dev_t, *pvhub_dev_t;

// The device extension for the vpdo.
// That's of the USBIP device which this bus driver enumerates.
typedef struct
{
	vdev_t	common;

	// An array of (zero terminated wide character strings).
	// The array itself also null terminated
	USHORT	vendor, product, revision;
	UCHAR	usbclass, subclass, protocol, inum;
	// unique port number of the device on the bus
	ULONG	port;

	/*
	 * user-defined instance id. If NULL, port number will be used.
	 * instance id is regarded as USB serial.
	 */
	PWCHAR	winstid;

	// Link point to hold all the vpdos for a single bus together
	LIST_ENTRY	Link;

	// set to TRUE when the vpdo is exposed via PlugIn IOCTL,
	// and set to FALSE when a UnPlug IOCTL is received.
	BOOLEAN		plugged;

	UCHAR	speed;
	UCHAR	num_configurations; // Number of Possible Configurations

	// a pending irp when no urb is requested
	PIRP	pending_read_irp;
	// a partially transferred urb_req
	struct urb_req	*urbr_sent_partial;
	// a partially transferred length of urbr_sent_partial
	ULONG	len_sent_partial;
	// all urb_req's. This list will be used for clear or cancellation.
	LIST_ENTRY	head_urbr;
	// pending urb_req's which are not transferred yet
	LIST_ENTRY	head_urbr_pending;
	// urb_req's which had been sent and have waited for response
	LIST_ENTRY	head_urbr_sent;
	KSPIN_LOCK	lock_urbr;
	PFILE_OBJECT	fo;
	unsigned int	devid;
	unsigned long	seq_num;
	PUSB_DEVICE_DESCRIPTOR	dsc_dev;
	PUSB_CONFIGURATION_DESCRIPTOR	dsc_conf;
	UNICODE_STRING	usb_dev_interface;
	UCHAR	current_intf_num, current_intf_alt;
} vpdo_dev_t, *pvpdo_dev_t;

PDEVICE_OBJECT
vdev_create(PDRIVER_OBJECT drvobj, vdev_type_t type);

void vdev_add_ref(pvdev_t vdev);
void vdev_del_ref(pvdev_t vdev);

pvpdo_dev_t vhub_find_vpdo(pvhub_dev_t vhub, unsigned port);

void
vhub_mark_unplugged_vpdo(pvhub_dev_t vhub, pvpdo_dev_t vpdo);

LPWSTR
get_device_prop(PDEVICE_OBJECT pdo, DEVICE_REGISTRY_PROPERTY prop, PULONG plen);
