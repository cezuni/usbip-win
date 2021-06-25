#include <windows.h>
#include "mssign32.h"
#include "usbip_common.h"
#include "usbip_util.h"

static BOOL
load_mssign32_lib(HMODULE	*phMod, SignerSignEx_t *pfSignerSignEx, SignerFreeSignerContext_t *pfSignerFreeSignerContext)
{
	*phMod = LoadLibrary("MSSign32.dll");
	if (*phMod == NULL) {
		dbg("cannot load mssign32.dll");
		return FALSE;
	}
	*pfSignerSignEx = (SignerSignEx_t)GetProcAddress(*phMod, "SignerSignEx");
	*pfSignerFreeSignerContext = (SignerFreeSignerContext_t)GetProcAddress(*phMod, "SignerFreeSignerContext");
	if (*pfSignerSignEx == NULL || *pfSignerFreeSignerContext == NULL) {
		dbg("cannot get functions from mssign32.dll");
		FreeLibrary(*phMod);
		return FALSE;
	}
	return TRUE;
}

static PCCERT_CONTEXT
load_cert_context(LPCSTR subject, HCERTSTORE *phCertStore)
{
	PCCERT_CONTEXT	pCertContext;
	WCHAR	*wSubject;

	*phCertStore = CertOpenStore(CERT_STORE_PROV_SYSTEM, 0, (HCRYPTPROV)NULL, CERT_SYSTEM_STORE_LOCAL_MACHINE, L"Root");
	if (*phCertStore == NULL) {
		dbg("load_cert_context: failed to open certificate store: %lx", GetLastError());
		return NULL;
	}
	wSubject = utf8_to_wchar(subject);
	pCertContext = CertFindCertificateInStore(*phCertStore, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, CERT_FIND_SUBJECT_STR, wSubject, NULL);
	free(wSubject);

	return pCertContext;
}

int
sign_file(LPCSTR subject, LPCSTR fpath)
{
	SIGNER_FILE_INFO	signerFileInfo;
	SIGNER_SUBJECT_INFO	signerSubjectInfo;
	SIGNER_CERT_STORE_INFO	signerCertStoreInfo;
	SIGNER_CERT		signerCert;
	SIGNER_SIGNATURE_INFO	signerSignatureInfo;
	CRYPT_ATTRIBUTE		cryptAttribute[2];
	CRYPT_INTEGER_BLOB	oidSpOpusInfoBlob, oidStatementTypeBlob;
	CRYPT_ATTRIBUTES_ARRAY	cryptAttributesArray;
	HCERTSTORE		hCertStore;
	BYTE	pbOidSpOpusInfo[] = SP_OPUS_INFO_DATA;
	BYTE	pbOidStatementType[] = STATEMENT_TYPE_DATA;
	DWORD	dwIndex;
	WCHAR	*wfpath;
	PCCERT_CONTEXT		pCertContext;
	PSIGNER_CONTEXT		pSignerContext = NULL;
	SignerSignEx_t			funcSignerSignEx;
	SignerFreeSignerContext_t	funcSignerFreeSignerContext;
	HRESULT	hres;
	HMODULE	hMod;
	int	ret = ERR_GENERAL;

	if (!load_mssign32_lib(&hMod, &funcSignerSignEx, &funcSignerFreeSignerContext))
		return ERR_GENERAL;

	pCertContext = load_cert_context(subject, &hCertStore);
	if (pCertContext == NULL) {
		dbg("cannot load certificate: subject: %s", subject);
		FreeLibrary(hMod);
		return ERR_NOTEXIST;
	}

	// Setup SIGNER_FILE_INFO struct
	signerFileInfo.cbSize = sizeof(SIGNER_FILE_INFO);
	wfpath = utf8_to_wchar(fpath);

	signerFileInfo.pwszFileName = wfpath;
	signerFileInfo.hFile = NULL;

	// Prepare SIGNER_SUBJECT_INFO struct
	signerSubjectInfo.cbSize = sizeof(SIGNER_SUBJECT_INFO);
	dwIndex = 0;
	signerSubjectInfo.pdwIndex = &dwIndex;
	signerSubjectInfo.dwSubjectChoice = SIGNER_SUBJECT_FILE;
	signerSubjectInfo.pSignerFileInfo = &signerFileInfo;

	// Prepare SIGNER_CERT_STORE_INFO struct
	signerCertStoreInfo.cbSize = sizeof(SIGNER_CERT_STORE_INFO);
	signerCertStoreInfo.pSigningCert = pCertContext;
	signerCertStoreInfo.dwCertPolicy = SIGNER_CERT_POLICY_CHAIN;
	signerCertStoreInfo.hCertStore = NULL;

	// Prepare SIGNER_CERT struct
	signerCert.cbSize = sizeof(SIGNER_CERT);
	signerCert.dwCertChoice = SIGNER_CERT_STORE;
	signerCert.pCertStoreInfo = &signerCertStoreInfo;
	signerCert.hwnd = NULL;

	// Prepare the additional Authenticode OIDs
	oidSpOpusInfoBlob.cbData = sizeof(pbOidSpOpusInfo);
	oidSpOpusInfoBlob.pbData = pbOidSpOpusInfo;
	oidStatementTypeBlob.cbData = sizeof(pbOidStatementType);
	oidStatementTypeBlob.pbData = pbOidStatementType;
	cryptAttribute[0].cValue = 1;
	cryptAttribute[0].rgValue = &oidSpOpusInfoBlob;
	cryptAttribute[0].pszObjId = "1.3.6.1.4.1.311.2.1.12"; // SPC_SP_OPUS_INFO_OBJID in wintrust.h
	cryptAttribute[1].cValue = 1;
	cryptAttribute[1].rgValue = &oidStatementTypeBlob;
	cryptAttribute[1].pszObjId = "1.3.6.1.4.1.311.2.1.11"; // SPC_STATEMENT_TYPE_OBJID in wintrust.h
	cryptAttributesArray.cAttr = 2;
	cryptAttributesArray.rgAttr = cryptAttribute;

	// Prepare SIGNER_SIGNATURE_INFO struct
	signerSignatureInfo.cbSize = sizeof(SIGNER_SIGNATURE_INFO);
	signerSignatureInfo.algidHash = CALG_SHA_256;
	signerSignatureInfo.dwAttrChoice = SIGNER_NO_ATTR;
	signerSignatureInfo.pAttrAuthcode = NULL;
	signerSignatureInfo.psAuthenticated = &cryptAttributesArray;
	signerSignatureInfo.psUnauthenticated = NULL;

	// Sign file with cert
	hres = funcSignerSignEx(0, &signerSubjectInfo, &signerCert, &signerSignatureInfo, NULL, NULL, NULL, NULL, &pSignerContext);
	if (hres != S_OK) {
		dbg("SignerSignEx failed. hResult #%X", hres);
		goto out;
	}
	ret = 0;
out:
	free(wfpath);
	if (pSignerContext)
		funcSignerFreeSignerContext(pSignerContext);
	CertFreeCertificateContext(pCertContext);
	CertCloseStore(hCertStore, 0);
	FreeLibrary(hMod);

	return ret;
}
