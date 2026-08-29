module;
#include <cstdarg>
#include <stdio.h>
#include <windows.h>

export module DebugOutput;

import std;
import Registry;

export namespace tgit::DebugOutput {

class CTraceToOutputDebugString
{
public:
	static CTraceToOutputDebugString& Instance()
	{
		static CTraceToOutputDebugString m_pInstance;
		return m_pInstance;
	}

	static bool Active()
	{
		return Instance().m_bActive;
	}

	// Non Unicode output helper
	void operator()(PCSTR pszFormat, ...) const
	{
		if (m_bActive)
		{
			va_list ptr;
			va_start(ptr, pszFormat);
			TraceV(pszFormat,ptr);
			va_end(ptr);
		}
	}

	// Unicode output helper
	void operator()(PCWSTR pszFormat, ...) const
	{
		if (m_bActive)
		{
			va_list ptr;
			va_start(ptr, pszFormat);
			TraceV(pszFormat,ptr);
			va_end(ptr);
		}
	}

private:
	CTraceToOutputDebugString()
		: m_LastTick(GetTickCount64())
		, m_bActive(!!CRegStdDWORD(L"Software\\TortoiseGit\\DebugOutputString", FALSE))
	{
	}

	// prevent cloning
	CTraceToOutputDebugString(const CTraceToOutputDebugString&);
	CTraceToOutputDebugString& operator=(const CTraceToOutputDebugString&);

	ULONGLONG m_LastTick;
	bool    m_bActive;

	// Non Unicode output helper
	static void TraceV(const PCSTR pszFormat, const va_list args)
	{
		// Format the output buffer
		char szBuffer[1024];
		_vsnprintf_s(szBuffer, _countof(szBuffer), _countof(szBuffer)-1, pszFormat, args);
		OutputDebugStringA(szBuffer);
	}

	// Unicode output helper
	static void TraceV(const PCWSTR pszFormat, const va_list args)
	{
		wchar_t szBuffer[1024];
		_vsnwprintf_s(szBuffer, _countof(szBuffer), _countof(szBuffer)-1, pszFormat, args);
		OutputDebugStringW(szBuffer);
	}

	bool IsActive()
	{
#ifdef DEBUG
		return true;
#else
		if (GetTickCount64() - m_LastTick > 10000UL)
		{
			m_LastTick = GetTickCount64();
			m_bActive = !!CRegStdDWORD(L"Software\\TortoiseGit\\DebugOutputString", FALSE);
		}
		return m_bActive;
#endif
	}
};

}

export using namespace tgit::DebugOutput;
