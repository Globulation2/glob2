// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL_stdinc.h>
#include <cstdlib>

namespace GAGCore
{
// Startup and test configuration uses the process environment, including code
// that calls getenv directly. SDL3 also caches an environment for its drivers;
// keep both copies current when changing configuration on the main thread.
inline int setProcessEnvironment(const char *name, const char *value, int overwrite)
{
	if (SDL_setenv_unsafe(name, value, overwrite) < 0)
		return -1;
	const char *actual = SDL_getenv_unsafe(name);
#ifdef _WIN32
	// SDL updates the Win32 environment, while the C runtime retains its own
	// table. Keep getenv callers in the same process in sync as well.
	if (actual && _putenv_s(name, actual) != 0)
		return -1;
#endif
	return actual && SDL_SetEnvironmentVariable(SDL_GetEnvironment(), name, actual, true) ? 0 : -1;
}
} // namespace GAGCore
