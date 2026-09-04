// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2008 Bradley Arsenault

#ifndef TextSort_h
#define TextSort_h

#include <string>

namespace GAGCore
{
	///This compares two strings using the natsort library created by Martin Pool
	bool naturalStringSort(const std::string& lhs, const std::string& rhs);
};

#endif
