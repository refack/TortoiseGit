module;
#include <windows.h>
#include <Uxtheme.h>

export module RIAA;
import std;
using std::FILE;

export namespace RIAA
{
template <typename type>
struct CDefaultHandleNull
{
	static constexpr type DefaultHandle()
	{
		return nullptr;
	}
};

struct CDefaultHandleInvalid
{
	static constexpr HANDLE DefaultHandle()
	{
		return INVALID_HANDLE_VALUE;
	}
};

/**
 * \ingroup Utils
 * Helper classes for handles.
 */
template <typename HandleType,
	template <class> class CloseFunction,
	typename NullType = CDefaultHandleNull<HandleType>>
class CSmartHandle
{
public:
	CSmartHandle()
		: m_Handle(NullType::DefaultHandle())
	{
	}

	CSmartHandle(HandleType&& h)
		: m_Handle(h)
	{
	}

	CSmartHandle(CSmartHandle&& h) noexcept
		: m_Handle(h.Detach())
	{
	}

	CSmartHandle& operator=(CSmartHandle&& h) noexcept
	{
		*this = h.Detach();
		return *this;
	}

	HandleType& operator=(HandleType&& h)
	{
		if (m_Handle != h)
		{
			CleanUp();
			m_Handle = h;
		}

		return m_Handle;
	}

	bool CloseHandle()
	{
		return CleanUp();
	}

	HandleType Detach() noexcept
	{
		HandleType p = m_Handle;
		m_Handle = NullType::DefaultHandle();

		return p;
	}

	operator HandleType() const
	{
		return m_Handle;
	}

	HandleType * GetPointer()
	{
		return &m_Handle;
	}

	operator bool() const
	{
		return IsValid();
	}

	bool IsValid() const
	{
		return m_Handle != NullType::DefaultHandle();
	}


	~CSmartHandle()
	{
		CleanUp();
	}

private:
	CSmartHandle(const HandleType&) = delete;
	CSmartHandle(const CSmartHandle&) = delete;
	HandleType& operator=(const HandleType&) = delete;
	CSmartHandle& operator=(const CSmartHandle&) = delete;

protected:
	bool CleanUp()
	{
		if (m_Handle != NullType::DefaultHandle())
		{
			const bool b = CloseFunction<HandleType>::Close(m_Handle);
			m_Handle = NullType::DefaultHandle();
			return b;
		}
		return false;
	}


	HandleType m_Handle;
};

template <typename T>
struct CCloseHandle
{
	static bool Close(T handle)
	{
		return !!::CloseHandle(handle);
	}
};



template <typename T>
struct CCloseRegKey
{
	static bool Close(T handle)
	{
		return RegCloseKey(handle) == ERROR_SUCCESS;
	}
};


template <typename T>
struct CCloseLibrary
{
	static bool Close(T handle)
	{
		return !!::FreeLibrary(handle);
	}
};


template <typename T>
struct CCloseViewOfFile
{
	static bool Close(T handle)
	{
		return !!::UnmapViewOfFile(handle);
	}
};

template <typename T>
struct CCloseFindFile
{
	static bool Close(T handle)
	{
		return !!::FindClose(handle);
	}
};

template <typename T>
struct CCloseFILE
{
	static bool Close(T handle)
	{
		return !!fclose(handle);
	}
};

template <typename T>
struct CCloseThemeData
{
	static bool Close(T hTheme)
	{
		return !!::CloseThemeData(hTheme);
	}
};

template <typename T>
struct CCloseIcon
{
	static bool Close(T handle)
	{
		return !!DestroyIcon(handle);
	}
};

// Client code (definitions of standard Windows handles).
using CAutoGeneralHandle	= CSmartHandle<HANDLE,	CCloseHandle>;
using CAutoRegKey			= CSmartHandle<HKEY,		CCloseRegKey>;
using CAutoViewOfFile		= CSmartHandle<PVOID,		CCloseViewOfFile>;
using CAutoLibrary			= CSmartHandle<HMODULE,	CCloseLibrary>;
using CAutoFile				= CSmartHandle<HANDLE,	CCloseHandle, CDefaultHandleInvalid>;
using CAutoFindFile			= CSmartHandle<HANDLE,	CCloseFindFile, CDefaultHandleInvalid>;
using CAutoFILE				= CSmartHandle<FILE*,		CCloseFILE>;
using CAutoThemeData		= CSmartHandle<HTHEME,	CCloseThemeData>;
using CAutoIcon				= CSmartHandle<HICON,		CCloseIcon>;

/*
void CompilerTests()
{
	// compiler tests
	{
		HANDLE h = reinterpret_cast<HANDLE>(1);
		CAutoFile hFile = h;                    // C2280
		CAutoFile hFile2 = std::move(h);        // OK
		// OK, uses move semantics
		CAutoFile hFile3 = CreateFile(L"c:\\test.txt", GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		CAutoFile hFile4 = hFile3;              // C2280
		CAutoFile hFile5 = std::move(hFile3);   // OK
	}
}
*/
}

export using namespace RIAA;