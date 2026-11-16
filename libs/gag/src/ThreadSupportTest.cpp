// SPDX-License-Identifier: GPL-3.0-or-later
#include "Glob2Test.h"
#include <ThreadSupport.h>
#include <cstdlib>
#include <string>
#include <tuple>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <pthread.h>
#include <sys/qos.h>
#elif defined(__linux__)
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

using GAGCore::ThreadSupport::ThreadRole;

namespace
{
// The calling thread's scheduling as the operating system reports it.
int observedScheduling()
{
#if defined(_WIN32)
	return GetThreadPriority(GetCurrentThread());
#elif defined(__APPLE__)
	return static_cast<int>(qos_class_self());
#elif defined(__linux__)
	return getpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)));
#else
	return 0;
#endif
}

int expectedScheduling(ThreadRole role)
{
#if defined(_WIN32)
	return role == ThreadRole::Interactive  ? THREAD_PRIORITY_ABOVE_NORMAL
	       : role == ThreadRole::Background ? THREAD_PRIORITY_BELOW_NORMAL
	                                        : THREAD_PRIORITY_NORMAL;
#elif defined(__APPLE__)
	return static_cast<int>(role == ThreadRole::Interactive  ? QOS_CLASS_USER_INTERACTIVE
	                        : role == ThreadRole::Background ? QOS_CLASS_UTILITY
	                                                         : QOS_CLASS_USER_INITIATED);
#elif defined(__linux__)
	return role == ThreadRole::Interactive ? -4 : role == ThreadRole::Background ? 5 : 2;
#else
	(void)role;
	return 0;
#endif
}

// Runs probe on a thread launched with role and returns what it observed.
template <class Probe> auto onLaunched(ThreadRole role, Probe probe)
{
	decltype(probe()) result{};
	auto thread = GAGCore::ThreadSupport::launch([&] { result = probe(); }, role);
	thread.join();
	return result;
}

struct EnvironmentOverride
{
	explicit EnvironmentOverride(const char *value)
	{
		if (const char *old = std::getenv("GLOB2_THREAD_PRIORITY"))
			previous = old, had = true;
		set(value);
	}
	~EnvironmentOverride() { set(had ? previous.c_str() : nullptr); }
	static void set(const char *value)
	{
#if defined(_WIN32)
		_putenv_s("GLOB2_THREAD_PRIORITY", value ? value : "");
#else
		if (value)
			setenv("GLOB2_THREAD_PRIORITY", value, 1);
		else
			unsetenv("GLOB2_THREAD_PRIORITY");
#endif
	}
	std::string previous;
	bool had = false;
};

// Linux roles are absolute nice values, which a process started above nice 0
// cannot reach without privilege; effect checks then have nothing to observe.
bool startedNiced()
{
#if defined(__linux__)
	return getpriority(PRIO_PROCESS, 0) > 0;
#else
	return false;
#endif
}
}

TEST_SUITE("ThreadSupport")
{
#if defined(_WIN32) || defined(__APPLE__) || defined(__linux__)
	TEST_CASE("background threads are scheduled below normal ones")
	{
		EnvironmentOverride enabled(nullptr);
		if (startedNiced())
			return;
		CHECK(onLaunched(ThreadRole::Background, observedScheduling) ==
		      expectedScheduling(ThreadRole::Background));
	}

	TEST_CASE("workers are scheduled between the core loop and background")
	{
		EnvironmentOverride enabled(nullptr);
		if (startedNiced())
			return;
		CHECK(onLaunched(ThreadRole::Worker, observedScheduling) == expectedScheduling(ThreadRole::Worker));
	}

	TEST_CASE("interactive applies where the platform allows raising priority")
	{
		EnvironmentOverride enabled(nullptr);
		if (startedNiced())
			return;
		const auto [applied, observed] = onLaunched(ThreadRole::Worker, [] {
			const bool applied = GAGCore::ThreadSupport::setCurrentThreadRole(ThreadRole::Interactive);
			return std::pair{applied, observedScheduling()};
		});
#if defined(__linux__)
		// Unprivileged Linux threads cannot lower their nice value outside
		// RLIMIT_NICE, so a refused request leaves the Worker value.
		CHECK(observed == (applied ? expectedScheduling(ThreadRole::Interactive)
		                           : expectedScheduling(ThreadRole::Worker)));
#else
		CHECK(applied);
		CHECK(observed == expectedScheduling(ThreadRole::Interactive));
#endif
	}
#endif

#if defined(_WIN32) || defined(__APPLE__)
	TEST_CASE("a launched thread takes its own role, not its launcher's")
	{
		EnvironmentOverride enabled(nullptr);
		// Linux children inherit a raised nice value that unprivileged threads
		// cannot lower again, so only platforms that reset freely check this.
		const int child = onLaunched(ThreadRole::Background,
		                             [] { return onLaunched(ThreadRole::Worker, observedScheduling); });
		CHECK(child == expectedScheduling(ThreadRole::Worker));
	}
#endif

	TEST_CASE("GLOB2_THREAD_PRIORITY=0 leaves scheduling untouched")
	{
		EnvironmentOverride off("0");
		const auto [applied, before, after] = onLaunched(ThreadRole::Worker, [] {
			const int before = observedScheduling();
			const bool applied = GAGCore::ThreadSupport::setCurrentThreadRole(ThreadRole::Background);
			return std::tuple{applied, before, observedScheduling()};
		});
		CHECK_FALSE(applied);
		CHECK(after == before);
	}
}
