// SPDX-License-Identifier: GPL-3.0-or-later
#include "ThumbSide.h"
#include "GlobalContainer.h"

namespace ThumbSide
{
bool left()
{
	return globalContainer->settings.thumbSide == Settings::THUMB_LEFT;
}
} // namespace ThumbSide
