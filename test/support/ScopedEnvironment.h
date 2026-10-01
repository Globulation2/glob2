// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL.h>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>

namespace glob2test
{
// Restore the original environment even if a rendering assertion aborts a case.
class ScopedEnvironment
{
public:
	ScopedEnvironment(const char *name, const char *value) : name(name)
	{
		if (const char *previous = SDL_getenv(name))
			original = previous;
		if (SDL_setenv(name, value, 1) != 0)
			throw std::runtime_error("Cannot set test environment variable " + this->name);
	}
	~ScopedEnvironment()
	{
		if (original)
			SDL_setenv(name.c_str(), original->c_str(), 1);
		else
		{
#ifdef _WIN32
			_putenv_s(name.c_str(), "");
#else
			unsetenv(name.c_str());
#endif
		}
	}
	ScopedEnvironment(const ScopedEnvironment &) = delete;
	ScopedEnvironment &operator=(const ScopedEnvironment &) = delete;

private:
	std::string name;
	std::optional<std::string> original;
};
}
