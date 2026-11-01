// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2001-2008 Stephane Magnenat & Luc-Olivier de Charrière

#include "TextSort.h"

extern "C"
{
	#include "natsort/strnatcmp.c"
};

namespace GAGCore
{
	bool naturalStringSort(const std::string& lhs, const std::string& rhs)
	{
		int val = strnatcasecmp(lhs.c_str(), rhs.c_str());
		if(val == 1)
			return false;
		return true;
	}
}

