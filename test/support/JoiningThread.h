// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <thread>
#include <utility>

namespace glob2test
{
// Scoped test workers do not use stop tokens. Keep automatic joining on toolchains
// whose C++20 standard library has std::thread but not std::jthread (the pinned NDK).
class JoiningThread
{
	std::thread worker;

  public:
	template <class Function, class... Args>
	explicit JoiningThread(Function &&function, Args &&...args)
		: worker(std::forward<Function>(function), std::forward<Args>(args)...)
	{
	}
	JoiningThread(const JoiningThread &) = delete;
	JoiningThread &operator=(const JoiningThread &) = delete;
	~JoiningThread()
	{
		if (worker.joinable()) worker.join();
	}
	void join() { worker.join(); }
};
} // namespace glob2test
