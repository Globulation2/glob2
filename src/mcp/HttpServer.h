// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Protocol.h"
namespace MCP
{
class HttpServer
{
	struct Impl;
	std::unique_ptr<Impl> impl;

  public:
	HttpServer(Protocol &, int port);
	~HttpServer();
	// Actual listener port; also supports ephemeral port 0 in isolated transport tests.
	int boundPort() const;
	// Retire network operations on their owning thread; polling avoids a UI-thread join.
	void stop();
	bool stopped() const;
	HttpServer(const HttpServer &) = delete;
};
bool allowedHost(const std::string &, int port);
bool allowedOrigin(const std::string &, int port);
} // namespace MCP
