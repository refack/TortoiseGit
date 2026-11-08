module;
#include "git2/sys/errors.h"
#include "Utils/CertificateValidationHelper.h"
#include "ProtoModules/UserPassword.h"
#include "ProtoModules/CheckCertificateDlg.h"
#include "ProtoModules/IconExtractor.h"

export module AppUtils.GitEnv;
import TortoiseProc;

using cert_t = std::vector<char>;
static cert_t last_accepted_cert{};

constexpr static boolean cert_t_cmp(const git_cert_x509* cert) {
	const std::span received{ static_cast<const char*>(cert->data), cert->len };
	return std::ranges::equal(last_accepted_cert, received);
}

constexpr static void cert_t_set(const git_cert_x509* cert) {
	last_accepted_cert.assign(static_cast<const char*>(cert->data), static_cast<const char*>(cert->data) + cert->len);
}

/**
 * \ingroup TortoiseProc
 * The git installation and repository a process works against: libgit2's credential
 * and certificate callbacks, the git.exe version, the log output encoding, and the
 * folder icon of a bare repository.
 */
export namespace CAppUtils {

int Git2GetUserPassword(git_credential **out, const char *url, const char *username_from_url, unsigned int /*allowed_types*/, void * /*payload*/)
{
	CUserPassword dlg;
	dlg.m_URL = CUnicodeUtils::GetUnicode<CString>(url);
	if (username_from_url)
		dlg.m_UserName = CUnicodeUtils::GetUnicode<CString>(username_from_url);

	if (dlg.DoModal() == IDOK)
		return git_credential_userpass_plaintext_new(out, CUnicodeUtils::GetUTF8<CStringA>(dlg.m_UserName), dlg.m_passwordA);

	git_error_set_str(GIT_ERROR_NONE, "User cancelled.");
	return GIT_EUSER;
}

int Git2CertificateCheck(git_cert* base_cert, int /*valid*/, const char* host, void* /*payload*/)
{
	if (base_cert->cert_type == GIT_CERT_X509)
	{
		auto cert = reinterpret_cast<git_cert_x509*>(base_cert);

		if (cert_t_cmp(cert))
			return 0;

		PCCERT_CONTEXT pServerCert = CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, static_cast<BYTE*>(cert->data), static_cast<DWORD>(cert->len));
		SCOPE_EXIT { CertFreeCertificateContext(pServerCert); };

		DWORD verificationError = VerifyServerCertificate(pServerCert, CUnicodeUtils::GetUnicode<CString>(host).GetBuffer(), 0);
		if (!verificationError)
		{
			cert_t_set(cert);
			return 0;
		}

		CString servernameInCert;
		CertGetNameString(pServerCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, CStrBuf(servernameInCert, 128), 128);

		CString issuer;
		CertGetNameString(pServerCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, CERT_NAME_ISSUER_FLAG, nullptr, CStrBuf(issuer, 128), 128);

		CCheckCertificateDlg dlg;
		dlg.cert = cert;
		dlg.m_sCertificateCN = servernameInCert;
		dlg.m_sCertificateIssuer = issuer;
		dlg.m_sHostname = CUnicodeUtils::GetUnicode<CString>(host);
		dlg.m_sError = (CFormatMessageWrapper(verificationError)).c_str();
		if (dlg.DoModal() == IDOK)
		{
			cert_t_set(cert);
			return 0;
		}
	}
	return GIT_ECERTIFICATE;
}



void SetupBareRepoIcon(const CString& path)
{
	if (PathFileExists(path + L"\\Desktop.ini"))
		return;

	// create a desktop.ini file which sets our own icon for the repo folder
	// we extract the icon to use from the resources and write it to disk
	// so even those who don't have TGit installed can benefit from it.
	CIconExtractor gitIconResource;
	if (gitIconResource.ExtractIcon(nullptr, MAKEINTRESOURCE(IDI_GITFOLDER), path + L"\\git.ico") == 0)
	{
		CAutoFile hFile = ::CreateFile(path + L"\\Desktop.ini", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN, nullptr);
		if (hFile)
		{
			DWORD dwWritten = 0;
			CString sIni = L"[.ShellClassInfo]\r\nConfirmFileOp=0\r\nIconFile=git.ico\r\nIconIndex=0\r\nInfoTip=Git Repository\r\n";
			WriteFile(hFile, static_cast<LPCWSTR>(sIni), sIni.GetLength() * sizeof(wchar_t), &dwWritten, nullptr);
		}
		PathMakeSystemFolder(path);
	}
}

}
