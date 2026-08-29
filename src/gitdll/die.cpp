// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2008-2011, 2013, 2016-2017 - TortoiseGit

// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//

#include <cstdarg>
#include <windows.h>
#include <string>
#include <format>

using std::operator ""s;

extern "C" int close_all();

namespace
{
inline int dyingRecCnt = 0;
std::string g_last_error_s{};

void reset_dll_state()
{
	close_all();
	dyingRecCnt = 0;
}

[[noreturn]] [[gnu::format(printf, 1, 2)]] [[maybe_unused]] void die(const std::string_view  err, const std::string_view msg = {})
{
	g_last_error_s = std::vformat(err, std::make_format_args(msg));
	reset_dll_state();
	throw std::exception{ g_last_error_s.c_str() };
}

[[noreturn]] [[gnu::format(printf, 1, 2)]] void die(const std::string_view  err, int code, const std::string_view msg = {})
{
	g_last_error_s = std::vformat(err, std::make_format_args(code, msg));
	reset_dll_state();
	throw std::exception{ g_last_error_s.c_str() };
}
} // namespace

void v_print_debug(const std::string_view fmt, const std::format_args args)
{
	// Safely formats the arguments into a modern std::string
	const std::string message = std::vformat(fmt, args);

	// Output directly to the native Windows debug stream
	OutputDebugStringA(message.c_str());
}

void v_print_debug(const std::string_view fmt, va_list params) {
	v_print_debug(fmt, std::make_format_args(params));
}

[[gnu::format(printf, 1, 0)]] std::string v_format_legacy(const char* fmt, const va_list args)
{
	// 1. Create a copy to measure the string size without corrupting the original list
	va_list args_copy;
	va_copy(args_copy, args);
	const int length = vsnprintf(nullptr, 0, fmt, args_copy);
	va_end(args_copy);

	if (length <= 0)
		return std::string{};

	std::string result(length, '\0');
	std::ignore = vsnprintf(result.data(), length + 1, fmt, args);
	return result;
}

extern "C"
{

char* HandOffToC(const std::string& cpp_str) noexcept
{
	const auto len = cpp_str.size() + 1;
	const auto c_buffer = static_cast<char*>(std::malloc(len));

	if (!c_buffer)
		return nullptr;

	strcpy_s(c_buffer, len, cpp_str.c_str());

	return c_buffer;
}

void print_debug(const std::string_view fmt)
{
	const std::string message = "[gitdll] "s + std::string{fmt};
	OutputDebugStringA(message.c_str());
}

[[gnu::format(printf, 1, 0)]] void handle_error(const char* fmt, const va_list params)
{
	const std::string message = "[error] "s + v_format_legacy(fmt, params);
	print_debug(message);
}

[[gnu::format(printf, 1, 0)]] void handle_warning(const char* fmt, const va_list params)
{
	const std::string message = "[warning] "s + v_format_legacy(fmt, params);
	print_debug(message);
}

[[noreturn]] [[gnu::format(printf, 1, 0)]] void die_dll(const char* err, const va_list params)
{
	g_last_error_s = v_format_legacy(err, params);
	reset_dll_state();
	throw std::exception{ g_last_error_s.c_str() };
}

[[noreturn]] void vc_exit(const int code)
{
	if (g_last_error_s.empty())
		die("libgit called \"exit(%d)\".", code);

	const auto msg = g_last_error_s.c_str();
	die("libgit called \"exit(%d)\". Last error was:\n%s", code, msg);
}

int die_is_recursing_dll()
{
	return dyingRecCnt++;
}
}
