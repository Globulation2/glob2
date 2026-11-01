// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "online/ApiErrorText.h"
#include "ui/PresentedError.h"
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
inline std::string exceptionText(const std::exception &error, const std::string &summary = {})
{
	const auto locale = onlineErrorLocale();
	return presentException(error, locale, summary.empty()
		? Online::presentOnlineProblem("", locale) : summary);
}
} // namespace Glob2UI
