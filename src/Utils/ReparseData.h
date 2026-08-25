// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2026 - TortoiseGit

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

// FSCTL_GET_REPARSE_POINT and friends; the struct below is only useful together with them
#include <winioctl.h>

/*
 * Layout of the buffer FSCTL_GET_REPARSE_POINT fills in, as documented at
 * https://learn.microsoft.com/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_reparse_data_buffer
 *
 * Windows declares REPARSE_DATA_BUFFER in ntifs.h, which ships with the driver kit
 * rather than the ordinary SDK, so applications that read reparse points have to
 * declare it themselves. The name is deliberately prefixed to avoid colliding with
 * the declaration in ntifs.h or in MinGW's headers, should either be pulled in.
 */
typedef struct _TGIT_REPARSE_DATA_BUFFER
{
	ULONG  ReparseTag;
	USHORT ReparseDataLength;
	USHORT Reserved;
	union
	{
		struct
		{
			USHORT SubstituteNameOffset;
			USHORT SubstituteNameLength;
			USHORT PrintNameOffset;
			USHORT PrintNameLength;
			ULONG  Flags;
			WCHAR  PathBuffer[1];
		} SymbolicLink;
		struct
		{
			USHORT SubstituteNameOffset;
			USHORT SubstituteNameLength;
			USHORT PrintNameOffset;
			USHORT PrintNameLength;
			WCHAR  PathBuffer[1];
		} MountPoint;
		struct
		{
			UCHAR DataBuffer[1];
		} Generic;
	} ReparseBuffer;
} TGIT_REPARSE_DATA_BUFFER;
