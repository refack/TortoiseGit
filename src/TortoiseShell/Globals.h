// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2008-2012, 2019-2022, 2026 - TortoiseGit
// Copyright (C) 2003-2008 - TortoiseSVN

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
#include <type_traits>


#define ITEMIS_ONLYONE				0x00000001
#define ITEMIS_EXTENDED				0x00000002
#define ITEMIS_INGIT				0x00000004
#define ITEMIS_CONFLICTED			0x00000008
#define ITEMIS_FOLDER				0x00000010
#define ITEMIS_FOLDERINGIT			0x00000020
#define ITEMIS_NORMAL				0x00000040
#define ITEMIS_IGNORED				0x00000080
#define ITEMIS_INVERSIONEDFOLDER	0x00000100
#define ITEMIS_ADDED				0x00000200
#define ITEMIS_DELETED				0x00000400
#define ITEMIS_PATCHFILE			0x00001000
// #define ITEMIS_SHORTCUT			0x00002000 //unused
#define ITEMIS_PATCHINCLIPBOARD		0x00008000
#define ITEMIS_PATHINCLIPBOARD      0x00010000
#define ITEMIS_TWO					0x00020000
#define ITEMIS_SUBMODULECONTAINER	0x00040000
#define ITEMIS_GITSVN				0x00080000
#define ITEMIS_STASH				0x00100000
#define ITEMIS_WCROOT				0x00200000
#define ITEMIS_BISECT				0x00400000
#define ITEMIS_BAREREPO				0x00800000
#define ITEMIS_SUBMODULE			0x01000000
#define ITEMIS_MERGEACTIVE			0x02000000
#define ITEMIS_HASDIFFLATER         0x04000000
#define ITEMIS_INACCESSIBLE			0x08000000
