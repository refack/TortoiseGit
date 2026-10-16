// Gitdll - git but as a dll
// Copyright (C) 2026 - Refael Ackeramnn
#pragma once

#define TGIT_VERMAJOR		3
#define TGIT_VERMINOR		0
#define TGIT_VERMICRO		0
#define TGIT_VERBUILD		0
#define TGIT_VERDATE		__DATE__

#define FILEVER TGIT_VERMAJOR, TGIT_VERMINOR, TGIT_VERMICRO, TGIT_VERBUILD
#define PRODUCTVER FILEVER
#define STRINGIFY(A)		#A
#define MAKEVERSIONSTR(delim, a, b, c, d) STRINGIFY(a) delim STRINGIFY(b) delim STRINGIFY(c) delim STRINGIFY(d)
#define STRFILEVER			MAKEVERSIONSTR(".", TGIT_VERMAJOR, TGIT_VERMINOR, TGIT_VERMICRO, TGIT_VERBUILD)
#define STRPRODUCTVER		STRFILEVER

#if _M_ARM64
#define TGIT_PLATFORM		"ARM64"
#else
#define TGIT_PLATFORM		"x86_64"
#endif

#define PREVIEW				0
#define PREVIEW_INFO		"yyyymmdd-hash"
