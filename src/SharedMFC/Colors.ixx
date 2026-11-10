module;
#include <Windows.h>
#include <cassert>

export module Colors;
import std;
import Registry;


/**
 * A UI color stylesheet. Instances are real state (one palette each); the
 * default-constructed sheet is backed by the registry keys under
 * Software\TortoiseGit\Colors. How alternate sheets serialize is TBD.
 *
 * g_Colors below is the process working set.
 */
export class CColors
{
public:
	enum Colors
	{
		Cmd,
		Conflict,
		Modified,
		Merged,
		Deleted,
		Added,
		LastCommit,
		NoteNode,
		Renamed,
		LastCommitNode,
		PropertyChanged,
		CurrentBranch,
		LocalBranch,
		RemoteBranch,
		Tag,
		Stash,
		BranchLine1,
		BranchLine2,
		BranchLine3,
		BranchLine4,
		BranchLine5,
		BranchLine6,
		BranchLine7,
		BranchLine8,
		BisectGood,
		BisectBad,
		BisectSkip,
		OtherRef,
		FilterMatch,
		COLOR_END = -1,
	};

	CColors();

	COLORREF GetColor(Colors col, bool bDefault = false);
	void SetColor(Colors col, COLORREF cr);

	//mixFactor: 0 -> baseColor, 255 -> newColor
	static COLORREF MixColors(COLORREF baseColor, COLORREF newColor, unsigned char mixFactor);

	static COLORREF Lighten(COLORREF baseColor, unsigned char amount = 100);
	static COLORREF Darken(COLORREF baseColor, unsigned char amount = 100);

private:
	struct COLOR_DATA
	{
		Colors		Color;
		CRegDWORD	RegKey;
	};

	std::array<COLOR_DATA, FilterMatch + 1> m_ColorArray;
};

/// the process working set
export CColors g_Colors;	// NOLINT(*-throwing-static-initialization)


module :private;

CColors::CColors()
	: m_ColorArray{{
		{ .Color = Cmd,				.RegKey = CRegDWORD(L"Software\\TortoiseGit\\Colors\\Cmd", RGB(100, 100, 100)) },
		{ .Color = Conflict,		.RegKey = CRegDWORD(L"Software\\TortoiseGit\\Colors\\Conflict", RGB(255, 0, 0)) },
		{ .Color = Modified,		.RegKey = CRegDWORD(L"Software\\TortoiseGit\\Colors\\Modified", RGB(0, 50, 160)) },
		{ .Color = Merged,			.RegKey = CRegDWORD(L"Software\\TortoiseGit\\Colors\\Merged", RGB(0, 100, 0)) },
		{ .Color = Deleted,			.RegKey = CRegDWORD(L"Software\\TortoiseGit\\Colors\\Deleted", RGB(100, 0, 0)) },
		{ .Color = Added,			.RegKey = CRegDWORD(L"Software\\TortoiseGit\\Colors\\Added", RGB(100, 0, 100)) },
		{ .Color = LastCommit,		.RegKey = CRegDWORD(L"Software\\TortoiseGit\\Colors\\LastCommit", RGB(100, 100, 100)) },
		{ .Color = NoteNode,		.RegKey = CRegDWORD(L"Software\\TortoiseGit\\Colors\\NoteNode", RGB(160, 160, 0)) },
		{ .Color = Renamed,			.RegKey = CRegDWORD(L"Software\\TortoiseGit\\Colors\\Renamed", RGB(0, 0, 255)) },
		{ .Color = LastCommitNode,	.RegKey = CRegDWORD(L"Software\\TortoiseGit\\Colors\\LastCommitNode", RGB(200, 200, 200)) },
		{ .Color = PropertyChanged,	.RegKey = CRegDWORD(L"Software\\TortoiseGit\\Colors\\PropertyChanged", RGB(0, 50, 160)) },
		{ .Color = CurrentBranch,	.RegKey = CRegDWORD(L"Software\\TortoiseGit\\Colors\\CurrentBranch", RGB(200, 0, 0)) },
		{ .Color = LocalBranch,		.RegKey = CRegDWORD(L"Software\\TortoiseGit\\Colors\\LocalBranch", RGB(0, 195, 0)) },
		{ .Color = RemoteBranch,	.RegKey = CRegDWORD(L"Software\\TortoiseGit\\Colors\\RemoteBranch", RGB(255, 221, 170)) },
		{ .Color = Tag,				.RegKey = CRegDWORD(L"Software\\TortoiseGit\\Colors\\Tag", RGB(255, 255, 0)) },
		{ .Color = Stash,			.RegKey = CRegDWORD(L"Software\\TortoiseGit\\Colors\\Stash", RGB(128, 128, 128)) },
		{ .Color = BranchLine1,		.RegKey = CRegDWORD(L"Software\\TortoiseGit\\Colors\\BranchLine1", RGB(0, 0, 0)) },
		{ .Color = BranchLine2,		.RegKey = CRegDWORD(L"Software\\TortoiseGit\\Colors\\BranchLine2", RGB(0xFF, 0, 0)) },
		{ .Color = BranchLine3,		.RegKey = CRegDWORD(L"Software\\TortoiseGit\\Colors\\BranchLine3", RGB(0, 0xFF, 0)) },
		{ .Color = BranchLine4,		.RegKey = CRegDWORD(L"Software\\TortoiseGit\\Colors\\BranchLine4", RGB(0, 0, 0xFF)) },
		{ .Color = BranchLine5,		.RegKey = CRegDWORD(L"Software\\TortoiseGit\\Colors\\BranchLine5", RGB(128, 128, 128)) },
		{ .Color = BranchLine6,		.RegKey = CRegDWORD(L"Software\\TortoiseGit\\Colors\\BranchLine6", RGB(128, 128, 0)) },
		{ .Color = BranchLine7,		.RegKey = CRegDWORD(L"Software\\TortoiseGit\\Colors\\BranchLine7", RGB(0, 128, 128)) },
		{ .Color = BranchLine8,		.RegKey = CRegDWORD(L"Software\\TortoiseGit\\Colors\\BranchLine8", RGB(128, 0, 128)) },
		{ .Color = BisectGood,		.RegKey = CRegDWORD(L"Software\\TortoiseGit\\Colors\\BisectGood", RGB(0, 100, 200)) },
		{ .Color = BisectBad,		.RegKey = CRegDWORD(L"Software\\TortoiseGit\\Colors\\BisectBad", RGB(255, 0, 0)) },
		{ .Color = BisectSkip,		.RegKey = CRegDWORD(L"Software\\TortoiseGit\\Colors\\BisectSkip", RGB(192, 192, 192)) },
		{ .Color = OtherRef,		.RegKey = CRegDWORD(L"Software\\TortoiseGit\\Colors\\OtherRef", RGB(224, 224, 224)) },
		{ .Color = FilterMatch,		.RegKey = CRegDWORD(L"Software\\TortoiseGit\\Colors\\FilterMatch", RGB(200, 0, 0)) },
	}}
{
}

