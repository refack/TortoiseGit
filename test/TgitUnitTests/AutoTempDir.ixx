module;
#include <memory>

export module AutoTempDir;
import std;
import str_utils;
using namespace std::literals;
namespace fs = std::filesystem;


// something something something MFC/ATL
constexpr auto ERROR_ACCESS_DENIED = 5L;


void DeleteDirectoryRecursive(const fs::path& dir)
{
	{
		std::error_code ec;
		fs::remove_all(dir, ec);
		if (ec && ec.value() != ERROR_ACCESS_DENIED)
			std::cerr << "remove " << dir << " failed: " << ec.message() << std::endl;
	}

	for (const auto& it : fs::recursive_directory_iterator(dir))
	{
		if (it.is_directory())
			continue;

		// remove READONLY
		it.status().permissions(it.status().permissions() & ~fs::perms::owner_write);

		std::error_code ec;
		fs::remove(it, ec);
		if (ec)
			std::cerr << "remove(" << it.path() << ") failed: " << ec.message() << std::endl;
	};

	{
		// pickup any empty directories
		std::error_code ec;
		fs::remove_all(dir, ec);
		if (ec && ec.value() != ERROR_ACCESS_DENIED)
			std::cerr << "remove(" << dir << ") failed: " << ec.message() << std::endl;
	}
}

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

			// ReSharper disable once CppDeprecatedEntity
			const auto uniqnam = "tgit-tests-{}"s + std::tmpnam(nullptr);
			if (
				const auto candidate = m_tempdir / uniqnam;
				fs::create_directory(candidate, ec)
			) {
				m_tempdir = candidate;
				return;
			}
		}
		std::exit(ec.value());
	}

	~CAutoTempDir() { try { DeleteDirectoryRecursive(m_tempdir); } catch (...) {} } // NOLINT(*-empty-catch)

	[[nodiscard]] fs::path GetTempDir() const { return m_tempdir; }

	/// The tests' spelling for it; git's object files are read-only, which fs::remove_all alone trips over.
	static void DeleteDirectoryRecursive(const fs::path& dir) { ::DeleteDirectoryRecursive(dir); }
};
