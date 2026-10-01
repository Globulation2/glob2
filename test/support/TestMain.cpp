// SPDX-License-Identifier: GPL-3.0-or-later
// Entry point shared by glob2-engine-tests and glob2-unit-tests. Makes a bare
// invocation from a shell safe: the process always runs in a disposable profile with
// SDL's dummy drivers unless the caller (normally test/run_tests.py) decides otherwise.
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL.h>
#define DOCTEST_CONFIG_IMPLEMENT
#include "Glob2Test.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace
{
	std::filesystem::path selfMadeProfile;

	void install()
	{
		const char* profile = std::getenv("GLOB2_USER_DATA_DIR");
		if (!profile || !*profile)
		{
			selfMadeProfile = std::filesystem::temp_directory_path() / ("glob2-tests-" + std::to_string(static_cast<long long>(
#ifdef _WIN32
				_getpid()
#else
				getpid()
#endif
			)));
			std::filesystem::create_directories(selfMadeProfile);
			SDL_setenv("GLOB2_USER_DATA_DIR", selfMadeProfile.string().c_str(), 1);
		}
		// GLOB2_TEST_DISPLAY=1 (set by the runner for [display] cases) keeps the real
		// video driver; everything else renders nowhere.
		const char* display = std::getenv("GLOB2_TEST_DISPLAY");
		if (!display || !*display || std::string(display) == "0")
		{
			SDL_setenv("SDL_VIDEODRIVER", "dummy", 0);
			SDL_setenv("SDL_RENDER_DRIVER", "software", 0);  // the dummy driver has no accelerated renderer
		}
		SDL_setenv("SDL_AUDIODRIVER", "dummy", 0);
	}

	void teardown()
	{
		if (selfMadeProfile.empty()) return;
		const char* keep = std::getenv("GLOB2_TEST_KEEP_PROFILE");
		if (keep && *keep && std::string(keep) != "0") return;
		std::error_code ignored;
		std::filesystem::remove_all(selfMadeProfile, ignored);
	}
}

int main(int argc, char** argv)
{
	// Line-buffered output interleaves correctly with doctest's reporter when captured.
	std::setvbuf(stdout, nullptr, _IOLBF, 0);
	std::setvbuf(stderr, nullptr, _IOLBF, 0);
    std::fprintf(stderr, "GLOB2_TEST_BUILD compiler=%s platform=%s pointer_bits=%zu\n",
#ifdef __VERSION__
        __VERSION__,
#else
        "unknown",
#endif
        SDL_GetPlatform(),sizeof(void*)*8);
	SDL_SetMainReady();
	install();
	doctest::Context context(argc, argv);
	context.setOption("no-breaks", true);
	const int result = context.run();
	teardown();
	return result;
}
