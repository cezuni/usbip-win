#include "pdu.h"

#include <usb.h>
#include <usbdi.h>

#include <stdbool.h>

/*
 * Linux error codes.
 * See: include/uapi/asm-generic/errno-base.h, include/uapi/asm-generic/errno.h
 */
enum {
	ENOENT_LNX = ENOENT,
	ENXIO_LNX = ENXIO,
	ENOMEM_LNX = ENOMEM,
	EBUSY_LNX = EBUSY,
	EXDEV_LNX = EXDEV,
	ENODEV_LNX = ENODEV,
	EINVAL_LNX = EINVAL,
	ENOSPC_LNX = ENOSPC,
	EPIPE_LNX = EPIPE,
	ETIME_LNX = 62,
	ENOSR_LNX = 63,
	ECOMM_LNX = 70,
	EPROTO_LNX = 71,
	EOVERFLOW_LNX = 75,
	EILSEQ_LNX = 84,
	ECONNRESET_LNX = 104,
	ESHUTDOWN_LNX = 108,
	ETIMEDOUT_LNX = 110,
	EINPROGRESS_LNX = 115,
	EREMOTEIO_LNX = 121,
};

static_assert(ENOENT == 2, "assert");
static_assert(ENXIO == 6, "assert");
static_assert(ENOMEM == 12, "assert");
static_assert(EBUSY == 16, "assert");
static_assert(EXDEV == 18, "assert");
static_assert(ENODEV == 19, "assert");
static_assert(EINVAL == 22, "assert");
static_assert(ENOSPC == 28, "assert");
static_assert(EPIPE == 32, "assert");
static_assert(ETIME_LNX != ETIME, "assert");
static_assert(ENOSR_LNX != ENOSR, "assert");
//static_assert(ECOMM_LNX != ECOMM, "assert");
static_assert(EPROTO_LNX != EPROTO, "assert");
static_assert(EOVERFLOW_LNX != EOVERFLOW, "assert");
static_assert(EILSEQ_LNX != EILSEQ, "assert");
static_assert(ECONNRESET_LNX != ECONNRESET, "assert");
//static_assert(ESHUTDOWN_LNX != ESHUTDOWN, "assert");
static_assert(ETIMEDOUT_LNX != ETIMEDOUT, "assert");
static_assert(EINPROGRESS_LNX != EINPROGRESS, "assert");
//static_assert(EREMOTEIO_LNX != EREMOTEIO, "assert");

/*
 * See:
 * <kernel>/doc/Documentation/usb/error-codes.txt
 * winddk, usb.h
 */
USBD_STATUS
to_usbd_status(int usbip_status)
{
	switch (abs(usbip_status)) {
	case 0:
		return USBD_STATUS_SUCCESS;
	case EPIPE_LNX:
		return USBD_STATUS_STALL_PID; /* USBD_STATUS_ENDPOINT_HALTED */
	case EREMOTEIO_LNX:
		return USBD_STATUS_ERROR_SHORT_TRANSFER;
	case ETIME_LNX:
		return USBD_STATUS_DEV_NOT_RESPONDING;
	case ETIMEDOUT_LNX:
		return USBD_STATUS_TIMEOUT;
	case ENOENT_LNX:
	case ECONNRESET_LNX:
		return USBD_STATUS_CANCELED;
	case EINPROGRESS_LNX:
		return USBD_STATUS_PENDING;
	case EOVERFLOW_LNX:
		return USBD_STATUS_BABBLE_DETECTED;
	case ENODEV_LNX:
	case ESHUTDOWN_LNX:
		return USBD_STATUS_DEVICE_GONE;
	case EILSEQ_LNX:
		return USBD_STATUS_CRC;
	case ECOMM_LNX:
		return USBD_STATUS_DATA_OVERRUN;
	case ENOSR_LNX:
		return USBD_STATUS_DATA_UNDERRUN;
	case ENOMEM_LNX:
		return USBD_STATUS_INSUFFICIENT_RESOURCES;
	case EPROTO_LNX:
		return USBD_STATUS_BTSTUFF; /* USBD_STATUS_INTERNAL_HC_ERROR */
	case ENOSPC_LNX:
		return USBD_STATUS_NO_BANDWIDTH;
	case EXDEV_LNX:
		return USBD_STATUS_ISOCH_REQUEST_FAILED;
	case ENXIO_LNX:
		return USBD_STATUS_INTERNAL_HC_ERROR;
	}

	return USBD_STATUS_INVALID_PARAMETER;
}

/*
 * See:
 * winddk, usb.h
 * <kernel>/doc/Documentation/usb/error-codes.txt
 */
