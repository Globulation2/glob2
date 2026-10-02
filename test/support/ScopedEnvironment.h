#include <Environment.h>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <SDL3/SDL.h>
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
		if (const char *previous = SDL_getenv_unsafe(name))
			original = previous;
#ifdef _WIN32
		if (const char *previous = std::getenv(name))
			originalCRT = previous;
		// SDL writes the Windows process environment; CRT readers have their
		// own cached view, used by the shader and UI-scale test overrides.
		if (_putenv_s(name, value) != 0)
		{
			restore();
			throw std::runtime_error("Cannot set test CRT environment variable " + this->name);
		}
#endif
		if (GAGCore::setProcessEnvironment(name, value, 1) != 0)
		{
			restore();
			throw std::runtime_error("Cannot set test environment variable " + this->name);
		}
	}
	~ScopedEnvironment() { restore(); }
	ScopedEnvironment(const ScopedEnvironment &) = delete;
	ScopedEnvironment &operator=(const ScopedEnvironment &) = delete;

private:
	void restore() noexcept
	{
#ifdef _WIN32
		// Restore CRT first: SDL then restores the process view independently,
		// including when the two views differed before this scope.
		_putenv_s(name.c_str(), originalCRT ? originalCRT->c_str() : "");
#endif
		if (original)
			GAGCore::setProcessEnvironment(name.c_str(), original->c_str(), 1);
		else
		{
#ifdef _WIN32
            SDL_setenv_unsafe(name.c_str(), "", 1);
#else
            unsetenv(name.c_str());
#endif
            SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), name.c_str());
		}
	}
	std::string name;
	std::optional<std::string> original;
#ifdef _WIN32
	std::optional<std::string> originalCRT;
#endif
};
}
