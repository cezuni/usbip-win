
#include "usbip.h"
#include "usbip_common.h"
#include "getopt.h"

#include <windows.h>
#include <setupapi.h>
#include <newdev.h>
#include <io.h>

typedef enum {
	DRIVER_ROOT,
	DRIVER_VHCI_WDM,
	DRIVER_VHCI_UDE,
} drv_type_t;

typedef struct {
	const char	*name;
	const char	*inf_filename;
	const char	*hwid_inf_section;
	const char	*hwid_inf_key;
	const char	*hwid;
	const char	*device_id;
	const char	*devclass;
} drv_info_t;

static drv_info_t	drv_infos[] = {
	{ "root", "usbip_root.inf", "Standard.NTamd64", "usbip-win VHCI Root", "USBIPWIN\\root\0", "ROOT\\USBIP\\root", "System" },
	{ "vhci(wdm)", "usbip_vhci.inf", "Standard.NTamd64", "usbip-win VHCI", "USBIPWIN\\vhci", NULL, NULL },
	{ "vhci(ude)", "usbip_vhci_ude.inf", "Standard.NTamd64", "usbip-win VHCI(ude)", "root\\vhci_ude", "ROOT\\USB\\0000", "USB" }
};

static BOOL	only_wdm, only_ude, force;

void
usbip_install_usage(void)
{
	printf(
"usage: usbip install\n"
"    install usbip VHCI drivers\n"
"    -w, --wdm    install only wdm version\n"
"    -u, --ude    install only ude version\n"
"    -f, --force  install forcefully\n"
);
}

void
usbip_uninstall_usage(void)
{
	printf(
"usage: usbip uninstall\n"
"    uninstall usbip VHCI drivers\n"
"    -w, --wdm    uninstall only wdm version\n"
"    -u, --ude    uninstall only ude version\n"
"    -f, --force  uninstall forcefully\n"
);
}

static char *
get_abs_path(const char *fname)
{
	char	fpathbuf[MAX_PATH];
	char	exe_path[MAX_PATH];
	char	*sep;

	if (GetModuleFileName(NULL, exe_path, MAX_PATH) == 0) {
		dbg("failed to get a executable path");
		return NULL;
	}
	if ((sep = strrchr(exe_path, '\\')) == NULL) {
		dbg("invalid executanle path: %s", exe_path);
		return NULL;
	}
	*sep = '\0';
	snprintf(fpathbuf, MAX_PATH, "%s\\%s", exe_path, fname);
	return _strdup(fpathbuf);
}

static BOOL
is_exist_fname(const char *fname)
{
	char	*fpath;
	BOOL	exist = FALSE;

	fpath = get_abs_path(fname);
	if (_access(fpath, 0) == 0)
		exist = TRUE;
	free(fpath);
	return exist;
}

static char *
get_source_inf_path(drv_info_t *pinfo)
{
	return get_abs_path(pinfo->inf_filename);
}

static BOOL
is_driver_oem_inf(drv_info_t *pinfo, const char *inf_path)
{
	HINF	hinf;
	INFCONTEXT	ctx;
	BOOL	found = FALSE;

	hinf = SetupOpenInfFile(inf_path, NULL, INF_STYLE_WIN4, NULL);
	if (hinf == INVALID_HANDLE_VALUE) {
		dbg("cannot open inf file: %s", inf_path);
		return FALSE;
	}

	if (SetupFindFirstLine(hinf, pinfo->hwid_inf_section, pinfo->hwid_inf_key, &ctx)) {
		char	hwid[32];
		DWORD	reqsize;

		if (SetupGetStringField(&ctx, 2, hwid, 32, &reqsize)) {
			if (_stricmp(hwid, pinfo->hwid) == 0)
				found = TRUE;
		}
	}

	SetupCloseInfFile(hinf);
	return found;
}

static char *
get_oem_inf_pattern(void)
{
	char	oem_inf_pattern[MAX_PATH];
	char	windir[MAX_PATH];

	if (GetWindowsDirectory(windir, MAX_PATH) == 0)
		return NULL;
	snprintf(oem_inf_pattern, MAX_PATH, "%s\\inf\\oem*.inf", windir);
	return _strdup(oem_inf_pattern);
}

