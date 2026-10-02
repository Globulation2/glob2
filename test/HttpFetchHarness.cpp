// SPDX-License-Identifier: GPL-3.0-or-later
// Drives one HttpFetch request for tests/transport/test_http_fetch.py and
// prints the outcome as JSON. Polls like a UI screen: no blocking waits.
#include "HttpFetch.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <iostream>
#include <thread>

int main(int argc, char **argv)
{
	if (argc < 3)
	{
		std::cerr << "usage: http-fetch-test get|post|put|cancel URL [BODY] [TIMEOUT_MS]\n";
		return 2;
	}
	using Clock = std::chrono::steady_clock;
	const std::string mode = argv[1];
	HttpFetch::Request request;
	request.url = argv[2];
	request.method = mode == "post"	   ? HttpFetch::Method::Post
					 : mode == "put"	   ? HttpFetch::Method::Put
					 : mode == "patch"  ? HttpFetch::Method::Patch
					 : mode == "delete" ? HttpFetch::Method::Delete
										: HttpFetch::Method::Get;
	if (argc > 3)
		request.body = argv[3];
	if (argc > 4)
		request.timeout = std::chrono::milliseconds(std::stoi(argv[4]));
	request.headers = {{"Content-Type", "application/json"}, {"X-Glob2-Test", "header value"}};
	request.responseLimit = 1024 * 1024;

	const auto started = Clock::now();
	auto fetch = HttpFetch::start(request);
	auto state = fetch->state();
	while (state == HttpFetch::State::Pending)
	{
		if (mode == "cancel" && Clock::now() - started > std::chrono::milliseconds(200))
		{
			const auto cancelling = Clock::now();
			fetch->cancel();
			if (Clock::now() - cancelling > std::chrono::milliseconds(200))
			{
				std::cerr << "cancel blocked\n";
				return 1;
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		state = fetch->state();
	}
	static const char *names[] = {"pending", "done", "failed", "timed-out", "cancelled"};
	nlohmann::json out = {
		{"state", names[static_cast<int>(state)]},
		{"elapsed_ms",
		 std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count()},
		{"error", fetch->error()}};
	if (state == HttpFetch::State::Done)
	{
		const auto &response = fetch->response();
		out["status"] = response.status;
		out["body"] = response.body;
		out["echo_header"] = response.header("x-echo-method");
		out["headers"] = nlohmann::json::object();
		for (const auto &field : response.headers)
			out["headers"][field.first] = field.second;
	}
	std::cout << out.dump() << std::endl;
	return 0;
}
