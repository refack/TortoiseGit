// TEMPORARY diagnostic: symbolised stack for a crash during static
// initialisation, where neither main() nor gtest is running yet. Remove once
// the crash is understood.
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#pragma comment(lib, "dbghelp.lib")

namespace
{
LONG CALLBACK OnException(EXCEPTION_POINTERS* info)
{
	if (info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
		return EXCEPTION_CONTINUE_SEARCH;

	const HANDLE process = GetCurrentProcess();
	SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
	SymInitialize(process, nullptr, TRUE);

	CONTEXT context = *info->ContextRecord;
	STACKFRAME64 frame{};
	frame.AddrPC.Offset = context.Rip;
	frame.AddrPC.Mode = AddrModeFlat;
	frame.AddrFrame.Offset = context.Rbp;
	frame.AddrFrame.Mode = AddrModeFlat;
	frame.AddrStack.Offset = context.Rsp;
	frame.AddrStack.Mode = AddrModeFlat;

	std::fprintf(stderr, "CRASHPROBE: access violation (%s 0x%llx)\n",
		info->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
		static_cast<unsigned long long>(info->ExceptionRecord->ExceptionInformation[1]));
	for (int i = 0; i < 40 && StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, &context, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr); ++i)
	{
		char buffer[sizeof(SYMBOL_INFO) + 512]{};
		auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
		symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
		symbol->MaxNameLen = 511;
		DWORD64 displacement = 0;
		IMAGEHLP_LINE64 line{ sizeof(IMAGEHLP_LINE64) };
		DWORD lineDisplacement = 0;
		const bool haveSymbol = SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol);
		const bool haveLine = SymGetLineFromAddr64(process, frame.AddrPC.Offset, &lineDisplacement, &line);
		std::fprintf(stderr, "CRASHPROBE: #%02d %s  %s(%lu)\n", i, haveSymbol ? symbol->Name : "?", haveLine ? line.FileName : "", haveLine ? line.LineNumber : 0ul);
	}
	std::fflush(stderr);
	return EXCEPTION_CONTINUE_SEARCH;
}

struct Install
{
	Install() { AddVectoredExceptionHandler(1, OnException); }
};
}

#pragma warning(disable : 4073)
#pragma init_seg(lib)
static Install s_install;
