// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Tools.h"
namespace MCP
{
struct Headers
{
	std::string version, method, name;
	bool duplicated = false;
};
struct Reply
{
	int status = 200;
	Json body;
	std::shared_ptr<Request> pending;
	bool modern = false;
	Json id;
};
class Protocol
{
	const ToolRegistry &registry;
	ExecutionContext &context;

  public:
	Protocol(const ToolRegistry &r, ExecutionContext &c) : registry(r), context(c) {}
	Reply receive(const std::string &body, const Headers &headers);
	static Json response(Json id, Json result, bool modern);
};
std::string encodeBase64(const void *, std::size_t);
std::string randomToken();
} // namespace MCP
