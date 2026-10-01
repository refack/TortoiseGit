module;
#include <windows.h>


export module TempFile;
import std;
import git_h;
import GitHash;
import TGitPath;
import RIAA;
import DirFileEnum;
import wil;

using namespace std::literals;


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
		const std::wstring temppath = TortoiseGitTempDirectory();
		CTGitPath tempfile;
		if (path.IsEmpty())
		{
			// GetTempFileName wants a MAX_PATH buffer, and leaves it untouched on failure
			std::wstring name;
			name.resize_and_overwrite(MAX_PATH, [&temppath](wchar_t* const buffer, const size_t capacity) {
				return ::GetTempFileName(temppath.c_str(), L"git", 0, buffer) ? wcsnlen(buffer, capacity) : size_t{ 0 };
			});
			tempfile = CTGitPath(name);
		}
		else
		{
			const std::wstring suffix = hash.IsEmpty() ? std::wstring{} : L"-" + hash.ToString(g_Git.GetShortHASHLength());
			const std::wstring extension = path.GetFileExtension();
			int i = 0;
			do
			{
				// use the UI path, which does unescaping for urls
				std::wstring filename = path.GetBaseFilename();
				// remove illegal chars which could be present in urls
				std::erase_if(filename, [](const wchar_t c) { return L"?*<>|\""sv.contains(c); });
				// the inner loop assures that the resulting path is < MAX_PATH
				// if that's not possible without reducing the 'filename' to less than 5 chars, use a path
				// that's longer than MAX_PATH (in that case, we can't really do much to avoid longer paths)
				do
				{
					// {:03x} is printf's %3.3x: at least three hex digits, zero-padded
					tempfile.SetFromWin(std::format(L"{}{}{}.{:03x}{}", temppath, filename, suffix, i, extension));
					if (!filename.empty())
						filename.pop_back();
				} while (filename.size() > 4 && tempfile.GetWinPathString().size() >= MAX_PATH);
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
		const std::wstring path = TortoiseGitTempDirectory();
		if (path.empty())
			return;

		const auto sysTime = wil::clock::now();
		// only delete files older than a day
		const auto cutoff = sysTime - std::chrono::days{1};

		CDirFileEnum finder(path.c_str());
		while (const auto file = finder.NextFile())
		{
			const std::wstring filepath = file->GetFilePath().GetString(); // CDirFileEnum still hands out CString
			if (file->GetCreateTimeTicks() < cutoff)
			{
				::SetFileAttributes(filepath.c_str(), FILE_ATTRIBUTE_NORMAL);
				if (file->IsDirectory())
					::RemoveDirectory(filepath.c_str());
				else
					::DeleteFile(filepath.c_str());
			}
		}
	}

private:
	/// "<temp>\TortoiseGit\", or empty when there is no temp path. The two-call
	/// contract of GetTortoiseGitTempPath, filled straight into the string.
	static std::wstring TortoiseGitTempDirectory()
	{
		const DWORD needed = GetTortoiseGitTempPath(0, nullptr); // counts the NUL
		if (needed == 0)
			return {};
		std::wstring directory;
		directory.resize_and_overwrite(needed, [needed](wchar_t* const buffer, const size_t capacity) {
			// on failure it leaves buffer[0] == 0, so wcsnlen answers "empty" either way
			return GetTortoiseGitTempPath(needed, buffer) ? wcsnlen(buffer, capacity) : size_t{ 0 };
		});
		return directory;
	}
};

} // namespace TempFile

export using namespace TempFile;