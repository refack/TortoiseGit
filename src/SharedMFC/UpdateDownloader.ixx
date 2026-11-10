module;
#include "stdafx.h"
// #include <Ntddk.h>
#include <wil/win32_helpers.h>

#include <tgitlib/version.h>


export module UpdateDownloader;
import std;
import tgit;
import DebugOutput;
using namespace tgit;



export class CUpdateDownloader {
	HWND m_hWnd = nullptr;
	bool m_bForce = false;
	UINT m_uiMsg = 0;
	CEvent* m_eventStop = nullptr;

	std::wstring userAgent;
	wil::unique_hinternet hOpenHandle;

public:
	struct DOWNLOADSTATUS {
		ULONG ulProgress;
		ULONG ulProgressMax;
	};



	CUpdateDownloader(const HWND hwnd, const CString& sVersion, const bool force = false, const UINT msg = 0, CEvent* eventStop = nullptr) :
		m_hWnd(hwnd)
		, m_bForce(force)
		, m_uiMsg(msg)
		, m_eventStop(eventStop)
		, userAgent{ std::format(L"TortoiseGit {}; {}; {}", sVersion, _T(TGIT_PLATFORM), til::GetOSVanityString()) }
		, hOpenHandle{ InternetOpen(userAgent.c_str(), INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0) } {}


	~CUpdateDownloader() = default;


	DWORD DownloadFile(const CString& url, const CString& dest, bool showProgress) const;


	const std::wstring& GetUserAgent() const { return userAgent; }
};