static char *
get_oem_inf(drv_info_t *pinfo)
{
	char	*oem_inf_pattern;
	HANDLE	hFind;
	WIN32_FIND_DATA	wfd;
	char	*oem_inf_name = NULL;

	oem_inf_pattern = get_oem_inf_pattern();
	if (oem_inf_pattern == NULL) {
		dbg("failed to get oem inf pattern");
		return NULL;
	}

	hFind = FindFirstFile(oem_inf_pattern, &wfd);
	free(oem_inf_pattern);

	if (hFind == INVALID_HANDLE_VALUE) {
		dbg("failed to get oem inf: 0x%lx", GetLastError());
		return NULL;
	}

	do {
		if (is_driver_oem_inf(pinfo, wfd.cFileName)) {
			oem_inf_name = _strdup(wfd.cFileName);
			break;
		}
	} while (FindNextFile(hFind, &wfd));

	FindClose(hFind);

	return oem_inf_name;
}

static BOOL
is_exist_driver_package(drv_info_t *pinfo)
{
	char* oem_inf_name;

	oem_inf_name = get_oem_inf(pinfo);
	if (oem_inf_name == NULL)
		return FALSE;
	return TRUE;
}

static BOOL
uninstall_driver_package(drv_info_t *pinfo)
{
	char *oem_inf_name;

	oem_inf_name = get_oem_inf(pinfo);
	if (oem_inf_name == NULL)
		return FALSE;

	if (!SetupUninstallOEMInf(oem_inf_name, 0, NULL)) {
		dbg("failed to uninstall a old %s driver package: 0x%lx", pinfo->name, GetLastError());
		free(oem_inf_name);
		return FALSE;
	}
	free(oem_inf_name);

	return TRUE;
}

extern BOOL has_certificate(LPCSTR subject);

static int
install_driver_package(drv_info_t *pinfo)
{
	char	*inf_path;
	int	res = 0;

	if (!has_certificate("USBIP Test")) {
		dbg("USBIP Test certificate not found");
		return ERR_CERTIFICATE;
	}

	inf_path = get_source_inf_path(pinfo);
	if (inf_path == NULL)
		return ERR_GENERAL;

	if (!SetupCopyOEMInf(inf_path, NULL, SPOST_PATH, 0, NULL, 0, NULL, NULL)) {
		DWORD	err = GetLastError();
		switch (err) {
		case ERROR_FILE_NOT_FOUND:
			res = ERR_NOTEXIST;
			dbg("usbip_%s.inf or usbip_vhci.sys file not found", pinfo->name);
			break;
		case ERROR_ACCESS_DENIED:
			res = ERR_ACCESS;
			dbg("failed to install %s driver package: access denied", pinfo->name);
			break;
		default:
			res = ERR_GENERAL;
			dbg("failed to install %s driver package: err:%lx", pinfo->name, err);
			break;
		}
	}
	free(inf_path);
	return res;
}

static BOOL
is_exist_device(drv_info_t *pinfo)
{
	HDEVINFO	hdevinfoset;
	SP_DEVINFO_DATA	devinfo;
	BOOL	exist = FALSE;

	hdevinfoset = SetupDiCreateDeviceInfoList(NULL, NULL);
	if (hdevinfoset == INVALID_HANDLE_VALUE) {
		return FALSE;
	}

	memset(&devinfo, 0, sizeof(SP_DEVINFO_DATA));
	devinfo.cbSize = sizeof(SP_DEVINFO_DATA);
	if (SetupDiOpenDeviceInfo(hdevinfoset, pinfo->device_id, NULL, 0, &devinfo))
		exist = TRUE;
	SetupDiDestroyDeviceInfoList(hdevinfoset);
	return exist;
}

static BOOL
uninstall_device(drv_info_t *pinfo)
{
	HDEVINFO	hdevinfoset;
	SP_DEVINFO_DATA	devinfo;

	hdevinfoset = SetupDiCreateDeviceInfoList(NULL, NULL);
	if (hdevinfoset == INVALID_HANDLE_VALUE) {
		dbg("failed to create devinfoset");
		return FALSE;
	}

	memset(&devinfo, 0, sizeof(SP_DEVINFO_DATA));
	devinfo.cbSize = sizeof(SP_DEVINFO_DATA);
	if (!SetupDiOpenDeviceInfo(hdevinfoset, pinfo->device_id, NULL, 0, &devinfo)) {
		SetupDiDestroyDeviceInfoList(hdevinfoset);
		return FALSE;
	}

	if (!DiUninstallDevice(NULL, hdevinfoset, &devinfo, 0, NULL)) {
		dbg("cannot uninstall device: error: 0x%lx", GetLastError());
		SetupDiDestroyDeviceInfoList(hdevinfoset);
		return FALSE;
	}
	SetupDiDestroyDeviceInfoList(hdevinfoset);
	return TRUE;
}

