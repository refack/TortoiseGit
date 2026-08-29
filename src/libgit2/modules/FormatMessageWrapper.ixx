module;
#include <windows.h>


export module FormatMessageWrapper;

import std;

export class CFormatMessageWrapper
{
	DWORD  err_id = {};
	WCHAR* buffer = {};
	DWORD  length = {};

public:
	explicit CFormatMessageWrapper(const std::optional<DWORD> maybe_err_id = std::nullopt)
	{
		err_id = maybe_err_id ? *maybe_err_id : GetLastError();

		// The Dark Art incantation that has lived for over 30 years.
		length = FormatMessageW(
		   FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		   nullptr,
		   err_id,
		   MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
		   reinterpret_cast<LPWSTR>(&buffer),
		   0,
		   nullptr
		);
	}
	~CFormatMessageWrapper()
	{
		LocalFree(buffer);
	}
	CFormatMessageWrapper(const CFormatMessageWrapper&)            = delete;
	CFormatMessageWrapper& operator=(const CFormatMessageWrapper&) = delete;

	explicit operator bool() const { return length != 0; }
	[[nodiscard]] bool     empty()         const { return length == 0; }
	[[nodiscard]] LPCWSTR  c_str()         const { return buffer ? buffer : L""; }
	[[nodiscard]] DWORD    code()          const { return err_id; }
};


// this is a POC in C++/WinRT

/**
 * A wrapper class for calling the FormatMessage() Win32 function and controlling
 * the lifetime of the allocated error message buffer.
export class CFormatMessageWrapper
{
	// Must be first
	std::error_code err_id;
	// Must come after err_id
	std::wstring msg;

  public:
	explicit CFormatMessageWrapper(const std::optional<std::int32_t> maybe_code = std::nullopt) :
		err_id{ maybe_code.or_else([] {return std::optional<std::int32_t>{GetLastError()};}).value(), std::system_category() },
		msg{ winrt::hresult_error{ HRESULT_FROM_WIN32(err_id.value()) }.message() }
	{}

	operator LPCWSTR() const { return msg.c_str(); }
	operator bool() const { return err_id.value() != 0; }
	bool operator!() const { return err_id.value() == 0; }
};
 */
