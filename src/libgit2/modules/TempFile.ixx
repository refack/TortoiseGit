module;
#include "Git.h"
#include "GitHash.h"

export module TempFile;
import TGitPath;
import RIAA;
import DirFileEnum;
import wil;

export namespace TempFile
{
/**
 * \ingroup Utils
 * This singleton class handles temporary files.
 * All temp files are deleted at the end of a run of SVN operations
 */
class CTempFiles
{
	CTGitPathList m_TempFileList;

	CTempFiles() = default;
	~CTempFiles() { m_TempFileList.DeleteAllFiles(false); }

public:
	// prevent cloning
	CTempFiles(const CTempFiles&) = delete;
	CTempFiles& operator=(const CTempFiles&) = delete;

	static CTempFiles& Instance()
	{
		static CTempFiles instance;
		return instance;
	}

	/**
	 * Returns a path to a temporary file.
	 * \param bRemoveAtEnd if true, the temp file is removed when this object goes out of scope.
	 * \param path         if set, the temp file will have the same file extension as this path.
	 * \param hash
	 */

	CTGitPath GetTempFilePath(const bool bRemoveAtEnd, const CTGitPath& path = {}, const CGitHash& hash = {})
	{
		const DWORD len = GetTortoiseGitTempPath(0, nullptr);

		const auto temppath = std::make_unique<wchar_t[]>(len + 1);
		const auto tempF = std::make_unique<wchar_t[]>(len + 50);
		GetTortoiseGitTempPath(len + 1, temppath.get());
		CTGitPath tempfile;
		if (path.IsEmpty())
		{
			::GetTempFileName(temppath.get(), L"git", 0, tempF.get());
			tempfile = CTGitPath(tempF.get());
		}
		else
		{
			CString possibletempfile;
			int i = 0;
			do
			{
				// use the UI path, which does unescaping for urls
				CString filename = path.GetBaseFilename().c_str();
				// remove illegal chars which could be present in urls
				filename.Remove('?');
				filename.Remove('*');
				filename.Remove('<');
				filename.Remove('>');
				filename.Remove('|');
				filename.Remove('"');
				// the inner loop assures that the resulting path is < MAX_PATH
				// if that's not possible without reducing the 'filename' to less than 5 chars, use a path
				// that's longer than MAX_PATH (in that case, we can't really do much to avoid longer paths)
				do
				{
					if (!hash.IsEmpty())
						possibletempfile.Format(L"%s%s-%s.%3.3x%s", temppath.get(), filename, hash.ToString(g_Git.GetShortHASHLength()).c_str(), i, path.GetFileExtension().c_str());
					else
						possibletempfile.Format(L"%s%s.%3.3x%s", temppath.get(), filename, i, path.GetFileExtension().c_str());
					tempfile.SetFromWin(possibletempfile);
					filename.Truncate(std::max(0, filename.GetLength() - 1));
				} while (filename.GetLength() > 4 && tempfile.GetWinPathString().size() >= MAX_PATH);
				++i;
				// now create the temp file in a thread safe way, so that subsequent calls to GetTempFile() return different filenames.
				const RIAA::CAutoFile hFile = CreateFile(tempfile.GetWinPath(), GENERIC_READ, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr);
				if (const auto lastErr = GetLastError(); hFile || (lastErr != ERROR_FILE_EXISTS && lastErr != ERROR_ACCESS_DENIED))
					break;
			} while (true);
		}
		if (bRemoveAtEnd)
			m_TempFileList.AddPath(tempfile);
		return tempfile;
	}

	static void DeleteOldTempFiles()
	{
		DWORD len = GetTortoiseGitTempPath(0, nullptr);
		const auto path = std::make_unique<wchar_t[]>(len + 100);
		len = GetTortoiseGitTempPath(len + 100, path.get());
		if (len == 0)
			return;

		const auto sysTime = winrt::clock::now();
		// only delete files older than a day
		const auto cutoff = sysTime - std::chrono::days{1};

		CDirFileEnum finder(path.get());
		while (const auto file = finder.NextFile())
		{
			CString filepath = file->GetFilePath();
			if (file->GetCreateTimeTicks() < cutoff)
			{
				::SetFileAttributes(filepath, FILE_ATTRIBUTE_NORMAL);
				if (file->IsDirectory())
					::RemoveDirectory(filepath);
				else
					::DeleteFile(filepath);
			}
		}
	}
};

} // namespace TempFile

export using namespace TempFile;