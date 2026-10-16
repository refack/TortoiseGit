// TortoiseGitMerge - a Diff/Patch program

// Copyright (C) 2023 - TortoiseGit
// Copyright (C) 2006-2007, 2012-2016, 2019, 2023 - TortoiseSVN

// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//
#pragma once
#include "EOL.h"
#include <deque>
#include <regex>
#include <optional>
#include <string>
#include <string_view>

// A template class to make an array which looks like a CStringArray or CDWORDArray but
// is in fact based on a STL vector, which is much faster at large sizes
template <typename T> class CStdArrayV
{
public:
	int GetCount() const { return static_cast<int>(m_vec.size()); }
	const T& GetAt(int index) const { return m_vec[index]; }
	void RemoveAt(int index)	{ m_vec.erase(m_vec.begin()+index); }
	void InsertAt(int index, const T& strVal)	{ m_vec.insert(m_vec.begin()+index, strVal); }
	void InsertAt(int index, const T& strVal, int nCopies)	{ m_vec.insert(m_vec.begin()+index, nCopies, strVal); }
	void SetAt(int index, const T& strVal)	{ m_vec[index] = strVal; }
	void Add(const T& strVal)	{
		if (m_vec.size()==m_vec.capacity()) {
			m_vec.reserve(m_vec.capacity() ? m_vec.capacity()*2 : 256);
		}
		m_vec.push_back(strVal);
	}
	void RemoveAll()				{ m_vec.clear(); }
	void Reserve(int nHintSize)		{ m_vec.reserve(nHintSize); }

private:
	std::vector<T> m_vec;
};

// A template class to make an array which looks like a CStringArray or CDWORDArray but
// is in fact based on a STL deque, which is much faster at large sizes
template <typename T> class CStdArrayD
{
public:
	int GetCount() const { return static_cast<int>(m_vec.size()); }
	const T& GetAt(int index) const { return m_vec[index]; }
	void RemoveAt(int index)    { m_vec.erase(m_vec.begin()+index); }
	void InsertAt(int index, const T& strVal)   { m_vec.insert(m_vec.begin()+index, strVal); }
	void InsertAt(int index, const T& strVal, int nCopies)  { m_vec.insert(m_vec.begin()+index, nCopies, strVal); }
	void SetAt(int index, const T& strVal)  { m_vec[index] = strVal; }
	void Add(const T& strVal)    { m_vec.push_back(strVal); }
	void RemoveAll()             { m_vec.clear(); }

private:
	std::deque<T> m_vec;
};

using CStdDWORDArray = CStdArrayV<DWORD>;

struct CFileTextLine {
	CString				sLine;
	EOL					eEnding = EOL::AutoLine;
};
using CStdFileLineArray = CStdArrayD<CFileTextLine>;
/**
 * \ingroup TortoiseMerge
 *
 * Represents an array of text lines which are read from a file.
 * This class is also responsible for determining the encoding of
 * the file (e.g. UNICODE(UTF16), UTF8, ASCII, ...).
 */
class CFileTextLines  : public CStdFileLineArray
{
public:
	CFileTextLines() = default;
	~CFileTextLines() = default;

	enum class UnicodeType
	{
		AUTOTYPE,
		BINARY,
		ASCII,
		UTF16_LE,		//=1200,
		UTF16_BE,		//=1201,
		UTF16_LEBOM,	//=1200,
		UTF16_BEBOM,	//=1201,
		UTF32_LE,		//=12000,
		UTF32_BE,		//=12001,
		UTF8,			//=65001,
		UTF8BOM,		//=UTF8+65536,
	};

	struct SaveParams {
		UnicodeType		m_UnicodeType = CFileTextLines::UnicodeType::AUTOTYPE;
		EOL				m_LineEndings = EOL::AutoLine;
	};

	/**
	 * Loads the text file and adds each line to the array
	 * \param sFilePath the path to the file
	 * \param lengthHint hint to create line array
	 */
	BOOL			Load(const CString& sFilePath, int lengthHint = 0);
	/**
	 * Saves the whole array of text lines to a file, preserving
	 * the line endings detected at Load()
	 * \param sFilePath the path to save the file to
	 * \param bSaveAsUTF8 enforce encoding for save
	 * \param bUseSVNCompatibleEOLs limit EOLs to CRLF, CR and LF, last one is used instead of all others
	 * \param dwIgnoreWhitespaces "enum" mode of removing whitespaces
	 * \param bIgnoreCase converts whole file to lower case
	 * \param bBlame limit line len
	 */
	 BOOL Save(const CString& sFilePath
			 , bool bSaveAsUTF8 = false
			 , bool bUseSVNCompatibleEOLs = false
			 , DWORD dwIgnoreWhitespaces = 0
			 , BOOL bIgnoreCase = FALSE
			 , bool bBlame = false
			 , bool bIgnoreComments = false
			 , const CString& linestart = CString()
			 , const CString& blockstart = CString()
			 , const CString& blockend = CString()
			 , const std::wregex& rx = std::wregex()
			 , const std::wstring& replacement = L"");
	/**
	 * Returns an error string of the last failed operation
	 */
	CString			GetErrorString() const  {return m_sErrorString;}
	/**
	 * Copies the settings of a file like the line ending styles
	 * to another CFileTextLines object.
	 */
	void			CopySettings(CFileTextLines * pFileToCopySettingsTo) const;