DWORD CUpdateDownloader::DownloadFile(const CString& url, const CString& dest, const bool showProgress) const {
	CString hostname;
	CString urlpath;
	URL_COMPONENTS urlComponents = {};
	urlComponents.dwStructSize = sizeof(urlComponents);
	urlComponents.lpszHostName = hostname.GetBufferSetLength(INTERNET_MAX_HOST_NAME_LENGTH);
	urlComponents.dwHostNameLength = INTERNET_MAX_HOST_NAME_LENGTH;
	urlComponents.lpszUrlPath = urlpath.GetBufferSetLength(INTERNET_MAX_PATH_LENGTH);
	urlComponents.dwUrlPathLength = INTERNET_MAX_PATH_LENGTH;
	if (!InternetCrackUrl(url, url.GetLength(), 0, &urlComponents))
		return GetLastError();
	hostname.ReleaseBuffer();
	urlpath.ReleaseBuffer();

	if (m_bForce)
		DeleteUrlCacheEntry(url);

	BOOL bTrue = TRUE;
	const BOOL enableDecoding = InternetSetOption(hOpenHandle.get(), INTERNET_OPTION_HTTP_DECODING, &bTrue, sizeof(bTrue));

	const bool isHttps = urlComponents.nScheme == INTERNET_SCHEME_HTTPS;
	const auto hConnectHandle = wil::unique_hinternet{
		InternetConnect(
			hOpenHandle.get(),
			hostname,
			urlComponents.nPort,
			nullptr,
			nullptr,
			isHttps ? INTERNET_SCHEME_HTTP : urlComponents.nScheme,
			0,
			0
		)
	};
	if (!hConnectHandle) {
		const DWORD err = GetLastError();
		DebugOutput::TracePrintf(_T(__FUNCTION__) L": Download of %s failed on InternetConnect: %d\n", url, err);
		return err;
	}
	const auto hResourceHandle = wil::unique_hinternet{
		HttpOpenRequest(
			hConnectHandle.get(),
			nullptr,
			urlpath,
			nullptr,
			nullptr,
			nullptr,
			INTERNET_FLAG_KEEP_CONNECTION | (isHttps ? INTERNET_FLAG_SECURE : 0) | (m_bForce ? INTERNET_FLAG_HYPERLINK : 0),
			0
		)
	};
	if (!hResourceHandle) {
		const DWORD err = GetLastError();
		DebugOutput::TracePrintf(_T(__FUNCTION__) L": Download of %s failed on HttpOpenRequest: %d\n", url, err);
		return err;
	}

	if (enableDecoding)
		HttpAddRequestHeaders(hResourceHandle.get(), L"Accept-Encoding: gzip, deflate\r\n", static_cast<DWORD>(-1), HTTP_ADDREQ_FLAG_ADD);

	{
	resend:
		const BOOL httpsendrequest = HttpSendRequest(hResourceHandle.get(), nullptr, 0, nullptr, 0);

		const DWORD dwError = InternetErrorDlg(
			m_hWnd,
			hResourceHandle.get(),
			ERROR_SUCCESS,
			FLAGS_ERROR_UI_FILTER_FOR_ERRORS | FLAGS_ERROR_UI_FLAGS_CHANGE_OPTIONS | FLAGS_ERROR_UI_FLAGS_GENERATE_DATA,
			nullptr
		);

		if (dwError == ERROR_INTERNET_FORCE_RETRY)
			goto resend;
		else if (!httpsendrequest) {
			const DWORD err = GetLastError();
			DebugOutput::TracePrintf(_T(__FUNCTION__) L": Download of %s failed: %d, %d\n", url, httpsendrequest, err);
			return err;
		}
	}

	DWORD contentLength = 0;
	{
		DWORD length = sizeof(contentLength);
		HttpQueryInfo(hResourceHandle.get(), HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &contentLength, &length, nullptr);
	}
	{
		DWORD statusCode = 0;
		DWORD length = sizeof(statusCode);
		if (!HttpQueryInfo(hResourceHandle.get(), HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &length, nullptr) || statusCode != 200) {
			DebugOutput::TracePrintf(_T(__FUNCTION__) L": Download of %s returned %d\n", url, statusCode);
			if (statusCode == 404)
				return ERROR_FILE_NOT_FOUND;
			else if (statusCode == 403)
				return ERROR_ACCESS_DENIED;
			return static_cast<DWORD>(INET_E_DOWNLOAD_FAILURE);
		}
	}

	CFile destinationFile;
	if (!destinationFile.Open(dest, CFile::modeCreate | CFile::modeWrite)) {
		return ERROR_OPEN_FAILED;
	}
	DWORD downloadedSum = 0; // sum of bytes downloaded so far
	constexpr DWORD BUFFER_SIZE = 65536;
	const auto buff = std::make_unique<char[]>(BUFFER_SIZE);
	do {
		DWORD size; // size of the data available
		if (!InternetQueryDataAvailable(hResourceHandle.get(), &size, 0, 0)) {
			const DWORD err = GetLastError();
			DebugOutput::TracePrintf(_T(__FUNCTION__) L": Download of %s failed on InternetQueryDataAvailable: %d\n", url, err);
			return err;
		}

		DWORD downloaded; // size of the downloaded data
		if (!InternetReadFile(hResourceHandle.get(), buff.get(), std::min(BUFFER_SIZE, size), &downloaded)) {
			const DWORD err = GetLastError();
			DebugOutput::TracePrintf(_T(__FUNCTION__) L": Download of %s failed on InternetReadFile: %d\n", url, err);
			return err;
		}

		if (downloaded == 0)
			break;

		try {
			destinationFile.Write(buff.get(), downloaded);
		} catch (CFileException* e) {
			destinationFile.Close();
			::DeleteFile(dest);
			auto ret = e->m_lOsError;
			if (e->m_cause == CFileException::diskFull)
				ret = ERROR_DISK_FULL;
			e->Delete();
			return ret;
		}

		if (DWordAdd(downloadedSum, downloaded, &downloadedSum) != S_OK)
			downloadedSum = DWORD_MAX - 1;

		if (!showProgress)
			continue;

		ASSERT(m_uiMsg && m_eventStop);

		if (contentLength == 0) // got no content-length from webserver
		{
			DOWNLOADSTATUS downloadStatus = { .ulProgress = 0, .ulProgressMax = 1 + 1 }; // + 1 for download of signature file
			::SendMessage(m_hWnd, m_uiMsg, 0, reinterpret_cast<LPARAM>(&downloadStatus));
		} else {
			if (downloadedSum > contentLength)
				downloadedSum = contentLength - 1;
			DOWNLOADSTATUS downloadStatus = { .ulProgress = downloadedSum, .ulProgressMax = contentLength + 1 }; // + 1 for download of signature file
			::SendMessage(m_hWnd, m_uiMsg, 0, reinterpret_cast<LPARAM>(&downloadStatus));
		}

		if (::WaitForSingleObject(*m_eventStop, 0) == WAIT_OBJECT_0) {
			return static_cast<DWORD>(E_ABORT); // canceled by the user
		}
	}
	while (true);

	if (downloadedSum == 0) {
		DebugOutput::TracePrintf(_T(__FUNCTION__) L": Download size of %s was zero.\n", url);
		return static_cast<DWORD>(INET_E_DOWNLOAD_FAILURE);
	}
	return ERROR_SUCCESS;
}
