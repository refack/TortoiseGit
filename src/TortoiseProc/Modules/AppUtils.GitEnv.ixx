module;
#include "stdafx.h"
#include "std.h"

#include "TortoiseProc.h"
#include "git2/sys/errors.h"
#include "UserPassword.h"
#include "Utils/CertificateValidationHelper.h"
#include "CheckCertificateDlg.h"
#include "IconExtractor.h"

export module AppUtils.GitEnv;

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

int GetMsysgitVersion(HWND hWnd)
{
	if (TGitH::ms_LastMsysGitVersion)
		return TGitH::ms_LastMsysGitVersion;

	CString versiondebug;

	CRegDWORD regTime		= CRegDWORD(L"Software\\TortoiseGit\\git_file_time");
	CRegDWORD regVersion	= CRegDWORD(L"Software\\TortoiseGit\\git_cached_version");

	const fs::path gitpath = GitCall::ms_LastMsysGitDir / L"git.exe";

	__int64 time=0;
	if (!TGitH::GetFileModifyTime(gitpath, &time))
	{
		if (static_cast<DWORD>(TGitH::filetime_to_time_t(time)) == regTime)
		{
			TGitH::ms_LastMsysGitVersion = regVersion;
			return regVersion;
		}
	}

	CString err;
	const int ver = g_Git.GetGitVersion(&versiondebug, &err);
	if (ver < 0)
	{
		MessageBox(hWnd, L"git.exe not correctly set up (" + err + L")\nCheck TortoiseGit settings and consult help file for \"Git.exe Path\".", L"TortoiseGit", MB_OK | MB_ICONERROR);
		return -1;
	}

	{
		if (!ver)
		{
			MessageBox(hWnd, L"Could not parse git.exe version number: \"" + versiondebug + L'"', L"TortoiseGit", MB_OK | MB_ICONERROR);
			return -1;
		}
	}

	regTime = static_cast<DWORD>(TGitH::filetime_to_time_t(time));
	regVersion = ver;
	TGitH::ms_LastMsysGitVersion = ver;

	// reinitialize everything (especially libgit2 config search paths), only needed because APPDATA is a Git config dir since 2.46
	g_Git.m_bInitialized = false;
	g_Git.CheckMsysGitDir();

	// tell the cache to refresh everything and restart
	SendCacheCommand(TGITCACHECOMMAND_REFRESHALL);
	SendCacheCommand(TGITCACHECOMMAND_END);

	return ver;
}


bool IsGitVersionNewerOrEqual(HWND hWnd, unsigned __int8 major, unsigned __int8 minor, unsigned __int8 patchlevel = 0, unsigned __int8 build = 0)
{
	auto ver = GetMsysgitVersion(hWnd);
	return ver >= ConvertVersionToInt(major, minor, patchlevel, build);
}

int GetLogOutputEncode(CGit* pGit = &g_Git)
{
	CString output = pGit->GetConfigValue(L"i18n.logOutputEncoding");
	if(output.IsEmpty())
		return CUnicodeUtils::StdGetCPCode(tgit::str::StringView(pGit->GetConfigValue(L"i18n.commitencoding")));
	else
		return CUnicodeUtils::StdGetCPCode(tgit::str::StringView(output));
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
