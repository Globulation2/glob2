// SPDX-License-Identifier: GPL-3.0-or-later
// Entry point shared by glob2-engine-tests and glob2-unit-tests. Makes a bare
// invocation from a shell safe: the process always runs in a disposable profile with
// SDL's dummy drivers unless the caller (normally test/run_tests.py) decides otherwise.
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <Environment.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#define DOCTEST_CONFIG_IMPLEMENT
#include "Glob2Test.h"
#include <glob2/TestBuildProvenance.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
	const char *profile = SDL_getenv_unsafe("GLOB2_USER_DATA_DIR");
	if (!profile || !*profile)
	{
		selfMadeProfile = std::filesystem::temp_directory_path() /
						  ("glob2-tests-" + std::to_string(static_cast<long long>(
#ifdef _WIN32
												_getpid()
#else
												getpid()
#endif
													)));
		std::filesystem::create_directories(selfMadeProfile);
		GAGCore::setProcessEnvironment("GLOB2_USER_DATA_DIR", selfMadeProfile.string().c_str(), 1);
	}
	// GLOB2_TEST_DISPLAY=1 (set by the runner for [display] cases) keeps the real
	// video driver; everything else renders nowhere.
	const char *display = SDL_getenv_unsafe("GLOB2_TEST_DISPLAY");
	if (!display || !*display || std::string(display) == "0")
	{
		GAGCore::setProcessEnvironment("SDL_VIDEODRIVER", "dummy", 0);
		GAGCore::setProcessEnvironment("SDL_RENDER_DRIVER", "software",
				   0); // the dummy driver has no accelerated renderer
	}
	GAGCore::setProcessEnvironment("SDL_AUDIODRIVER", "dummy", 0);
}

void teardown()
{
	if (selfMadeProfile.empty())
		return;
	const char *keep = SDL_getenv_unsafe("GLOB2_TEST_KEEP_PROFILE");
	if (keep && *keep && std::string(keep) != "0")
		return;
	std::error_code ignored;
	std::filesystem::remove_all(selfMadeProfile, ignored);
}
} // namespace

#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_OSX
extern "C" void* glob2BeginTestActivity();
extern "C" void glob2EndTestActivity(void*);
namespace
{
struct NativeTestActivity
{
    void* token = glob2BeginTestActivity();
    ~NativeTestActivity() { if (token) glob2EndTestActivity(token); }
};
}
#endif
#endif

int main(int argc, char **argv)
{
#if defined(__APPLE__) && TARGET_OS_OSX
    NativeTestActivity activity;
#endif

	// Line-buffered output interleaves correctly with doctest's reporter when captured.
	std::setvbuf(stdout, nullptr, _IOLBF, 0);
	std::setvbuf(stderr, nullptr, _IOLBF, 0);
	std::fprintf(stderr, "GLOB2_TEST_BUILD compiler=%s platform=%s pointer_bits=%zu\n",
#ifdef __VERSION__
				 __VERSION__,
#else
				 "unknown",
#endif
				 SDL_GetPlatform(), sizeof(void *) * 8);
	std::fprintf(stderr, "GLOB2_TEST_PROVENANCE %s\n", GLOB2_TEST_PROVENANCE_JSON);
	if (const char *artifacts = SDL_getenv_unsafe("GLOB2_TEST_ARTIFACTS_ROOT"))
	{
		std::filesystem::create_directories(artifacts);
		std::ofstream proof(std::filesystem::path(artifacts) / "build-provenance.json");
		proof << GLOB2_TEST_PROVENANCE_JSON << '\n';
		proof.close();
		if (!proof)
		{
			std::fprintf(stderr, "Cannot retain test build provenance in %s\n", artifacts);
			return 1;
		}
	}
	SDL_SetMainReady();
	install();
	doctest::Context context(argc, argv);
	context.setOption("no-breaks", true);
	const int result = context.run();
	teardown();
	return result;
}
