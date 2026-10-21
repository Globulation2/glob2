// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <StringTable.h>
#include <Toolkit.h>
#include <string>

namespace Glob2UI
{
inline std::string libraryText(const std::string &text)
{
	const std::string key = "[library " + text + "]";
	auto *strings = GAGCore::Toolkit::getStringTable();
	return strings && strings->doesStringExist(key) ? strings->getString(key) : text;
}
} // namespace Glob2UI
