#include "usbip_setupdi.h"
#include "usbip_stub.h"
#include "usbip_util.h"
#include "usbip_common.h"

#include <stdlib.h>
#include <stdio.h>
#include <newdev.h>

char *get_dev_property(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data, DWORD prop);

BOOL build_cat(const char *path, const char *catname, const char *hwid);
int sign_file(LPCSTR subject, LPCSTR fpath);

BOOL
is_service_usbip_stub(HDEVINFO dev_info, SP_DEVINFO_DATA *dev_info_data)
{
	char	*svcname;
	BOOL	res;

	svcname = get_dev_property(dev_info, dev_info_data, SPDRP_SERVICE);
	if (svcname == NULL)
		return FALSE;
	res = _stricmp(svcname, STUB_DRIVER_SVCNAME) == 0 ? TRUE: FALSE;
	free(svcname);
	return res;
}

static void
copy_file(const char *fname, const char *path_drvpkg)
{
	char	*path_src, *path_dst;
	char	*path_mod;

	path_mod = get_module_dir();
	if (path_mod == NULL) {
		return;
	}
	asprintf(&path_src, "%s\\%s", path_mod, fname);
	free(path_mod);
	asprintf(&path_dst, "%s\\%s", path_drvpkg, fname);

	CopyFile(path_src, path_dst, TRUE);
	free(path_src);
	free(path_dst);
}

static void
translate_inf(const char *id_hw, FILE *in, FILE *out)
{
	char	buf[4096];
	char	*line;

	while ((line = fgets(buf, 4096, in))) {
		char	*mark;

		mark = strstr(line, "%hwid%");
		if (mark) {
			strcpy_s(mark, 4096 - (mark - buf), id_hw);
		}
		fwrite(line, strlen(line), 1, out);
	}
}

static void
copy_stub_inf(const char *id_hw, const char *path_drvpkg)
{
	char	*path_inx, *path_dst;
	char	*path_mod;
	FILE	*in, *out;
	errno_t	err;

	path_mod = get_module_dir();
	if (path_mod == NULL)
		return;
	asprintf(&path_inx, "%s\\usbip_stub.inx", path_mod);
	free(path_mod);

	err = fopen_s(&in, path_inx, "r");
	free(path_inx);
	if (err != 0) {
		return;
	}
	asprintf(&path_dst, "%s\\usbip_stub.inf", path_drvpkg);
	err = fopen_s(&out, path_dst, "w");
	free(path_dst);
	if (err != 0) {
		fclose(in);
		return;
	}

	translate_inf(id_hw, in, out);
	fclose(in);
	fclose(out);
}

static void
remove_dir_all(const char *path_dir)
{
	char	*fpat;
	WIN32_FIND_DATA	wfd;
	HANDLE	hfs;

	asprintf(&fpat, "%s\\*", path_dir);
	hfs = FindFirstFile(fpat, &wfd);
	free(fpat);
	if (hfs != INVALID_HANDLE_VALUE) {
		do {
			if (*wfd.cFileName != '.') {
				char	*fpath;
				asprintf(&fpath, "%s\\%s", path_dir, wfd.cFileName);
				DeleteFile(fpath);
				free(fpath);
			}
		} while (FindNextFile(hfs, &wfd));

		FindClose(hfs);
	}
	RemoveDirectory(path_dir);
}

static BOOL
get_temp_drvpkg_path(char path_drvpkg[])
{
	char	tempdir[MAX_PATH + 1];

	if (GetTempPath(MAX_PATH + 1, tempdir) == 0)
		return FALSE;
	if (GetTempFileName(tempdir, "stub", 0, path_drvpkg) > 0) {
		DeleteFile(path_drvpkg);
		if (CreateDirectory(path_drvpkg, NULL))
			return TRUE;
	}
	else
		DeleteFile(path_drvpkg);
	return FALSE;
}

static int
apply_stub_fdo(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data)
{
	char	path_drvpkg[MAX_PATH + 1];
	char	*id_hw, *path_cat;
	char	*path_inf;
	BOOL	reboot_required;
	int	ret;

	id_hw = get_id_hw(dev_info, pdev_info_data);
	if (id_hw == NULL)
		return ERR_GENERAL;
	if (!get_temp_drvpkg_path(path_drvpkg)) {
		free(id_hw);
		return ERR_GENERAL;
	}
	copy_file("usbip_stub.sys", path_drvpkg);
	copy_stub_inf(id_hw, path_drvpkg);

	if (!build_cat(path_drvpkg, "usbip_stub.cat", id_hw)) {
		remove_dir_all(path_drvpkg);
		free(id_hw);
		return ERR_GENERAL;
	}

	asprintf(&path_cat, "%s\\usbip_stub.cat", path_drvpkg);
	if ((ret = sign_file("USBIP Test", path_cat)) < 0) {
		remove_dir_all(path_drvpkg);
		free(path_cat);
		free(id_hw);
		if (ret == ERR_NOTEXIST)
			return ERR_CERTIFICATE;
		return ERR_GENERAL;
	}

	free(path_cat);

	/* update driver */
	asprintf(&path_inf, "%s\\usbip_stub.inf", path_drvpkg);
	if (!UpdateDriverForPlugAndPlayDevicesA(NULL, id_hw, path_inf, INSTALLFLAG_NONINTERACTIVE | INSTALLFLAG_FORCE, &reboot_required)) {
		DWORD	err = GetLastError();
		dbg("failed to update driver %s ; %s ; errorcode: 0x%lx", path_inf, id_hw, err);
		free(path_inf);
		free(id_hw);
		remove_dir_all(path_drvpkg);
		if (err == 0xe0000242) {
			/* USBIP Test certificate is not installed at trusted publisher */
			return ERR_CERTIFICATE;
		}
		return ERR_GENERAL;
	}
	free(path_inf);
	free(id_hw);

	remove_dir_all(path_drvpkg);

	return 0;
}

static BOOL
rollback_driver(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data)
{
	BOOL	needReboot;

	if (!DiRollbackDriver(dev_info, pdev_info_data, NULL, ROLLBACK_FLAG_NO_UI, &needReboot)) {
		dbg("failed to rollback driver: %lx", GetLastError());
		return FALSE;
	}
	return TRUE;
}

static int
walker_attach(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data, devno_t devno, void *ctx)
{
	devno_t	*pdevno = (devno_t *)ctx;

	if (devno == *pdevno) {
		int	ret = apply_stub_fdo(dev_info, pdev_info_data);
		if (ret < 0)
			return ret;
		return 1;
	}
	return 0;
}

int
attach_stub_driver(devno_t devno)
{
	int	ret;

	ret = traverse_usbdevs(walker_attach, TRUE, &devno);
	switch (ret) {
	case 0:
		return ERR_NOTEXIST;
	case 1:
		return 0;
	case ERR_CERTIFICATE:
		return ERR_CERTIFICATE;
	default:
		return ERR_GENERAL;
	}
}

static int
walker_detach(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data, devno_t devno, void *ctx)
{
	devno_t	*pdevno = (devno_t *)ctx;

	if (devno == *pdevno) {
		if (!rollback_driver(dev_info, pdev_info_data))
			return ERR_GENERAL;
		return 1;
	}
	return 0;
}

int
detach_stub_driver(devno_t devno)
{
	int	ret;

	ret = traverse_usbdevs(walker_detach, TRUE, &devno);
	if (ret == 1)
		return 0;
	if (ret == 0)
		return ERR_NOTEXIST;
	return ERR_GENERAL;
}
