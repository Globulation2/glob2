// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <memory>
#include <string>
#include <cstddef>
struct ServerStatus
{
	bool ready = false, draining = false;
	size_t connections = 0, games = 0;
};
// HTTP control plane is intentionally private; it never accepts game data.
class ServerControl
{
  public:
	ServerControl(const std::string &address, unsigned short port);
	~ServerControl();
	void update(const ServerStatus &status);
	static void installSignals();
	static bool shutdownRequested();

  private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
// Holds an OS lock for the entire server lifetime, before registry/map loading.
class ServerDataLock
{
  public:
	explicit ServerDataLock(const std::string &directory);
	~ServerDataLock();

  private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