	bool			NeedsConversion() const { return m_bNeedsConversion; }
	UnicodeType		GetUnicodeType() const  {return m_SaveParams.m_UnicodeType;}
	EOL				GetLineEndings() const {return m_SaveParams.m_LineEndings;}

	void			Add(const CString& sLine, EOL ending) { CFileTextLine temp={sLine, ending}; CStdFileLineArray::Add(temp); }
	void			InsertAt(int index, const CString& strVal, EOL ending) { CFileTextLine temp={strVal, ending}; CStdFileLineArray::InsertAt(index, temp); }

	const CString&	GetAt(int index) const { return CStdFileLineArray::GetAt(index).sLine; }
	EOL				GetLineEnding(int index) const { return CStdFileLineArray::GetAt(index).eEnding; }
	void			SetSaveParams(const SaveParams& sp) { m_SaveParams = sp; }
	SaveParams		GetSaveParams() const { return m_SaveParams; }
	void			KeepEncoding(bool bKeep = true) { m_bKeepEncoding = bKeep; }
	//void				SetLineEnding(int index, EOL ending) { CStdFileLineArray::GetAt(index).eEnding = ending; }

	static const wchar_t * GetEncodingName(UnicodeType);

	/**
	 * Checks the Unicode type in a text buffer
	 * Must be public for TortoiseGitBlame
	 * \param pBuffer pointer to the buffer containing text
	 * \param cb size of the text buffer in bytes
	 */
	UnicodeType CheckUnicodeType(LPCVOID pBuffer, int cb);

private:
	void			SetErrorString();

	static void		StripWhiteSpace(CString& sLine, DWORD dwIgnoreWhitespaces, bool blame);
	bool			StripComments(CString& sLine, bool bInBlockComment);
	bool			IsInsideString(const CString& sLine, int pos);
	void			LineRegex(CString& sLine, const std::wregex& rx, const std::wstring& replacement) const;


private:
	CString				m_sErrorString;
	bool				m_bNeedsConversion = false;
	bool				m_bKeepEncoding = false;
	SaveParams			m_SaveParams;
	CString				m_sCommentLine;
	CString				m_sCommentBlockStart;
	CString				m_sCommentBlockEnd;
};



/// Turns a file's bytes into text. Stateless: the result is returned, not kept.
class CDecodeFilter
{
public:
	CDecodeFilter() = default;
	CDecodeFilter(const CDecodeFilter&) = delete;
	CDecodeFilter& operator=(const CDecodeFilter& Src) = delete;
	virtual ~CDecodeFilter() = default;

	/// The text, or nullopt when the bytes do not decode.
	virtual std::optional<std::wstring> Decode(std::string_view bytes) const = 0;
};

/// Turns text into a file's bytes and writes them.
class CEncodeFilter
{
public:
	CEncodeFilter(CStdioFile* p_File)
		: m_pFile(p_File)
	{
	}
	virtual ~CEncodeFilter() = default;

	/// The encoded bytes, in a buffer reused from call to call - Save encodes
	/// every line, so this keeps one allocation instead of one per line. Valid
	/// until the next Encode; copy it to keep it.
	virtual const std::string& Encode(std::wstring_view text) = 0;
	void Write(std::wstring_view text) { WriteBytes(Encode(text)); } ///< encode and write
	void WriteBytes(const std::string& bytes) { if (!bytes.empty()) m_pFile->Write(bytes.data(), static_cast<UINT>(bytes.size())); } ///< write already-encoded bytes

protected:
	std::string m_encoded;

private:
	CStdioFile * m_pFile;
};


class CAsciiFilter : public CEncodeFilter, public CDecodeFilter
{
public:
	CAsciiFilter(CStdioFile* pFile)
		: CEncodeFilter(pFile)
		, m_nCodePage(CP_ACP)
	{
	}
	std::optional<std::wstring> Decode(std::string_view bytes) const override;
	const std::string& Encode(std::wstring_view text) override;

protected:
	/**
	Code page for MultiByteToWideChar.
	*/
	UINT m_nCodePage;
};


class CUtf8Filter : public CAsciiFilter
{
public:
	CUtf8Filter(CStdioFile* pFile)
		: CAsciiFilter(pFile)
	{
		m_nCodePage = CP_UTF8;
	}
};


class CUtf16leFilter : public CEncodeFilter, public CDecodeFilter
{
public:
	CUtf16leFilter(CStdioFile* pFile)
		: CEncodeFilter(pFile)
	{}

	std::optional<std::wstring> Decode(std::string_view bytes) const override;
	const std::string& Encode(std::wstring_view text) override;
};


class CUtf16beFilter : public CUtf16leFilter
{
public:
	CUtf16beFilter(CStdioFile *pFile) : CUtf16leFilter(pFile){}

	std::optional<std::wstring> Decode(std::string_view bytes) const override;
	const std::string& Encode(std::wstring_view text) override;
};


class CUtf32leFilter : public CEncodeFilter, public CDecodeFilter
{
public:
	CUtf32leFilter(CStdioFile* pFile)
		: CEncodeFilter(pFile)
	{}

	std::optional<std::wstring> Decode(std::string_view bytes) const override;
	const std::string& Encode(std::wstring_view text) override;
};


class CUtf32beFilter : public CUtf32leFilter
{
public:
	CUtf32beFilter(CStdioFile *pFile) : CUtf32leFilter(pFile){}

	std::optional<std::wstring> Decode(std::string_view bytes) const override;
	const std::string& Encode(std::wstring_view text) override;
};
