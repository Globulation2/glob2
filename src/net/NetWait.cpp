// SPDX-License-Identifier: GPL-3.0-or-later
#include "NetWait.h"

#include <algorithm>
#include <chrono>
#include <thread>

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#define GLOB2_NET_WAIT_POLL 1
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#endif

#ifdef GLOB2_NET_WAIT_POLL
struct NetWaker::Impl
{
	int fds[2] = {-1, -1};
};

NetWaker::NetWaker() : impl(std::make_unique<Impl>())
{
	if (pipe(impl->fds) != 0)
	{
		impl->fds[0] = impl->fds[1] = -1;
		return;
	}
	for (int fd : impl->fds)
	{
		fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
		fcntl(fd, F_SETFD, FD_CLOEXEC);
	}
}

NetWaker::~NetWaker()
{
	for (int fd : impl->fds)
		if (fd >= 0)
			close(fd);
}

void NetWaker::wake()
{
	if (impl->fds[1] < 0)
		return;
	const char byte = 1;
	// A full pipe already holds a wake-up.
	[[maybe_unused]] const auto written = write(impl->fds[1], &byte, 1);
}

void NetWaker::drain()
{
	if (impl->fds[0] < 0)
		return;
	char bytes[64];
	while (read(impl->fds[0], bytes, sizeof(bytes)) > 0)
	{
	}
}

std::intptr_t NetWaker::handle() const
{
	return impl->fds[0];
}

void netWait(const std::vector<NetWaitHandle> &handles, NetWaker *waker, std::uint64_t timeoutMicros,
			 bool supported, std::uint64_t fallbackMicros)
{
	if (!supported)
		timeoutMicros = std::min(timeoutMicros, fallbackMicros);
	std::vector<pollfd> fds;
	fds.reserve(handles.size() + 1);
	if (waker && waker->handle() >= 0)
		fds.push_back({static_cast<int>(waker->handle()), POLLIN, 0});
	for (const auto &handle : handles)
	{
		if (handle.socket < 0 || (!handle.read && !handle.write))
			continue;
		short events = 0;
		if (handle.read)
			events |= POLLIN;
		if (handle.write)
			events |= POLLOUT;
		fds.push_back({static_cast<int>(handle.socket), events, 0});
	}
	// Round up, so a deadline is never polled a moment early and missed.
	const std::uint64_t millis = std::min<std::uint64_t>((timeoutMicros + 999) / 1000, 60000);
	if (poll(fds.data(), static_cast<nfds_t>(fds.size()), static_cast<int>(millis)) < 0 && errno != EINTR)
		std::this_thread::sleep_for(std::chrono::microseconds(std::min<std::uint64_t>(timeoutMicros, 1000)));
	if (waker)
		waker->drain();
}
#else
struct NetWaker::Impl
{
};

NetWaker::NetWaker() : impl(std::make_unique<Impl>()) {}
NetWaker::~NetWaker() = default;
void NetWaker::wake() {}
void NetWaker::drain() {}
std::intptr_t NetWaker::handle() const
{
	return -1;
}

void netWait(const std::vector<NetWaitHandle> &, NetWaker *, std::uint64_t timeoutMicros, bool,
			 std::uint64_t fallbackMicros)
{
	// No readiness wait here: keep the caller's polling pace.
	std::this_thread::sleep_for(std::chrono::microseconds(std::min(timeoutMicros, fallbackMicros)));
}
#endif