static int
create_devinfo(drv_info_t *pinfo, LPGUID pguid_system, HDEVINFO hdevinfoset, PSP_DEVINFO_DATA pdevinfo)
{
	DWORD	err;

	memset(pdevinfo, 0, sizeof(SP_DEVINFO_DATA));
	pdevinfo->cbSize = sizeof(SP_DEVINFO_DATA);
	if (SetupDiCreateDeviceInfo(hdevinfoset, pinfo->device_id, pguid_system, NULL, NULL, 0, pdevinfo))
		return 0;

	err = GetLastError();
	switch (err) {
	case ERROR_ACCESS_DENIED:
		dbg("failed to create device info: access denied");
		return ERR_ACCESS;
	default:
		dbg("failed to create a device info: 0x%lx", err);
		return ERR_GENERAL;
	}
}

static BOOL
setup_guid(LPCTSTR classname, LPGUID pguid)
{
	DWORD	reqsize;

	if (!SetupDiClassGuidsFromName(classname, pguid, 1, &reqsize)) {
		dbg("failed to get System setup class");
		return FALSE;
	}
	return TRUE;
}

static int
install_device(drv_info_t *pinfo)
{
	HDEVINFO	hdevinfoset;
	SP_DEVINFO_DATA	devinfo;
	GUID	guid_system;
	int	ret;

	setup_guid(pinfo->devclass, &guid_system);

	hdevinfoset = SetupDiGetClassDevs(&guid_system, NULL, NULL, 0);
	if (hdevinfoset == INVALID_HANDLE_VALUE) {
		dbg("failed to create devinfoset");
		return ERR_GENERAL;
	}

	ret = create_devinfo(pinfo, &guid_system, hdevinfoset, &devinfo);
	if (ret) {
		SetupDiDestroyDeviceInfoList(hdevinfoset);
		return ret;
	}

	if (!SetupDiSetDeviceRegistryProperty(hdevinfoset, &devinfo, SPDRP_HARDWAREID, pinfo->hwid, (DWORD)(strlen(pinfo->hwid) + 2))) {
		dbg("failed to set hw id: 0x%lx", GetLastError());
		SetupDiDestroyDeviceInfoList(hdevinfoset);
		return ERR_GENERAL;
	}
	if (!SetupDiCallClassInstaller(DIF_REGISTERDEVICE, hdevinfoset, &devinfo)) {
		dbg("failed to register: 0x%lx", GetLastError());
		SetupDiDestroyDeviceInfoList(hdevinfoset);
		return ERR_GENERAL;
	}
	if (!DiInstallDevice(NULL, hdevinfoset, &devinfo, NULL, 0, NULL)) {
		dbg("failed to install: 0x%lx", GetLastError());
		SetupDiDestroyDeviceInfoList(hdevinfoset);
		return ERR_GENERAL;
	}
	SetupDiDestroyDeviceInfoList(hdevinfoset);
	return 0;
}

static int
install_vhci(int type)
{
	drv_info_t	*pinfo = &drv_infos[type];
	int	ret;

	if (is_exist_device(pinfo)) {
		if (force)
			return 0;
		err("%s driver already exist", pinfo->name);
		return 3;
	}
	if (is_exist_driver_package(pinfo)) {
		if (force)
			uninstall_driver_package(pinfo);
	}
	ret = install_driver_package(pinfo);
	if (ret < 0) {
		switch (ret) {
		case ERR_ACCESS:
			err("access denied: make sure you are running as administrator");
			break;
		case ERR_CERTIFICATE:
			err("\"USBIP Test\" certificate not found. Please install first!");
			break;
		default:
			err("cannot install %s driver package", pinfo->name);
		}
		return 2;
	}

	ret = install_device(pinfo);
	if (ret < 0) {
		switch (ret) {
		case ERR_ACCESS:
			err("access denied: make sure you are running as administrator");
			break;
		default:
			err("cannot install %s device", pinfo->name);
			break;
		}
		return 4;
	}

	return 0;
}

static BOOL
check_files(const char *fnames[], BOOL verbose)
{
	int	i;

	for (i = 0; fnames[i]; i++) {
		if (!is_exist_fname(fnames[i])) {
			if (verbose)
				err("%s: file not found", fnames[i]);
			return FALSE;
		}
	}
	return TRUE;
}

static BOOL
check_files_wdm(BOOL verbose)
{
	const char	*fnames[] = { "usbip_vhci.sys", "usbip_vhci.inf", "usbip_vhci.cat", "usbip_root.inf", NULL };

	return check_files(fnames, verbose);
}

static BOOL
check_files_ude(BOOL verbose)
{
	const char	*fnames[] = { "usbip_vhci_ude.sys", "usbip_vhci_ude.inf", "usbip_vhci_ude.cat", NULL };

	return check_files(fnames, verbose);
}

