// SPDX-License-Identifier: GPL-3.0-or-later
#include <RecordingProcess.h>
#include <chrono>
#include <atomic>
#include <algorithm>
#include <stdexcept>
#include <thread>
#include <mutex>
#if !defined(__EMSCRIPTEN__) && !defined(GLOB2_MOBILE)
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <spawn.h>
#include <signal.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>
#include <cerrno>
extern char **environ;
#endif
#endif
namespace GAGCore::Recording
{
struct Process::Impl
{
#if !defined(__EMSCRIPTEN__) && !defined(GLOB2_MOBILE)
#ifdef _WIN32
	HANDLE child = nullptr, input = nullptr;
	std::atomic<bool> monitorStopped{false};
	std::atomic<std::int64_t> writingUntil{0};
	std::thread monitor;
#else
	pid_t child = -1;
	int input = -1;
#endif
#endif
};
Process::Process() : impl(std::make_unique<Impl>()) {}
#if !defined(__EMSCRIPTEN__) && !defined(GLOB2_MOBILE)
#ifdef _WIN32
namespace
{
std::wstring wide(const std::string &text)
{
	int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()),
								nullptr, 0);
	if (!n && !text.empty())
		throw std::runtime_error("Invalid UTF-8 encoder argument");
	std::wstring result(n, L' ');
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), result.data(),
						n);
	return result;
}
std::wstring quote(const std::string &text)
{
	std::wstring result = L"\"";
	unsigned slashes = 0;
	for (auto c : wide(text))
	{
		if (c == L'\\')
			++slashes;
		else
		{
			result.append(slashes * (c == L'"' ? 2 : 1), L'\\');
			slashes = 0;
			if (c == L'"')
				result += L'\\';
			result += c;
		}
	}
	result.append(slashes * 2, L'\\');
	return result + L'"';
}
} // namespace
namespace
{
struct LaunchHandles
{
	HANDLE read = nullptr, write = nullptr, log = nullptr;
	LPPROC_THREAD_ATTRIBUTE_LIST attributes = nullptr;
	~LaunchHandles()
	{
		if (attributes)
			DeleteProcThreadAttributeList(attributes);
		for (HANDLE handle : {read, write, log})
			if (handle && handle != INVALID_HANDLE_VALUE)
				CloseHandle(handle);
	}
};
} // namespace
void Process::launch(const std::vector<std::string> &args, const std::string &log, bool input)
{
	if (args.empty() || args.front().empty() || impl->child)
		throw std::runtime_error("Invalid encoder process launch");
	std::wstring command;
	for (const auto &arg : args)
	{
		if (!command.empty())
			command += L' ';
		command += quote(arg);
	}
	const auto logPath = wide(log);
	SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
	// Keep attribute-list backing storage alive until LaunchHandles destroys the list.
	std::vector<unsigned char> storage;
	LaunchHandles handles;
	if (input && !CreatePipe(&handles.read, &handles.write, &security, 0))
		throw std::runtime_error("Cannot create encoder pipe");
	if (handles.write && !SetHandleInformation(handles.write, HANDLE_FLAG_INHERIT, 0))
		throw std::runtime_error("Cannot make encoder pipe private");
	handles.log = CreateFileW(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security,
							  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (!input)
		handles.read = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
								   &security, OPEN_EXISTING, 0, nullptr);
	if (handles.log == INVALID_HANDLE_VALUE || !handles.read ||
		handles.read == INVALID_HANDLE_VALUE)
		throw std::runtime_error("Cannot open encoder log");
	STARTUPINFOEXW startup{};
	startup.StartupInfo.cb = sizeof(startup);
	startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
	startup.StartupInfo.hStdInput = handles.read;
	startup.StartupInfo.hStdOutput = startup.StartupInfo.hStdError = handles.log;
	SIZE_T bytes = 0;
	InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
	if (!bytes)
		throw std::runtime_error("Cannot allocate encoder handle allow-list");
	storage.resize(bytes);
	startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
	if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &bytes))
		throw std::runtime_error("Cannot initialize encoder handle allow-list");
	handles.attributes = startup.lpAttributeList;
	HANDLE inherited[] = {handles.read, handles.log};
	if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
								   inherited, sizeof(inherited), nullptr, nullptr))
		throw std::runtime_error("Cannot restrict encoder inherited handles");
	PROCESS_INFORMATION process{};
	if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
						EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr, nullptr,
						&startup.StartupInfo, &process))
		throw std::runtime_error("Cannot launch FFmpeg; install it or set --record-ffmpeg");
	CloseHandle(process.hThread);
	impl->child = process.hProcess;
	impl->input = handles.write;
	handles.write = nullptr;
	impl->monitorStopped = false;
	impl->monitor = std::thread(
		[this]
		{
			while (!impl->monitorStopped)
			{
				const auto deadline = impl->writingUntil.load();
				const auto time = std::chrono::duration_cast<std::chrono::milliseconds>(
									  std::chrono::steady_clock::now().time_since_epoch())
									  .count();
				if (deadline && time > deadline)
				{
					TerminateProcess(impl->child, 1);
					impl->writingUntil = 0;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
		});
}

void Process::write(const void *data, std::size_t bytes)
{
	impl->writingUntil = std::chrono::duration_cast<std::chrono::milliseconds>(
							 std::chrono::steady_clock::now().time_since_epoch())
							 .count() +
						 10000;
	bool ok = true;
	auto p = static_cast<const unsigned char *>(data);
	while (bytes)
	{
		DWORD sent = 0;
		if (!WriteFile(impl->input, p, DWORD(std::min<std::size_t>(bytes, 65536)), &sent,
					   nullptr) ||
			!sent)
		{
			ok = false;
			break;
		}
		p += sent;
		bytes -= sent;
	}
	impl->writingUntil = 0;
	if (!ok)
		throw std::runtime_error("FFmpeg pipe closed or timed out; inspect the recording log");
}
int Process::finish(int timeoutSeconds)
{
	if (impl->input)
	{
		CloseHandle(impl->input);
		impl->input = nullptr;
	}
	if (!impl->child)
		return 0;
	if (WaitForSingleObject(impl->child, DWORD(timeoutSeconds * 1000)) != WAIT_OBJECT_0)
	{
		TerminateProcess(impl->child, 1);
		WaitForSingleObject(impl->child, 1000);
	}
	DWORD code = 1;
	GetExitCodeProcess(impl->child, &code);
	impl->monitorStopped = true;
	if (impl->monitor.joinable())
		impl->monitor.join();
	CloseHandle(impl->child);
	impl->child = nullptr;
	return int(code);
}
#else
void Process::launch(const std::vector<std::string> &args, const std::string &log, bool input)
{
	if (args.empty() || args.front().empty() || impl->child >= 0)
		throw std::runtime_error("Invalid encoder process launch");
	static std::mutex launchMutex;
	std::lock_guard<std::mutex> launchLock(launchMutex);
	int pipes[2] = {-1, -1};
	if (input && pipe(pipes))
		throw std::runtime_error("Cannot create encoder pipe");
	int output = open(log.c_str(), O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0600);
	int read = input ? pipes[0] : open("/dev/null", O_RDONLY | O_CLOEXEC);
	if (output < 0 || read < 0)
	{
		for (int fd : pipes)
			if (fd >= 0)
				close(fd);
		if (!input && read >= 0)
			close(read);
		if (output >= 0)
			close(output);
		throw std::runtime_error("Cannot open encoder log");
	}
	if (pipes[0] >= 0)
		fcntl(pipes[0], F_SETFD, FD_CLOEXEC);
	if (pipes[1] >= 0)
		fcntl(pipes[1], F_SETFD, FD_CLOEXEC);
	posix_spawn_file_actions_t actions;
	posix_spawn_file_actions_init(&actions);
	posix_spawn_file_actions_adddup2(&actions, read, STDIN_FILENO);
	posix_spawn_file_actions_adddup2(&actions, output, STDOUT_FILENO);
	posix_spawn_file_actions_adddup2(&actions, output, STDERR_FILENO);
	posix_spawn_file_actions_addclose(&actions, read);
	posix_spawn_file_actions_addclose(&actions, output);
	if (pipes[1] >= 0)
		posix_spawn_file_actions_addclose(&actions, pipes[1]);
	std::vector<char *> argv;
	for (const auto &arg : args)
		argv.push_back(const_cast<char *>(arg.c_str()));
	argv.push_back(nullptr);
	int error = posix_spawnp(&impl->child, argv[0], &actions, nullptr, argv.data(), environ);
	posix_spawn_file_actions_destroy(&actions);
	close(read);
	close(output);
	if (error)
	{
		impl->child = -1;
		if (pipes[1] >= 0)
			close(pipes[1]);
		throw std::runtime_error("Cannot launch FFmpeg; install it or set --record-ffmpeg");
	}
	impl->input = pipes[1];
	if (input)
		fcntl(impl->input, F_SETFL, fcntl(impl->input, F_GETFL) | O_NONBLOCK);
}
void Process::write(const void *data, std::size_t bytes)
{
	// SIGPIPE is blocked only on this worker, never globally in the game.
	sigset_t signals, old;
	sigemptyset(&signals);
	sigaddset(&signals, SIGPIPE);
	pthread_sigmask(SIG_BLOCK, &signals, &old);
	bool ok = true;
	auto p = static_cast<const unsigned char *>(data);
	auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (bytes)
	{
		auto sent = ::write(impl->input, p, bytes);
		if (sent > 0)
		{
			p += sent;
			bytes -= sent;
			continue;
		}
		if (sent < 0 && (errno == EAGAIN || errno == EINTR) &&
			std::chrono::steady_clock::now() < deadline)
		{
			pollfd fd{impl->input, POLLOUT, 0};
			poll(&fd, 1, 100);
			continue;
		}
		ok = false;
		break;
	}
	// Consume this thread's pending SIGPIPE before restoring its original mask.
	if (!sigismember(&old, SIGPIPE))
	{
		sigset_t pending;
		sigpending(&pending);
		if (sigismember(&pending, SIGPIPE))
		{
			int signal;
			sigwait(&signals, &signal);
		}
	}
	pthread_sigmask(SIG_SETMASK, &old, nullptr);
	if (!ok)
		throw std::runtime_error("FFmpeg pipe closed or timed out; inspect the recording log");
}
int Process::finish(int timeoutSeconds)
{
	if (impl->input >= 0)
	{
		close(impl->input);
		impl->input = -1;
	}
	if (impl->child < 0)
		return 0;
	auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);
	int result = 0;
	for (;;)
	{
		pid_t got = waitpid(impl->child, &result, WNOHANG);
		if (got == impl->child)
			break;
		if (got < 0 && errno != EINTR)
		{
			impl->child = -1;
			return 1;
		}
		if (std::chrono::steady_clock::now() >= deadline)
		{
			kill(impl->child, SIGKILL);
			while (waitpid(impl->child, &result, 0) < 0 && errno == EINTR)
			{
			}
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	impl->child = -1;
	return WIFEXITED(result) ? WEXITSTATUS(result) : 1;
}
#endif
#else
void Process::launch(const std::vector<std::string> &, const std::string &, bool)
{
	throw std::runtime_error("Recording requires a desktop build");
}
void Process::write(const void *, std::size_t) {}
int Process::finish(int)
{
	return 1;
}
#endif
Process::~Process()
{
	finish(1);
}
} // namespace GAGCore::Recording
