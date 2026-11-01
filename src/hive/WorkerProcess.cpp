// SPDX-License-Identifier: GPL-3.0-or-later
#include "WorkerProcess.h"
#include <nlohmann/json.hpp>
#include <SDL3/SDL.h>
#include <filesystem>
#include <chrono>
#include <thread>
#include <atomic>
namespace Hive
{
namespace
{
std::string executable;
}
void setExecutable(const char *path)
{
	std::filesystem::path candidate(path);
#ifndef __EMSCRIPTEN__
	if (!candidate.has_parent_path())
		if (const char *base = SDL_GetBasePath())
			candidate = std::filesystem::path(base) / candidate;
#endif
	executable = std::filesystem::absolute(candidate).string();
}
#ifndef __EMSCRIPTEN__
Json runWorker(const Json &input)
{
	const char *args[] = {executable.c_str(), "dev", "hive-worker", nullptr};
	SDL_PropertiesID props = SDL_CreateProperties();
	auto *environment = SDL_CreateEnvironment(false);
	SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, const_cast<char **>(args));
	SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, environment);
	SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_APP);
	SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
	SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
	auto *process = SDL_CreateProcessWithProperties(props);
	SDL_DestroyProperties(props);
	SDL_DestroyEnvironment(environment);
	if (!process)
		return {{"ok", false},
				{"diagnostic", std::string("Cannot start Hive worker: ") + SDL_GetError()}};
	std::string wire = input.dump() + "\n", output;
	auto *stdinStream = SDL_GetProcessInput(process);
	auto *stdoutStream = SDL_GetProcessOutput(process);
	// SDL process input is nonblocking; pump both pipes so large observations cannot deadlock.
	std::size_t sent = 0;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	bool complete = false;
	int exitCode = 0;
	while (std::chrono::steady_clock::now() < deadline)
	{
		if (sent < wire.size())
			sent += SDL_WriteIO(stdinStream, wire.data() + sent, wire.size() - sent);
		char bytes[8192];
		auto n = SDL_ReadIO(stdoutStream, bytes, sizeof(bytes));
		if (n)
			output.append(bytes, n);
		if (output.size() > 2 * 1024 * 1024)
			break;
		if (output.find('\n') != std::string::npos)
		{
			complete = true;
			break;
		}
		// A finished worker can still have several chunks buffered in stdout.
		// Drain them before treating exit as an incomplete result.
		if (SDL_WaitProcess(process, false, &exitCode) &&
			SDL_GetIOStatus(stdoutStream) == SDL_IO_STATUS_EOF)
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	SDL_KillProcess(process, true);
	SDL_WaitProcess(process, true, &exitCode);
	SDL_DestroyProcess(process);
	if (!complete)
		return {{"ok", false},
				{"diagnostic", "Hive worker timed out, exited, or exceeded output capacity"}};
	try
	{
		auto result = Json::parse(output);
		if (!result.is_object())
			return {{"ok", false}, {"diagnostic", "Hive worker response must be a JSON object"}};
		return result;
	}
	catch (const std::exception &)
	{
		return {{"ok", false}, {"diagnostic", "Hive worker returned invalid JSON"}};
	}
}
#else
Json runWorker(const Json &)
{
	return {{"ok", false}, {"error", "Native worker unavailable"}};
}
#endif
std::future<Json> startWorker(Json request)
{
	static std::atomic<bool> busy{false};
	if (busy.exchange(true))
	{
		std::promise<Json> rejected;
		rejected.set_value({{"ok", false}, {"error", "Previous local worker is still retiring"}});
		return rejected.get_future();
	}
	std::packaged_task<Json()> task(
		[request = std::move(request)]
		{
			struct Release
			{
				~Release() { busy = false; }
			} release;
			return runWorker(request);
		});
	auto result = task.get_future();
	try
	{
		std::thread(std::move(task)).detach();
	}
	catch (...)
	{
		busy = false;
		throw;
	}
	return result;
}
} // namespace Hive
