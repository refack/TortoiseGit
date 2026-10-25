module;
#include <memory>
#include <process.h> // _getpid

export module AutoTempDir;
import std;
import str_utils;
using namespace std::literals;
namespace fs = std::filesystem;


void DeleteDirectoryRecursive(const fs::path& dir)
{
	std::error_code ec;
	fs::remove_all(dir, ec);
	if (!ec)
		return;

	// remove_all gives up at the first read-only file, and git's object files are
	// read-only. Clear the bit on disk - fs::permissions, not file_status's setter,
	// which only edits a copy - then remove again.
	std::error_code walk;
	for (auto it = fs::recursive_directory_iterator(dir, walk); !walk && it != fs::recursive_directory_iterator(); it.increment(walk))
	{
		std::error_code ignored;
		if (!it->is_directory(ignored))
			fs::permissions(it->path(), fs::perms::owner_write, fs::perm_options::add, ignored);
	}

	ec.clear();
	fs::remove_all(dir, ec);
	if (ec)
		std::cerr << "remove(" << dir << ") failed: " << ec.message() << std::endl;
}

// Per-process sequence for CAutoTempDir names; the pid separates processes.
std::atomic<unsigned> g_nextTempDir{ 0 };

export class CAutoTempDir // NOLINT(*-special-member-functions)
{
	constexpr static auto retries = 10;
	fs::path m_tempdir;

public:
	CAutoTempDir() {
		if (m_tempdir.empty())
			m_tempdir = std::filesystem::temp_directory_path();
		// This used to be GetTempFileName + DeleteFile + CreateDirectory. That is a
		// race: GetTempFileName's uniqueness guarantee comes from the *file* it
		// creates, so deleting it reopens the name to anyone else asking at the same
		// moment - and CreateDirectory's result was not checked, so a loser of that
		// race silently ended up pointing at somebody else's directory. Harmless
		// while the suite ran in one process; not harmless once it runs in eight,
		// where it showed up as an unrelated test failing about one run in five.
		//
		// The process id makes collisions between processes impossible rather than
		// unlikely, and the loop covers reuse within one process.
		std::error_code ec;
		for (const auto _ : std::views::iota(0, retries))
		{
			// Not std::tmpnam: the UCRT returns a *full path* in %TEMP%, so appending it
			// built "%TEMP%\tgit-tests-{}C:\Users\...", which no attempt could create.
			const auto uniqnam = std::format(L"tgit-tests-{}-{}", _getpid(), g_nextTempDir++);
			if (
				const auto candidate = m_tempdir / uniqnam;
				fs::create_directory(candidate, ec)
			) {
				m_tempdir = candidate;
				return;
			}
		}
		// ec is clear when every candidate merely existed; exiting with 0 would read as success
		std::exit(ec ? ec.value() : EXIT_FAILURE);
	}

	~CAutoTempDir() { try { DeleteDirectoryRecursive(m_tempdir); } catch (...) {} } // NOLINT(*-empty-catch)

	[[nodiscard]] fs::path GetTempDir() const { return m_tempdir; }

	/// The tests' spelling for it; git's object files are read-only, which fs::remove_all alone trips over.
	static void DeleteDirectoryRecursive(const fs::path& dir) { ::DeleteDirectoryRecursive(dir); }
};
