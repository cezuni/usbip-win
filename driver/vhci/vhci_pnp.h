#pragma once

#include "basetype.h"
#include "vhci_dev.h"

#define HWID_ROOT	L"USBIPWIN\\root"
#define HWID_VHCI	L"USBIPWIN\\vhci"

#define VHUB_PREFIX	L"USB\\ROOT_HUB"
#define VHUB_VID	L"1209"
#define VHUB_PID	L"8250"
#define VHUB_REV	L"0000"

#define HWID_VHUB \
	VHUB_PREFIX \
	L"&VID_" VHUB_VID \
	L"&PID_" VHUB_PID \
	L"&REV_" VHUB_REV

#define INITIALIZE_PNP_STATE(_Data_)    \
        (_Data_)->common.DevicePnPState =  NotStarted;\
        (_Data_)->common.PreviousPnPState = NotStarted;

#define SET_NEW_PNP_STATE(vdev, _state_) \
        do { (vdev)->PreviousPnPState = (vdev)->DevicePnPState;\
        (vdev)->DevicePnPState = (_state_); } while (0)

#define RESTORE_PREVIOUS_PNP_STATE(vdev)   \
        do { (vdev)->DevicePnPState = (vdev)->PreviousPnPState; } while (0)

extern PAGEABLE NTSTATUS vhci_unplug_port(pvhci_dev_t vhci, CHAR port);
