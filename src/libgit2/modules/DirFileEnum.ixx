module;
#include <atlstr.h>
#include <winrt/base.h>

export module DirFileEnum;
import StringUtils;
import wil;

export namespace DirFileEnum {
/**
 * \ingroup Utils
 * Enumerates over a directory tree, non-recursively.
 * Advantages over CFileFind:
 * 1) Return values are not broken.  An error return from
 *    CFileFind::FindNext() indicates that the *next*
 *    call to CFileFind::FindNext() will fail.
 *    A failure from CSimpleFileFind means *that* call
 *    failed, which is what I'd expect.
 * 2) Error handling.  If you use CFileFind, you are
 *    required to call ::GetLastError() yourself, there
 *    is no abstraction.
 * 3) Support for ignoring the "." and ".." directories
 *    automatically.
 * 4) No dynamic memory allocation.
 */
class CSimpleFileFind
{
	/**
	 * Windows FindFirstFile() handle.
	 */
	HANDLE m_hFindFile;

	/**
	 * Windows error code - if all is well, ERROR_SUCCESS.
	 * At end of directory, ERROR_NO_MORE_FILES.
	 */
	DWORD m_dError = ERROR_SUCCESS;

	/**
	 * Flag indicating that FindNextFile() has not yet been
	 * called.
	 */
	BOOL m_bFirst = TRUE;

protected:
	/**
	 * The prefix for files in this directory.
	 * Ends with a "\", unless it's a drive letter only
	 * ("C:" is different from "C:\", and "C:filename" is
	 * legal anyway.)
	 */
	CString m_sPathPrefix;

	/**
	 * The file data returned by FindFirstFile()/FindNextFile().
	 */
	WIN32_FIND_DATA m_findFileData;

public:
	/**
	 * Constructor.
	 *
	 * \param sPath    The path to search in.
	 * \param pPattern The filename pattern - default all files.
	 */
	CSimpleFileFind(const CString& sPath, LPCWSTR pPattern = L"*.*");
	~CSimpleFileFind();

	/**
	 * Advance to the next file.
	 * Note that the state of this object is undefined until
	 * this method is called the first time.
	 *
	 * \return TRUE if a file was found, FALSE on error or
	 * end-of-directory (use IsError() and IsPastEnd() to
	 * disambiguate).
	 */
	BOOL FindNextFile();

	/**
	 * Advance to the next file, ignoring the "." and ".."
	 * pseudo-directories (if seen).
	 *
	 * Behaves like FindNextFile(), apart from ignoring "."
	 * and "..".
	 *
	 * \return TRUE if a file was found, FALSE on error or
	 * end-of-directory.
	 */
	BOOL FindNextFileNoDots();

	/**
	 * Advance to the next file, ignoring all directories.
	 *
	 * Behaves like FindNextFile(), apart from ignoring
	 * directories.
	 *
	 * \return TRUE if a file was found, FALSE on error or
	 * end-of-directory.
	 */
	BOOL FindNextFileNoDirectories();

	/**
	 * Get the Windows error code.
	 * Only useful when IsError() returns true.
	 *
	 * \return Windows error code.
	 */
	DWORD GetError() const
	{
		return m_dError;
	}

	/**
	 * Check if the current file data is valid.
	 * (I.e. there has not been an error and we are not past
	 * the end of the directory).
	 *
	 * \return TRUE iff the current file data is valid.
	 */
	BOOL IsValid() const
	{
		return (m_dError == ERROR_SUCCESS);
	}

	/**
	 * Check if we have passed the end of the directory.
	 *
	 * \return TRUE iff we have passed the end of the directory.
	 */
	BOOL IsPastEnd() const
	{
		return (m_dError == ERROR_NO_MORE_FILES);
	}

	/**
	 * Check if there has been an unexpected error - i.e.
	 * any error other than passing the end of the directory.
	 *
	 * \return TRUE iff there has been an unexpected error.
	 */
	BOOL IsError() const
	{
		return (m_dError != ERROR_SUCCESS) && (m_dError != ERROR_NO_MORE_FILES);
	}

