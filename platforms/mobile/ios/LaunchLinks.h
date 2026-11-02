// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
// Universal links (https://<official instance>/j/<code>) arrive as an
// NSUserActivity, which SDL does not forward. The app delegate keeps the newest
// one; Online::pump() takes it on the game thread. glob2:// links arrive
// through SDL's openURL handling as SDL_EVENT_DROP_FILE instead.
std::string iosTakeLaunchLink();
