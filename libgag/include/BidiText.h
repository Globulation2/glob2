// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 glob2 contributors

#pragma once

#include <string>

namespace GAGCore
{
	//! Turn one line of logical-order UTF-8 into the form SDL_ttf draws left to right,
	//! for builds without fribidi (Android, iOS, browser): Arabic and Persian letters
	//! become their joined presentation forms, and the Unicode bidirectional algorithm
	//! (implicit levels only, one line, mirrored brackets) orders the result for
	//! display. Text without right-to-left characters is returned unchanged.
	std::string visualOrder(const std::string &logical);
}
