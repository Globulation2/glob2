// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Waiting for network work instead of polling on a timer: a loop that services
// transports collects their wait handles (NetTransport::waitHandles), then
// blocks until one of the sockets is ready, a NetWaker is woken from another
// thread, or the timeout passes. Readiness waits work on POSIX systems; where
// they do not (Windows' completion ports, the browser), netWait() sleeps for at
// most `fallbackMicros` and the caller polls as before.

#include <cstdint>
#include <memory>
#include <vector>

#include "NetTransport.h"

/// Wakes a thread blocked in netWait() from any other thread.
class NetWaker
{
  public:
	NetWaker();
	~NetWaker();
	NetWaker(const NetWaker &) = delete;
	NetWaker &operator=(const NetWaker &) = delete;
	/// Makes the next (or current) netWait() with this waker return at once.
	void wake();
	/// Clears pending wake-ups (netWait() does this after it returns).
	void drain();
	/// The socket netWait() watches, or -1 when wake-ups are unsupported here.
	std::intptr_t handle() const;

  private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};

/// Blocks until a handle is ready, the waker is woken, or `timeoutMicros` pass.
/// With `supported` false (some transport cannot report handles), or where
/// readiness waits are unavailable, it waits at most `fallbackMicros` instead.
void netWait(const std::vector<NetWaitHandle> &handles, NetWaker *waker, std::uint64_t timeoutMicros,
			 bool supported = true, std::uint64_t fallbackMicros = 1000);