static int
install_vhci_wdm(void)
{
	int	ret;

	if (!check_files_wdm(TRUE)) {
		err("cannot install vhci(wdm) driver");
		return 2;
	}
	ret = install_driver_package(&drv_infos[DRIVER_VHCI_WDM]);
	if (ret < 0) {
		switch (ret) {
		case ERR_ACCESS:
			err("access denied: make sure you are running as administrator");
			break;
		default:
			err("cannot install vhci driver package");
			break;
		}
		return 5;
	}
	ret = install_vhci(DRIVER_ROOT);
	if (ret != 0)
		return ret;

	info("vhci(wdm) driver installed successfully");
	return 0;
}

static int
install_vhci_ude(void)
{
	int	ret;

	if (!check_files_ude(TRUE)) {
		err("cannot install vhci(ude) driver");
		return 2;
	}
	ret = install_vhci(DRIVER_VHCI_UDE);
	if (ret == 0) {
		info("vhci(ude) driver installed successfully");
	}
	return ret;
}

static int
install_vhci_both(void)
{
	if (!check_files_ude(FALSE)) {
		if (!check_files_wdm(TRUE))
			err("cannot install vhci driver packages");
		return install_vhci_wdm();
	}
	return install_vhci_ude();
}

static int
uninstall_vhci(int type)
{
	drv_info_t	*pinfo = &drv_infos[type];

	if (!is_exist_device(pinfo)) {
		if (!is_exist_driver_package(pinfo)) {
			if (force)
				return 0;
			err("no %s driver found", pinfo->name);
			return 3;
		}
	}
	else {
		if (!uninstall_device(pinfo)) {
			return 2;
		}
	}

	if (!uninstall_driver_package(pinfo)) {
		err("cannot uninstall %s driver package", pinfo->name);
		return 4;
	}
	return 0;
}

static int
uninstall_vhci_wdm(void)
{
	int	ret;

	ret = uninstall_vhci(DRIVER_ROOT);
	if (ret != 0)
		return ret;
	if (!uninstall_driver_package(&drv_infos[DRIVER_VHCI_WDM])) {
		err("cannot uninstall vhci(wdm) driver package");
		return 5;
	}

	info("vhci(wdm) drivers uninstalled");
	return 0;
}

static int
uninstall_vhci_ude(void)
{
	int	ret;

	ret = uninstall_vhci(DRIVER_VHCI_UDE);
	if (ret == 0) {
		info("vhci(ude) driver uninstalled");
	}
	return ret;
}

static int
uninstall_vhci_both(void)
{
	int	ret;

	if (!is_exist_driver_package(&drv_infos[DRIVER_VHCI_UDE])) {
		if (!is_exist_driver_package(&drv_infos[DRIVER_VHCI_WDM])) {
			if (force)
				return 0;
			err("no vhci driver found");
			return 5;
		}
		return uninstall_vhci_wdm();
	}
	if (!is_exist_driver_package(&drv_infos[DRIVER_VHCI_WDM])) {
		return uninstall_vhci_ude();
	}
	ret = uninstall_vhci_wdm();
	if (ret != 0)
		return ret;
	return uninstall_vhci_ude();
}

static int
parse_opts(int argc, char *argv[])
{
	static const struct option opts[] = {
		{ "wdm", required_argument, NULL, 'w' },
		{ "ude", required_argument, NULL, 'u' },
		{ "force", required_argument, NULL, 'f' },
		{ NULL, 0, NULL, 0 }
	};

	for (;;) {
		int	opt = getopt_long(argc, argv, "wuf", opts, NULL);

		if (opt == -1)
			break;

		switch (opt) {
		case 'w':
			only_wdm = TRUE;
			break;
		case 'u':
			only_ude = TRUE;
			break;
		case 'f':
			force = TRUE;
			break;
		default:
			return FALSE;
		}
	}
	if (only_wdm && only_ude)
		only_wdm = only_ude = FALSE;
	return TRUE;
}

int
usbip_install(int argc, char *argv[])
{
	if (!parse_opts(argc, argv)) {
		usbip_install_usage();
		return 1;
	}
	if (only_wdm)
		return install_vhci_wdm();
	if (only_ude)
		return install_vhci_ude();
	return install_vhci_both();
}

int
usbip_uninstall(int argc, char *argv[])
{
	if (!parse_opts(argc, argv)) {
		usbip_uninstall_usage();
		return 1;
	}
	if (only_wdm)
		return uninstall_vhci_wdm();
	if (only_ude)
		return uninstall_vhci_ude();
	return uninstall_vhci_both();
}