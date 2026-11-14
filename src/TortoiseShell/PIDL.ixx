module;
#include "stdafx.h"


export module PIDL;


export class PIDL {
	LPMALLOC malloc_{};

public:
	PIDL() { std::ignore = ::SHGetMalloc(&malloc_); }

	virtual ~PIDL() { malloc_->Release(); }
};