int
to_usbip_status(USBD_STATUS status)
{
	switch (status) {
	case USBD_STATUS_SUCCESS:
		return 0;
	case USBD_STATUS_STALL_PID:
	case USBD_STATUS_ENDPOINT_HALTED:
		return -EPIPE_LNX;
	case USBD_STATUS_ERROR_SHORT_TRANSFER:
		return -EREMOTEIO_LNX;
	case USBD_STATUS_TIMEOUT:
		return -ETIMEDOUT_LNX; /* ETIME */
	case USBD_STATUS_CANCELED:
		return -ECONNRESET_LNX; /* ENOENT */
	case USBD_STATUS_PENDING:
		return -EINPROGRESS_LNX;
	case USBD_STATUS_BABBLE_DETECTED:
		return -EOVERFLOW_LNX;
	case USBD_STATUS_DEVICE_GONE:
		return -ENODEV_LNX;
	case USBD_STATUS_CRC:
		return -EILSEQ_LNX;
	case USBD_STATUS_DATA_OVERRUN:
		return -ECOMM_LNX;
	case USBD_STATUS_DATA_UNDERRUN:
		return -ENOSR_LNX;
	case USBD_STATUS_INSUFFICIENT_RESOURCES:
		return -ENOMEM_LNX;
	case USBD_STATUS_BTSTUFF:
	case USBD_STATUS_INTERNAL_HC_ERROR:
	case USBD_STATUS_HUB_INTERNAL_ERROR:
	case USBD_STATUS_DEV_NOT_RESPONDING:
		return -EPROTO_LNX;
	case USBD_STATUS_ERROR_BUSY:
		return -EBUSY_LNX;
	}

	return status < 0 ? -EINVAL_LNX : 0; /* see USBD_ERROR */
}

/*
* <linux/usb.h>, urb->transfer_flags
*/
enum {
	URB_SHORT_NOT_OK = 0x0001,
	URB_ISO_ASAP = 0x0002,
	URB_DIR_IN = 0x0200
};

 /*
 TransferFlags
 Specifies zero, one, or a combination of the following flags: 

 USBD_TRANSFER_DIRECTION_IN
 Is set to request data from a device. To transfer data to a device, this flag must be clear.  

 USBD_SHORT_TRANSFER_OK
 This flag should not be set unless USBD_TRANSFER_DIRECTION_IN is also set.
 
 USBD_START_ISO_TRANSFER_ASAP
 Causes the transfer to begin on the next frame, if no transfers have been submitted to the pipe 
 since the pipe was opened or last reset. Otherwise, the transfer begins on the first frame that 
 follows all currently queued requests for the pipe. The actual frame that the transfer begins on 
 will be adjusted for bus latency by the host controller driver. 

 For control endpoints:
 1.Direction in endpoint address or transfer flags should be ignored
 2.Direction is determined by bits of bmRequestType in the Setup packet (D7 Data Phase Transfer Direction) 
 */
ULONG
to_usbd_flags(int flags)
{
	ULONG TransferFlags = 0;

	if (flags & URB_DIR_IN) {
		TransferFlags |= USBD_TRANSFER_DIRECTION_IN;
		if (!(flags & URB_SHORT_NOT_OK)) {
			TransferFlags |= USBD_SHORT_TRANSFER_OK;
		}
	} else {
		static_assert(!USBD_TRANSFER_DIRECTION_OUT, "assert");
	}

	if (flags & URB_ISO_ASAP) {
		TransferFlags |= USBD_START_ISO_TRANSFER_ASAP;
	}

	return TransferFlags;
}

void
to_usbd_iso_descs(ULONG n_pkts, USBD_ISO_PACKET_DESCRIPTOR *usbd_iso_desc, const struct usbip_iso_packet_descriptor *iso_desc, BOOLEAN as_result)
{
	for (ULONG i = 0; i < n_pkts; ++i) {

		USBD_ISO_PACKET_DESCRIPTOR *dest = usbd_iso_desc + i;
		const struct usbip_iso_packet_descriptor *src = iso_desc + i;

		dest->Offset = src->offset;
		if (as_result) {
			dest->Length = src->actual_length;
			dest->Status = to_usbd_status(src->status);
		}
	}
}

void
to_iso_descs(ULONG n_pkts, struct usbip_iso_packet_descriptor *iso_desc, const USBD_ISO_PACKET_DESCRIPTOR *usbd_iso_desc, BOOLEAN as_result)
{
	for (ULONG i = 0; i < n_pkts; ++i) {

		struct usbip_iso_packet_descriptor *dest = iso_desc + i;
		const USBD_ISO_PACKET_DESCRIPTOR *src = usbd_iso_desc + i;

		dest->offset = src->Offset;
		if (as_result) {
			dest->actual_length = src->Length;
			dest->status = to_usbip_status(src->Status);
		}
	}
}

ULONG
get_iso_descs_len(ULONG n_pkts, const struct usbip_iso_packet_descriptor *iso_desc, BOOLEAN is_actual)
{
	ULONG len = 0;
		
	for (ULONG i = 0; i < n_pkts; ++i) {
		const struct usbip_iso_packet_descriptor *pkt = iso_desc + i;
		len += is_actual ? pkt->actual_length: pkt->length;
	}

	return len;
}

ULONG
get_usbd_iso_descs_len(ULONG n_pkts, const USBD_ISO_PACKET_DESCRIPTOR *usbd_iso_desc)
{
	ULONG len = 0;

	for (ULONG i = 0; i < n_pkts; ++i) {
		len += usbd_iso_desc[i].Length;
		
	}

	return len;
}
