// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <nlohmann/json.hpp>
#include <SDL3/SDL.h>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <string>

namespace MCP
{
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
struct Result
{
	Json data = Json::object();
	std::string error;
	std::shared_ptr<SDL_Surface> pixels;
};
struct Request
{
	std::string tool;
	Json arguments;
	std::string client;
	Clock::time_point deadline = Clock::now() + std::chrono::seconds(10);
	std::atomic<bool> cancelled{false};
	std::atomic<bool> inputStarted{false};
	std::promise<Result> completion;
	std::function<Result()> execute;
};
class ExecutionContext
{
  public:
	virtual ~ExecutionContext() = default;
	virtual std::shared_ptr<Request> submit(const std::string &, Json, std::string) = 0;
	virtual std::shared_ptr<Request> enqueue(std::function<Result()>, std::string)
	{
		throw std::logic_error("This execution context cannot queue extension work");
	}
};
struct Tool
{
	std::string name, description;
	Json inputSchema, outputSchema, annotations;
	std::function<std::shared_ptr<Request>(ExecutionContext &, Json, std::string)> handler;
	std::function<void(const Json &)> validate;
};
// Immutable after startup; no HTTP or game implementation dependencies.
class ToolRegistry
{
	std::map<std::string, Tool> tools;

  public:
	void add(Tool);
	Json list() const;
	std::shared_ptr<Request> call(ExecutionContext &, const std::string &, Json, std::string) const;
};
// Main-thread startup only: snapshots SDL names for immutable worker validation.
void registerUITools(ToolRegistry &);
Json toolResult(Result, const std::string &pngBase64 = {});
} // namespace MCP
