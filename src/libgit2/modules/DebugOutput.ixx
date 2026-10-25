module;
#include <cstdarg>
#include <cstdio>
#include <windows.h>



export module DebugOutput;

import std;
import Registry;
import RollingFile;

// Settings, all under HKCU\Software\TortoiseGit, read once per process:
//   DebugOutputString  DWORD   master switch; messages go to OutputDebugString
//   DebugLogFile       string  also append to this file (environment variables expanded)
//   DebugLogMaxSizeKB  DWORD   roll the file over once it reaches this size (0 = never)
//   DebugLogMaxFiles   DWORD   rolled-over files kept, as <file>.1 (newest) … <file>.N

std::wstring VFormat(PCWSTR fmt, va_list args) {
	va_list probe;
	va_copy(probe, args);
	const int len = _vscwprintf(fmt, probe);
	va_end(probe);
	if (len <= 0)
		return {};
	std::wstring s(len, L'\0');
	_vsnwprintf_s(s.data(), s.size() + 1, _TRUNCATE, fmt, args);
	return s;
}


std::wstring VFormat(PCSTR fmt, va_list args) {
	va_list probe;
	va_copy(probe, args);
	const int len = _vscprintf(fmt, probe);
	va_end(probe);
	if (len <= 0)
		return {};
	std::string narrow(len, '\0');
	_vsnprintf_s(narrow.data(), narrow.size() + 1, _TRUNCATE, fmt, args);
	std::wstring s(MultiByteToWideChar(CP_ACP, 0, narrow.data(), len, nullptr, 0), L'\0');
	MultiByteToWideChar(CP_ACP, 0, narrow.data(), len, s.data(), static_cast<int>(s.size()));
	return s;
}


std::wstring ExpandEnvironment(const std::wstring& s) {
	std::wstring out(ExpandEnvironmentStringsW(s.c_str(), nullptr, 0), L'\0');
	if (out.empty())
		return s;
	out.resize(ExpandEnvironmentStringsW(s.c_str(), out.data(), static_cast<DWORD>(out.size())) - 1);
	return out;
}


const bool g_active = !!CRegStdDWORD(L"Software\\TortoiseGit\\DebugOutputString", FALSE);

const std::unique_ptr<tgit::CRollingFile> g_file = []() -> std::unique_ptr<tgit::CRollingFile> {
	if (!g_active)
		return nullptr;
	const auto file = ExpandEnvironment(CRegStdString(L"Software\\TortoiseGit\\DebugLogFile", L""));
	if (file.empty())
		return nullptr;
	return std::make_unique<tgit::CRollingFile>(
		file,
		std::uintmax_t{ CRegStdDWORD(L"Software\\TortoiseGit\\DebugLogMaxSizeKB", 10 * 1024) } * 1024,
		CRegStdDWORD(L"Software\\TortoiseGit\\DebugLogMaxFiles", 5)
	);
}();



export namespace tgit::DebugOutput {
constexpr bool IsTraceActive() {
	return g_active;
}


// Writes the message verbatim; safe for text that may contain '%'.
void TraceRaw(const std::wstring& message) {
	if (!g_active)
		return;
	OutputDebugStringW(message.c_str());
	if (g_file)
		g_file->Append(message);
}


// printf-style; the format is only expanded when tracing is on.
[[deprecated("use Trace(std::wformat_string, ...): its arguments are checked at compile time")]]
void TracePrintf(PCSTR pszFormat, ...) {
	if (!g_active)
		return;
	va_list args;
	va_start(args, pszFormat);
	TraceRaw(VFormat(pszFormat, args));
	va_end(args);
}


[[deprecated("use Trace(std::wformat_string, ...): its arguments are checked at compile time")]]
void TracePrintf(PCWSTR pszFormat, ...) {
	if (!g_active)
		return;
	va_list args;
	va_start(args, pszFormat);
	TraceRaw(VFormat(pszFormat, args));
	va_end(args);
}


template <class... Args>
void Trace(std::wformat_string<Args...> fmt, Args&&... args) {
	if (IsTraceActive())
		TraceRaw(std::format(fmt, std::forward<Args>(args)...));
}
}

export using namespace tgit::DebugOutput;