	/**
	 * Check if the current file is a directory.
	 *
	 * \return TRUE iff the current file is a directory.
	 */
	bool IsDirectory() const
	{
		return !!(m_findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
	}

	/**
	 * Get the current file name (excluding the path).
	 *
	 * \return the current file name.
	 */
	CString GetFileName() const
	{
		return m_findFileData.cFileName;
	}

	/*
	 * Get the current file name, including the path.
	 *
	 * \return the current file path.
	 */
	CString GetFilePath() const
	{
		return m_sPathPrefix + m_findFileData.cFileName;
	}

	/**
	 * Returns the last write time of the path
	 */
	FILETIME GetLastWriteTime() const
	{
		return m_findFileData.ftLastWriteTime;
	}
	FILETIME GetCreateTime() const
	{
		return m_findFileData.ftCreationTime;
	}
	winrt::Windows::Foundation::DateTime GetLastWriteTimeTicks() const
	{
		return winrt::clock::from_FILETIME(m_findFileData.ftLastWriteTime);
	}
	winrt::Windows::Foundation::DateTime GetCreateTimeTicks() const
	{
		return winrt::clock::from_FILETIME(m_findFileData.ftCreationTime);
	}

	/**
	 * Check if the current file is the "." or ".."
	 * pseudo-directory.
	 *
	 * \return TRUE iff the current file is the "." or ".."
	 * pseudo-directory.
	 */
	BOOL IsDots() const
	{
		return IsDirectory() && m_findFileData.cFileName[0] == '.' && ((m_findFileData.cFileName[1] == 0) || (m_findFileData.cFileName[1] == '.' && m_findFileData.cFileName[2] == 0));
	}
};

/**
 * \ingroup Utils
 * Enumerates over a directory tree, recursively.
 *
 * \par requirements
 * win95 or later
 * winNT4 or later
 * MFC
 *
 * \version 1.0
 * first version
 *
 * \date 18-Feb-2004
 *
 * \author Jon Foster
 *
 * \par license
 * This code is GPL'd.
 */
class CDirFileEnum
{
public:
	// inherit protected so that CDirStackEntry ist not polymorphic with CSimpleFileFind
	class CDirStackEntry : protected CSimpleFileFind
	{
	protected:
		CDirStackEntry(CDirStackEntry* seNext, const CString& sDirName);
		~CDirStackEntry();

		CDirStackEntry* m_seNext;

	public:
		using CSimpleFileFind::GetCreateTime;
		using CSimpleFileFind::GetFileName;
		using CSimpleFileFind::GetFilePath;
		using CSimpleFileFind::GetLastWriteTime;
		using CSimpleFileFind::IsDirectory;

		friend class CDirFileEnum;
	};

	CDirStackEntry* m_seStack;
	BOOL m_bIsNew;

	void PopStack();
	void PushStack(const CString& sDirName);

	/**
	 * Iterate through the specified directory and all subdirectories.
	 * It does not matter whether or not the specified directory ends
	 * with a slash.  Both relative and absolute paths are allowed,
	 * the results of this iterator will be consistent with the style
	 * passed to this constructor.
	 *
	 * @param dirName The directory to search in.
	 */
	explicit CDirFileEnum(const CString& dirName): m_seStack(nullptr), m_bIsNew(TRUE) { PushStack(dirName);	}

	/**
	 * Destructor.  Frees all resources.
	 */
	~CDirFileEnum() {
		while (m_seStack)
			PopStack();
	}

