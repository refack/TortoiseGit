module;
#include <winnls.h>
#define PDICT_MAX_WORD_LENGTH 50


export module PersonalDictionary;
import std;
import wstr;
import PathUtils;
using namespace PathUtils;


/**
 * \ingroup Utils
 * Utility class for handling a dictionary with custom words.
 */
export class CPersonalDictionary
{
public:
	explicit CPersonalDictionary(const LONG lLanguage = 0): m_lLanguage(lLanguage) {}
	~CPersonalDictionary() = default;

	bool AddWord(std::wstring_view sWord);
	bool FindWord(std::wstring_view sWord);
	[[nodiscard]] bool Save() const;
	void Init(const LONG lLanguage = 0) {m_lLanguage = lLanguage;}

private:
	bool Load();

	LONG m_lLanguage;
	bool m_bLoaded = false;
	std::set<std::wstring> dict{};
};

template<class T>
static void OpenFileStream(T& file, LONG lLanguage, std::ios_base::openmode openmode = 0)
{
	const std::wstring path = std::format(L"{}{}.dic", CPathUtils::GetAppDataDirectory(), !lLanguage ? GetUserDefaultLCID() : lLanguage);

	const auto filepath = CUnicodeUtils::StdGetMulti(path);

	file.open(filepath.c_str(), openmode);
}

bool CPersonalDictionary::Load()
{

	if (m_bLoaded)
		return true;

	std::ifstream File;
	OpenFileStream(File, m_lLanguage);
	if (!File.good())
	{
		return false;
	}

	for (std::string line; std::getline(File, line);)
	{
		auto sWord = CUnicodeUtils::StdGetUnicode(line);
		CUnicodeUtils::Trim(sWord);
		if (sWord.empty() || sWord.length() >= PDICT_MAX_WORD_LENGTH)
			continue;
		dict.insert(std::move(sWord));
	}
	m_bLoaded = true;
	return true;
}

bool CPersonalDictionary::AddWord(std::wstring_view sWord)
{
	if (!m_bLoaded)
		Load();
	if (sWord.length() >= PDICT_MAX_WORD_LENGTH || sWord.empty())
		return false;
	dict.emplace(sWord);
	return true;
}

bool CPersonalDictionary::FindWord(std::wstring_view sWord)
{
	if (!m_bLoaded)
		Load();
	// even if the load failed for some reason, we mark it as loaded
	// and just assume an empty personal dictionary
	m_bLoaded = true;
	const auto it = dict.find(std::wstring{sWord});
	return it != dict.end();
}

bool CPersonalDictionary::Save() const
{
	if (!m_bLoaded)
		return false;
	std::ofstream File;
	OpenFileStream(File, m_lLanguage, std::ios::ios_base::binary);
	for (const auto& line : dict)
		File << CUnicodeUtils::StdGetUTF8(line) << "\n";
	File.close();
	return true;
}
