// SPDX-License-Identifier: GPL-3.0-or-later
// Browser HttpFetch: emscripten_fetch on the main thread. Its callbacks run
// between frames, so state() only reports what they recorded.
#include "HttpFetch.h"
#include <emscripten/fetch.h>
#include <chrono>
#include <cstring>
#include <stdexcept>

namespace HttpFetch
{
namespace
{
class BrowserFetch final : public Fetch
{
	emscripten_fetch_t *fetch = nullptr;
	State status = State::Pending;
	Response result;
	std::string failure, body;
	std::size_t limit = 0;
	std::chrono::steady_clock::time_point expires;
	// emscripten_fetch reads these until the request completes.
	std::vector<std::string> headerText;
	std::vector<const char *> headerPointers;

	void record(emscripten_fetch_t *done, bool success)
	{
		if (status != State::Pending)
			return;
		if (success || (done->status > 0 && done->status < 600 && done->status != (unsigned short)-1))
		{
			if (static_cast<std::size_t>(done->numBytes) > limit)
			{
				status = State::Failed;
				failure = "HTTP response exceeds the size limit";
			}
			else
			{
				status = State::Done;
				result.status = done->status;
				if (done->data && done->numBytes)
					result.body.assign(done->data, static_cast<std::size_t>(done->numBytes));
				const auto length = emscripten_fetch_get_response_headers_length(done);
				std::string raw(length + 1, '\0');
				if (length)
					emscripten_fetch_get_response_headers(done, raw.data(), raw.size());
				raw.resize(length);
				size_t start = 0;
				while (start < raw.size())
				{
					auto end = raw.find('\n', start);
					if (end == std::string::npos)
						end = raw.size();
					auto line = raw.substr(start, end - start);
					if (!line.empty() && line.back() == '\r')
						line.pop_back();
					const auto colon = line.find(':');
					if (colon != std::string::npos)
					{
						auto value = line.substr(colon + 1);
						value.erase(0, value.find_first_not_of(' '));
						result.headers.emplace_back(line.substr(0, colon), value);
					}
					start = end + 1;
				}
			}
		}
		else
		{
			status = std::chrono::steady_clock::now() >= expires ? State::TimedOut : State::Failed;
			failure = status == State::TimedOut ? "HTTP request timed out"
												: "HTTP request failed; check the address, CORS and network";
		}
		fetch = nullptr;
		emscripten_fetch_close(done);
	}
	static void succeeded(emscripten_fetch_t *done)
	{
		static_cast<BrowserFetch *>(done->userData)->record(done, true);
	}
	static void failed(emscripten_fetch_t *done)
	{
		static_cast<BrowserFetch *>(done->userData)->record(done, false);
	}

  public:
	explicit BrowserFetch(Request spec)
	{
		try
		{
			parseUrl(spec.url);
			for (const auto &field : spec.headers)
				if (field.first.empty() || field.first.find_first_of(":\r\n") != std::string::npos ||
					field.second.find_first_of("\r\n") != std::string::npos)
					throw std::invalid_argument("Invalid request header");
		}
		catch (const std::exception &error)
		{
			status = State::Failed;
			failure = error.what();
			return;
		}
		limit = spec.responseLimit;
		body = std::move(spec.body);
		for (const auto &field : spec.headers)
		{
			headerText.push_back(field.first);
			headerText.push_back(field.second);
		}
		for (const auto &text : headerText)
			headerPointers.push_back(text.c_str());
		headerPointers.push_back(nullptr);
		emscripten_fetch_attr_t attributes;
		emscripten_fetch_attr_init(&attributes);
		std::strncpy(attributes.requestMethod, methodName(spec.method),
					 sizeof(attributes.requestMethod) - 1);
		attributes.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
		attributes.userData = this;
		attributes.onsuccess = succeeded;
		attributes.onerror = failed;
		attributes.timeoutMSecs = static_cast<unsigned long>(spec.timeout.count());
		attributes.requestHeaders = headerPointers.data();
		if (!body.empty())
		{
			attributes.requestData = body.data();
			attributes.requestDataSize = body.size();
		}
		expires = std::chrono::steady_clock::now() + spec.timeout;
		auto *started = emscripten_fetch(&attributes, spec.url.c_str());
		// A synchronous failure has already been recorded and closed.
		if (status == State::Pending)
			fetch = started;
		if (!started && status == State::Pending)
		{
			status = State::Failed;
			failure = "HTTP request could not be started";
		}
	}
	~BrowserFetch() override
	{
		cancel();
	}
	State state() override
	{
		if (status == State::Pending && std::chrono::steady_clock::now() >= expires + std::chrono::seconds(1))
		{
			// The XHR timeout normally fires first; this bounds a stuck request.
			cancel();
			status = State::TimedOut;
			failure = "HTTP request timed out";
		}
		return status;
	}
	const Response &response() const override
	{
		return result;
	}
	std::string error() const override
	{
		return failure;
	}
	void cancel() override
	{
		if (status == State::Pending)
			status = State::Cancelled;
		if (fetch)
		{
			// Closing an in-flight fetch invokes onerror synchronously;
			// record() ignores it because the request is no longer pending.
			auto *open = fetch;
			fetch = nullptr;
			emscripten_fetch_close(open);
		}
	}
};
} // namespace

std::unique_ptr<Fetch> start(Request request)
{
	return std::make_unique<BrowserFetch>(std::move(request));
}
} // namespace HttpFetch