	/**
	 * Get the next file from this iterator.
	 *
	 * \param  bRecurse if the last result was a directory, specifies whether to
	 *                recurse into that directory or skip it.
	 * \return CDirStackEntry* result On successful return

	 */
	const CDirStackEntry* NextFile(bool bRecurse = true);
};

CSimpleFileFind::CSimpleFileFind(const CString& sPath, LPCWSTR pPattern)
	: m_sPathPrefix(sPath)
{
	// Add a trailing \ to m_sPathPrefix if it is missing.
	// Do not add one to "C:" since "C:" and "C:\" are different.
	int len = m_sPathPrefix.GetLength();
	if (len != 0)
	{
		wchar_t ch = sPath[len - 1];
		if (ch != '\\' && (ch != ':' || len != 2))
			m_sPathPrefix += L'\\';
	}
	if (len >= 248 && (CStringUtils::StartsWith(m_sPathPrefix, L"\\\\?\\")))
		m_hFindFile = ::FindFirstFileEx(static_cast<LPCWSTR>(L"\\\\?\\" + m_sPathPrefix + pPattern), FindExInfoBasic, &m_findFileData, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
	else
		m_hFindFile = ::FindFirstFileEx(static_cast<LPCWSTR>(m_sPathPrefix + pPattern), FindExInfoBasic, &m_findFileData, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
	if (m_hFindFile == INVALID_HANDLE_VALUE)
		m_dError = ::GetLastError();
}

CSimpleFileFind::~CSimpleFileFind()
{
	if (m_hFindFile != INVALID_HANDLE_VALUE)
		::FindClose(m_hFindFile);
}

BOOL CSimpleFileFind::FindNextFile()
{
	if (m_dError)
		return FALSE;

	if (m_bFirst)
	{
		m_bFirst = FALSE;
		return TRUE;
	}

	if (!::FindNextFile(m_hFindFile, &m_findFileData))
	{
		m_dError = ::GetLastError();
		return FALSE;
	}

	return TRUE;
}

BOOL CSimpleFileFind::FindNextFileNoDots()
{
	BOOL result;
	do
	{
		result = FindNextFile();
	} while (result && IsDots());

	return result;
}

BOOL CSimpleFileFind::FindNextFileNoDirectories()
{
	BOOL result;
	do
	{
		result = FindNextFile();
	} while (result && IsDirectory());

	return result;
}

/*
 * Implementation notes:
 *
 * This is a depth-first traversal.  Directories are visited before
 * their contents.
 *
 * We keep a stack of directories.  The deepest directory is at the top
 * of the stack, the originally-requested directory is at the bottom.
 * If we come across a directory, we first return it to the user, then
 * recurse into it.  The finder at the bottom of the stack always points
 * to the file or directory last returned to the user (except immediately
 * after creation, when the finder points to the first valid thing we need
 * to return, but we haven't actually returned anything yet - hence the
 * m_bIsNew variable).
 *
 * Errors reading a directory are assumed to be end-of-directory, and
 * are otherwise ignored.
 *
 * The "." and ".." psedo-directories are ignored for obvious reasons.
 */


CDirFileEnum::CDirStackEntry::CDirStackEntry(CDirStackEntry* seNext, const CString& sDirName)
	: CSimpleFileFind(sDirName)
	, m_seNext(seNext)
{
}

CDirFileEnum::CDirStackEntry::~CDirStackEntry()
{
}

inline void CDirFileEnum::PopStack()
{
	CDirStackEntry* seToDelete = m_seStack;
	m_seStack = seToDelete->m_seNext;
	delete seToDelete;
}

inline void CDirFileEnum::PushStack(const CString& sDirName)
{
	m_seStack = new CDirStackEntry(m_seStack, sDirName);
}


const CDirFileEnum::CDirStackEntry* CDirFileEnum::NextFile(bool bRecurse /* = true */)
{
	if (m_bIsNew)
	{
		// Special-case first time - haven't found anything yet,
		// so don't do recurse-into-directory check.
		m_bIsNew = FALSE;
	}
	else if (m_seStack && m_seStack->IsDirectory() && bRecurse)
		PushStack(m_seStack->GetFilePath());

	while (m_seStack && !m_seStack->FindNextFileNoDots())
	{
		// No more files in this directory, try parent.
		PopStack();
	}

	if (m_seStack)
		return m_seStack;

	return nullptr;
}

}

export using namespace DirFileEnum;