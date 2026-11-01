// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "online/ApiErrorText.h"
#include <StringTable.h>
#include <Toolkit.h>

namespace Glob2UI
{
inline std::string onlineErrorLocale()
{
	auto *strings = GAGCore::Toolkit::getStringTable();
	return strings && strings->doesStringExist("[language-code]")
		? strings->getString("[language-code]") : "en";
}
inline std::string apiErrorText(const Online::ApiError &error)
{
	return Online::presentApiError(error, onlineErrorLocale());
}
inline std::string onlineProblemText(const std::string &diagnostic)
{
	return Online::presentOnlineProblem(diagnostic, onlineErrorLocale());
}
} // namespace Glob2UI
