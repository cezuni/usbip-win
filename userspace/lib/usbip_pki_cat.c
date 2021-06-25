#include <windows.h>
#include <wincrypt.h>
#include <mscat.h>
#include "usbip_common.h"
#include "usbip_util.h"

#define SHA1_HASH_LENGTH	20
#define SPC_UUID_LENGTH		16
#define SPC_FILE_LINK_CHOICE	3

#define OBJID_SPC_PE_IMAGE	"1.3.6.1.4.1.311.2.1.15"
#define OBJID_SPC_CAB_DATA	"1.3.6.1.4.1.311.2.1.25"

#define ATTR_FLAGS	(CRYPTCAT_ATTR_AUTHENTICATED | CRYPTCAT_ATTR_NAMEASCII | CRYPTCAT_ATTR_DATAASCII)

typedef BYTE	SPC_UUID[SPC_UUID_LENGTH];

typedef struct _SPC_SERIALIZED_OBJECT {
	SPC_UUID ClassId;
	CRYPT_DATA_BLOB SerializedData;
} SPC_SERIALIZED_OBJECT, *PSPC_SERIALIZED_OBJECT;

typedef struct SPC_LINK_ {
	DWORD dwLinkChoice;
	union {
		LPWSTR pwszUrl;
		SPC_SERIALIZED_OBJECT Moniker;
		LPWSTR pwszFile;
	};
} SPC_LINK, *PSPC_LINK;

typedef struct _SPC_PE_IMAGE_DATA {
	CRYPT_BIT_BLOB Flags;
	PSPC_LINK pFile;
} SPC_PE_IMAGE_DATA, *PSPC_PE_IMAGE_DATA;

static void
convert_to_hashstr(PBYTE pbHash, LPWSTR wstrHash)
{
	int	i;
	for (i = 0; i < SHA1_HASH_LENGTH; i++) {
		_snwprintf_s(&wstrHash[i * 2], 3, _TRUNCATE, L"%02X", pbHash[i]);
	}
}

static BOOL
calc_hash(LPCSTR fpath, PBYTE pbHash)
{
	HANDLE	hFile;
	DWORD	cbHash = SHA1_HASH_LENGTH;
	LPWSTR wszFilePath = NULL;

	hFile = CreateFile(fpath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		dbg("calc_hash: path not found: %s", fpath);
		return FALSE;
	}
	if ((!CryptCATAdminCalcHashFromFileHandle(hFile, &cbHash, pbHash, 0))) {
		dbg("calc_hash: failed to hash: %s", fpath);
		CloseHandle(hFile);
		return FALSE;
	}
	CloseHandle(hFile);

	return TRUE;
}