COLORREF CColors::GetColor(const Colors col, const bool bDefault)
{
	if (col == COLOR_END)
		return RGB(0, 0, 0);
	if (bDefault)
		return m_ColorArray[col].RegKey.defaultValue();
	else
		return m_ColorArray[col].RegKey;
}

void CColors::SetColor(const Colors col, const COLORREF cr)
{
	if (col == COLOR_END)
	{
		assert(false);
		return;
	}
	m_ColorArray[col].RegKey = cr;
}


COLORREF CColors::MixColors(COLORREF baseColor, const COLORREF newColor, const unsigned char mixFactor)
{
	short colRed;
	short colGreen;
	short colBlue;
	colRed   = static_cast<short>(static_cast<float>( baseColor & 0x000000FF)        - static_cast<float>( newColor & 0x000000FF)       ) * mixFactor / 0xFF; // red
	colGreen = static_cast<short>(static_cast<float>((baseColor & 0x0000FF00) >>  8) - static_cast<float>((newColor & 0x0000FF00) >>  8)) * mixFactor / 0xFF; // green
	colBlue  = static_cast<short>(static_cast<float>((baseColor & 0x00FF0000) >> 16) - static_cast<float>((newColor & 0x00FF0000) >> 16)) * mixFactor / 0xFF; // blue

	colRed   = ( baseColor&0x000000FF)		-colRed;
	colGreen = ((baseColor&0x0000FF00)>>8)	-colGreen;
	colBlue  = ((baseColor&0x00FF0000)>>16) -colBlue;
	baseColor = static_cast<int>(colRed) | (static_cast<int>(colGreen) << 8) | (static_cast<int>(colBlue) << 16);
	return baseColor;
}

COLORREF CColors::Lighten(const COLORREF baseColor, const unsigned char amount)
{
	return MixColors(baseColor, RGB(255, 255, 255), amount);
}

COLORREF CColors::Darken(const COLORREF baseColor, const unsigned char amount)
{
	return MixColors(baseColor, RGB(0, 0, 0), amount);
}
