module;
#include <winerror.h>


export module AutoTempDir;
import std;
using namespace std::literals;
namespace fs = std::filesystem;

void DeleteDirectoryRecursive(const fs::path& dir)
{
	{
		std::error_code ec;
		fs::remove_all(dir, ec);
		if (ec && ec.value() != ERROR_ACCESS_DENIED)
			std::cerr << "remove " << dir << " failed: " << ec.message() << std::endl;
	}

	for (auto it : fs::recursive_directory_iterator(dir))
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

export class CAutoTempDir
{
	fs::path m_tempdir{};

public:
	~CAutoTempDir() { DeleteDirectoryRecursive(m_tempdir); }
	fs::path GetTempDir() const { return m_tempdir; }

	CAutoTempDir()
	: m_tempdir(std::filesystem::temp_directory_path())
	{
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
		for (int attempt = 0; attempt < 10; ++attempt)
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
		throw std::system_error(ec);
	}
};
