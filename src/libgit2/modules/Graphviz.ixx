module;
#include <atlstr.h>
#include <GdiPlus.h>

export module Graphviz;
import wstr;
import std;
using std::operator""sv;

export class Graphviz
{
public:
	void DrawNode(const CString& id, const CString& text, const CString& fontName, const int fontSize, Gdiplus::Color /*borderColor*/, Gdiplus::Color backColor, int /*height*/)
	{
		m_content.AppendChar(L'\t');
		m_content.Append(id);

		m_content.AppendFormat(L" [label=\"%s\"", text);
		if (m_defaultFontName != fontName)
			m_content.AppendFormat(L", fontname=\"%s\"", fontName);

		if (m_defaultFontSize != fontSize)
			m_content.AppendFormat(L", fontsize=\"%d\"", fontSize);

		if (m_defaultBackColor.GetValue() != backColor.GetValue())
			m_content.AppendFormat(L", color=\"#%06X\"", backColor.GetValue() & 0xffffff);

		m_content.Append(L"];\r\n");
	}

	void BeginDrawTableNode(const CString& id, const CString& fontName, const int fontSize, int /*height*/)
	{
		m_tableNodeNum = 0;
		m_content.AppendChar(L'\t');
		m_content.Append(id);

		m_content.AppendChar(L'[');
		bool hasAttr = false;
		if (m_defaultFontName != fontName)
		{
			m_content.AppendFormat(L"fontname=\"%s\"", fontName);
			hasAttr = true;
		}

		if (m_defaultFontSize != fontSize)
		{
			if (hasAttr)
				m_content.Append(L", ");
			m_content.AppendFormat(L"fontsize=\"%d\"", fontSize);
			hasAttr = true;
		}

		if (hasAttr)
			m_content.Append(L", ");
		m_content.Append(L"color=transparent");

		m_content.Append(L", label=<\r\n\t<table border=\"0\" cellborder=\"0\" cellpadding=\"5\">\r\n");
	}


	void DrawTableNode(const CString& text, const Gdiplus::Color backColor)
	{
		m_content.AppendFormat(L"\t<tr><td port=\"f%d\" bgcolor=\"#%06X\">%s</td></tr>\r\n", m_tableNodeNum++, backColor.GetValue() & 0xffffff, text);
	}

	void EndDrawTableNode()
	{
		m_content.Append(L"\t</table>\r\n\t>];\r\n");
	}

	void DrawEdge(const CString& from, const CString& to)
	{
		m_content.AppendChar(L'\t');
		m_content.Append(from);
		m_content.Append(L"->");
		m_content.Append(to);
		m_content.Append(L"\r\n");
	}

	bool Save(const CString &path) const
	{
		DWORD dwWritten = 0;
		const auto hFile = CreateFile(path, GENERIC_WRITE, FILE_SHARE_DELETE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (!hFile)
			return false;

		std::string header = "digraph G {\r\n\tgraph [rankdir=BT];\r\n\tnode [style=\"filled, rounded\", shape=box, fontname=\"Courier New\", fontsize=9, height=0.26, penwidth=0];\r\n";
		constexpr auto footer = "\r\n}"sv;

		if (!WriteFile(hFile, header.c_str(), static_cast<DWORD>(header.size()), &dwWritten, nullptr))
			return false;

		auto contentA = CUnicodeUtils::StdGetUTF8(m_content.GetString());
		if (!WriteFile(hFile, contentA.c_str(), static_cast<DWORD>(contentA.size()), &dwWritten, nullptr))
			return false;
		if (!WriteFile(hFile, footer.data(), static_cast<DWORD>(footer.size()), &dwWritten, nullptr))
			return false;

		return true;
	}

private:
	CString m_defaultFontName;
	int m_defaultFontSize = -1;
	Gdiplus::Color m_defaultBackColor;
	int m_defaultHeight = -1;
	int m_tableNodeNum = 0;
	CString m_content;
};
