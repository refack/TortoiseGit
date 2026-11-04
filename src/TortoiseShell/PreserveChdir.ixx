module;
#include "stdafx.h"



export module PreserveChdir;
import std;

export class PreserveChdir
{
public:
	PreserveChdir();
	~PreserveChdir();

private:
	std::unique_ptr<wchar_t[]> m_originalCurrentDirectory;
};

PreserveChdir::PreserveChdir()
{
	DWORD len = GetCurrentDirectory(0, nullptr);
	if (!len)
		return;

	m_originalCurrentDirectory = std::make_unique<wchar_t[]>(len);
	if (GetCurrentDirectory(len, m_originalCurrentDirectory.get()) == 0)
		m_originalCurrentDirectory.reset();
}

PreserveChdir::~PreserveChdir()
{
	if (!m_originalCurrentDirectory)
		return;

	DWORD len = GetCurrentDirectory(0, nullptr);
	auto currentDirectory = std::make_unique<wchar_t[]>(len);

	// _tchdir is an expensive function - don't call it unless we really have to
	GetCurrentDirectory(len, currentDirectory.get());
	if (wcscmp(currentDirectory.get(), m_originalCurrentDirectory.get()) != 0)
		SetCurrentDirectory(m_originalCurrentDirectory.get());
}
