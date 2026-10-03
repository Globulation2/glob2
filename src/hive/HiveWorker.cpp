// SPDX-License-Identifier: GPL-3.0-or-later
#include "HiveWorker.h"
#include "script/ScriptRuntime.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#ifdef __linux__
#include <sys/prctl.h>
#include <sys/resource.h>
#include <linux/seccomp.h>
#include <linux/filter.h>
#include <linux/audit.h>
#include <sys/syscall.h>
#include <cstddef>
#include <cerrno>
#endif
namespace Hive
{
Json json(const Script::Value &v)
{
	switch (v.kind)
	{
	case Script::Value::Null:
		return nullptr;
	case Script::Value::Boolean:
		return v.number != 0;
	case Script::Value::Number:
		return v.number;
	case Script::Value::String:
		return v.text;
	case Script::Value::Array:
	{
		Json a = Json::array();
		for (auto &x : v.items)
			a.push_back(json(x));
		return a;
	}
	case Script::Value::Object:
	{
		Json o = Json::object();
		for (auto &[k, x] : v.fields)
			o[k] = json(x);
		return o;
	}
	}
	throw std::runtime_error("Invalid script data");
}
Script::Value value(const Json &j, unsigned depth)
{
	if (depth > 64)
		throw std::runtime_error("Data is too deeply nested");
	if (j.is_null())
		return {};
	if (j.is_boolean())
		return Script::Value(j.get<bool>());
	if (j.is_number())
	{
		double n = j.get<double>();
		if (!std::isfinite(n))
			throw std::runtime_error("Invalid number");
		return Script::Value(n);
	}
	if (j.is_string())
		return Script::Value(j.get<std::string>());
	auto v = j.is_array() ? Script::Value::array() : Script::Value::object();
	if (j.is_array())
		for (auto &x : j)
			v.items.push_back(value(x, depth + 1));
	else
		for (auto it = j.begin(); it != j.end(); ++it)
			v.set(it.key(), value(it.value(), depth + 1));
	return v;
}
namespace
{
int integer(const Json &j, int minimum, int maximum)
{
	if (!j.is_number())
		throw std::runtime_error("Expected integer");
	double v = j.get<double>();
	if (!std::isfinite(v) || std::floor(v) != v || v < minimum || v > maximum)
		throw std::runtime_error("Integer outside range");
	return int(v);
}
Json query(const Json &s, const std::string &name, const Json &args)
{
	if (name == "teams" || name == "buildingTypes")
		return s.at(name);
	if (name == "units" || name == "buildings")
	{
		Json f = args.empty() ? Json::object() : args.at(0);
		if (!f.is_object())
			throw std::runtime_error("Expected query options");
		int team = f.contains("team") && !f["team"].is_null()
					   ? integer(f["team"], 0, int(s.at("teams").size()) - 1)
					   : -1;
		int offset =
			f.contains("offset") && !f["offset"].is_null() ? integer(f["offset"], 0, 32768) : 0;
		int limit =
			f.contains("limit") && !f["limit"].is_null() ? integer(f["limit"], 0, 32768) : 32768;
		Json out = Json::array();
		for (auto &v : s.at(name))
			if (team < 0 || v.at("team") == team)
			{
				if (offset)
				{
					--offset;
					continue;
				}
				if (int(out.size()) >= limit)
					break;
				out.push_back(v);
			}
		return out;
	}
	if (name == "unit" || name == "building")
	{
		if (args.empty() || !args[0].is_object())
			throw std::runtime_error("Expected entity reference");
		int id = integer(args[0].at("id"), 0, 65535);
		if (!args[0].contains("generation") || !args[0]["generation"].is_number())
			return nullptr;
		for (auto &v : s.at(name == "unit" ? "units" : "buildings"))
			if (v.at("id") == id && v.at("generation") == args[0]["generation"])
				return v;
		return nullptr;
	}
	if (name == "tile" || name == "region")
	{
		int x = integer(args.at(0), -32768, 32767), y = integer(args.at(1), -32768, 32767);
		int width = s.at("width"), height = s.at("height");
		auto tile = [&](int tx, int ty) -> Json
		{
			tx = (tx % width + width) % width;
			ty = (ty % height + height) % height;
			return s.at("tiles").at(ty * width + tx);
		};
		if (name == "tile")
			return tile(x, y);
		int w = integer(args.at(2), 0, 256), h = integer(args.at(3), 0, 256);
		Json out = Json::array();
		for (int yy = 0; yy < h; yy++)
			for (int xx = 0; xx < w; xx++)
				out.push_back(tile(x + xx, y + yy));
		return out;
	}
	throw std::runtime_error("Unavailable colony query");
}
} // namespace
Json invoke(const Json &request)
{
	try
	{
		const auto source = request.at("source").get<std::string>();
		auto runtime = Script::makeRuntime();
		runtime->validate(source);
		if (request.value("validate", false))
			return {{"ok", true}};
		const auto &snapshot = request.at("snapshot");
		Script::Host host;
		host.commander = true;
		host.team = snapshot.at("team");
		host.tick = snapshot.at("tick");
		host.width = snapshot.at("width");
		host.height = snapshot.at("height");
		if (host.team < 0 || host.team >= 32 || !host.width || !host.height)
			throw std::runtime_error("Invalid colony context");
		Json wakes = Json::array();
		std::uint32_t rng = request.value("random", 1u);
		host.random = [&]
		{
			rng = rng * 1664525u + 1013904223u;
			return rng;
		};
		host.query = [&](const std::string &name, const std::vector<Script::Value> &args,
						 const Script::QueryBudget &budget)
		{
			Json a = Json::array();
			for (auto &v : args)
				a.push_back(json(v));
			if (name == "wakeAgent")
			{
				if (a.size() != 1 || !a[0].is_object() || wakes.size() >= 4)
					throw std::runtime_error("Invalid commander wake request");
				const auto &w = a[0];
				const auto key = w.at("key").get<std::string>(),
						   reason = w.at("reason").get<std::string>();
				if (key.empty() || key.size() > 80 || reason.empty() || reason.size() > 512 ||
					w.dump().size() > 4096)
					throw std::runtime_error("Wake request too large");
				wakes.push_back(w);
				return Script::Value();
			}
			Json output = query(snapshot, name, a);
			const auto size = output.dump().size();
			budget(size / 8 + 1, size * 4);
			return value(output);
		};
		Json initialState = request.value("state", Json(nullptr));
		bool initialized = request.value("initialized", false);
		if (request.contains("migration"))
		{
			auto migrated = invoke({{"source", request.at("migration")},
									{"snapshot", snapshot},
									{"state", {{"previous", request.at("previousState")}}},
									{"initialized", false}});
			if (!migrated.value("ok", false) || !migrated.value("orders", Json::array()).empty() ||
				!migrated.value("wakes", Json::array()).empty())
				throw std::runtime_error("Migration must only return a checkpoint");
			initialState = migrated.at("output");
			initialized = !initialState.is_null();
		}
		auto result = runtime->invoke(source, value(initialState), !initialized, host);
		Json effects = json(result.effects);
		if (!effects.is_object())
			throw std::runtime_error("Return an object containing output and orders");
		auto orders = effects.value("orders", Json::array());
		if (!orders.is_array() || orders.size() > 32 || effects.dump().size() > 65536)
			throw std::runtime_error("Too many orders or too much output");
		return {{"ok", true},
				{"state", json(result.state)},
				{"installedState", initialState},
				{"installedInitialized", initialized},
				{"output", effects.value("output", Json(nullptr))},
				{"orders", orders},
				{"wakes", wakes},
				{"random", rng}};
	}
	catch (const std::exception &e)
	{
		return {{"ok", false}, {"diagnostic", std::string(e.what()).substr(0, 4096)}};
	}
}
int workerMain()
{
#ifdef __linux__
	// The dynamic loader has finished. Admit only anonymous memory, pipe I/O,
	// clocks, signals and process exit. No path lookup, sockets, process creation,
	// ptrace or descriptor acquisition is permitted, even after an engine exploit.
#if defined(__x86_64__)
	constexpr unsigned architecture = AUDIT_ARCH_X86_64;
#elif defined(__aarch64__)
	constexpr unsigned architecture = AUDIT_ARCH_AARCH64;
#else
	return 2; // Fail closed until this architecture has a reviewed syscall profile.
#endif
#if defined(__x86_64__) || defined(__aarch64__)
#define HIVE_ALLOW(n)                                                                              \
	BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_##n, 0, 1),                                           \
		BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)
	sock_filter filter[] = {BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, arch)),
							BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, architecture, 1, 0),
							BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
							BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)),
							HIVE_ALLOW(read),
							HIVE_ALLOW(write),
							HIVE_ALLOW(close),
							HIVE_ALLOW(fstat),
							HIVE_ALLOW(brk),
							HIVE_ALLOW(mmap),
							HIVE_ALLOW(munmap),
							HIVE_ALLOW(mremap),
							HIVE_ALLOW(mprotect),
							HIVE_ALLOW(madvise),
							HIVE_ALLOW(futex),
							HIVE_ALLOW(clock_gettime),
							HIVE_ALLOW(gettimeofday),
							HIVE_ALLOW(getrandom),
							HIVE_ALLOW(rt_sigaction),
							HIVE_ALLOW(rt_sigprocmask),
							HIVE_ALLOW(rt_sigreturn),
							HIVE_ALLOW(sigaltstack),
							HIVE_ALLOW(getpid),
							HIVE_ALLOW(gettid),
							HIVE_ALLOW(exit),
							HIVE_ALLOW(exit_group),
							BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM)};
#undef HIVE_ALLOW
	sock_fprog program{static_cast<unsigned short>(sizeof(filter) / sizeof(filter[0])), filter};
#endif
#endif
#ifdef __linux__
	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0)
		return 2;
	rlimit limit{256 * 1024 * 1024, 256 * 1024 * 1024};
	setrlimit(RLIMIT_AS, &limit);
	rlimit files{0, 0};
	setrlimit(RLIMIT_FSIZE, &files);
	rlimit cpu{4, 4};
	setrlimit(RLIMIT_CPU, &cpu);
#if defined(__x86_64__) || defined(__aarch64__)
	if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program) != 0)
		return 2;
#endif
#endif
	std::string line;
	line.reserve(65536);
	// A fresh process per invocation has no engine world, credentials, or script host I/O bindings.
	char c;
	while (std::cin.get(c) && c != '\n')
	{
		if (line.size() >= 64 * 1024 * 1024)
			return 2;
		line += c;
	}
	try
	{
		std::cout << invoke(Json::parse(line)).dump() << '\n';
		return 0;
	}
	catch (...)
	{
		return 2;
	}
}
} // namespace Hive