static BOOL
add_file_hash(HANDLE hCat, LPCSTR path, LPCSTR fname, BOOL isPEType)
{
	const GUID	inf_guid = { 0xDE351A42, 0x8E59, 0x11D0,{ 0x8C, 0x47, 0x00, 0xC0, 0x4F, 0xC2, 0x95, 0xEE } };
	const GUID	pe_guid = { 0xC689AAB8, 0x8E78, 0x11D0,{ 0x8C, 0x47, 0x00, 0xC0, 0x4F, 0xC2, 0x95, 0xEE } };
	LPCWSTR		wszOSAttr = L"2:5.1,2:5.2,2:6.0,2:6.1";
	CRYPTCATMEMBER	*pCatMember;
	WCHAR	wstrHash[2 * SHA1_HASH_LENGTH + 1], *wfname;
	BYTE	pbHash[SHA1_HASH_LENGTH];
	char	*fpath;
	BYTE	pbEncoded[64];
	DWORD	cbEncoded;
	SPC_LINK	sSPCLink;
	SIP_INDIRECT_DATA	sSIPData;

	asprintf(&fpath, "%s\\%s", path, fname);
	if (!calc_hash(fpath, pbHash)) {
		free(fpath);
		return FALSE;
	}

	convert_to_hashstr(pbHash, wstrHash);

	sSPCLink.dwLinkChoice = SPC_FILE_LINK_CHOICE;
	sSPCLink.pwszUrl = L"<<<Obsolete>>>";
	cbEncoded = sizeof(pbEncoded);

	if (isPEType) {
		SPC_PE_IMAGE_DATA	sSPCImageData;
		const BYTE fImageData = 0xA0;		// Flags used for the SPC_PE_IMAGE_DATA "<<<Obsolete>>>" link

		sSPCImageData.Flags.cbData = 1;
		sSPCImageData.Flags.cUnusedBits = 0;
		sSPCImageData.Flags.pbData = (BYTE*)&fImageData;
		sSPCImageData.pFile = &sSPCLink;
		if (!CryptEncodeObject(X509_ASN_ENCODING, OBJID_SPC_PE_IMAGE, &sSPCImageData, pbEncoded, &cbEncoded)) {
			dbg("failed to encode SPC for pe image: %s", fname);
			return FALSE;
		}
	}
	else {
		if (!CryptEncodeObject(X509_ASN_ENCODING, OBJID_SPC_CAB_DATA, &sSPCLink, pbEncoded, &cbEncoded)) {
			dbg("failed to encode SPC for data: %s", fname);
			return FALSE;
		}
	}
	// Populate the SHA1 Hash OID
	sSIPData.Data.pszObjId = (isPEType) ? OBJID_SPC_PE_IMAGE: OBJID_SPC_CAB_DATA;
	sSIPData.Data.Value.cbData = cbEncoded;
	sSIPData.Data.Value.pbData = pbEncoded;
	sSIPData.DigestAlgorithm.pszObjId = szOID_OIWSEC_sha1;
	sSIPData.DigestAlgorithm.Parameters.cbData = 0;
	sSIPData.Digest.cbData = SHA1_HASH_LENGTH;
	sSIPData.Digest.pbData = pbHash;

	pCatMember = CryptCATPutMemberInfo(hCat, NULL, wstrHash, (GUID*)(isPEType ? &pe_guid : &inf_guid), 0x200, sizeof(sSIPData), (BYTE*)&sSIPData);
	if (pCatMember == NULL) {
		dbg("failed to add cat entry: %s", fname);
		return FALSE;
	}

	wfname = utf8_to_wchar(fname);
	// Add the "File" and "OSAttr" attributes to the newly created member
	if (CryptCATPutAttrInfo(hCat, pCatMember, L"File", ATTR_FLAGS, 2 * ((DWORD)wcslen(wfname) + 1), (BYTE*)wfname) == NULL ||
		CryptCATPutAttrInfo(hCat, pCatMember, L"OSAttr", ATTR_FLAGS, 2 * ((DWORD)wcslen(wszOSAttr) + 1), (BYTE*)wszOSAttr) == NULL) {
		free(wfname);
		dbg("unable to create attributes: %s", fname);
		return FALSE;
	}
	free(wfname);
	return TRUE;
}

BOOL
build_cat(LPCSTR path, LPCSTR catname, LPCSTR hwid)
{
	HCRYPTPROV	hProv;
	HANDLE		hCat;
	LPWSTR		wpath_cat, whwid;
	char		*path_cat;
	BOOL		res = FALSE;
	LPCWSTR		wOS = L"7_X86,7_X64,8_X86,8_X64,8_ARM,10_X86,10_X64,10_ARM";

	if (!CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
		dbg("unable to acquire crypt context for cat creation");
		return FALSE;
	}

	asprintf(&path_cat, "%s\\%s", path, catname);
	wpath_cat = utf8_to_wchar(path_cat);
	free(path_cat);
	hCat = CryptCATOpen(wpath_cat, CRYPTCAT_OPEN_CREATENEW, hProv, 0, 0);
	free(wpath_cat);
	if (hCat == INVALID_HANDLE_VALUE) {
		dbg("unable to create cat: %s", path);
		CryptReleaseContext(hProv, 0);
		return FALSE;
	}

	whwid = utf8_to_wchar(hwid);
	if (CryptCATPutCatAttrInfo(hCat, L"HWID1", CRYPTCAT_ATTR_AUTHENTICATED | CRYPTCAT_ATTR_NAMEASCII | CRYPTCAT_ATTR_DATAASCII,
		2 * ((DWORD)wcslen(whwid) + 1), (BYTE*)whwid) == NULL) {
		dbg("failed to set HWID1 cat attribute");
		free(whwid);
		goto out;
	}
	free(whwid);

	if (CryptCATPutCatAttrInfo(hCat, L"OS", CRYPTCAT_ATTR_AUTHENTICATED | CRYPTCAT_ATTR_NAMEASCII | CRYPTCAT_ATTR_DATAASCII,
		2 * ((DWORD)wcslen(wOS) + 1), (BYTE*)wOS) == NULL) {
		dbg("failed to set OS cat attribute");
		goto out;
	}

	add_file_hash(hCat, path, "usbip_stub.sys", TRUE);
	add_file_hash(hCat, path, "usbip_stub.inf", FALSE);

	if (!CryptCATPersistStore(hCat)) {
		dbg("unable to sort cat: %s", path);
		goto out;
	}
	res = TRUE;
out:
	CryptCATClose(hCat);
	CryptReleaseContext(hProv, 0);

	return res;
}
