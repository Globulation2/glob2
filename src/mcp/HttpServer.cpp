// SPDX-License-Identifier: GPL-3.0-or-later
#include "HttpServer.h"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <SDL3_image/SDL_image.h>
#include <thread>
#include <set>
#include <sstream>
#include <locale>
namespace MCP
{
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using Tcp = asio::ip::tcp;
namespace
{
std::string_view trim(std::string_view text)
{
	const auto first = text.find_first_not_of(" \t");
	if (first == std::string_view::npos)
		return {};
	return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}
std::string mediaType(std::string_view range)
{
	auto type = trim(range.substr(0, range.find(';')));
	std::string normalized(type);
	for (auto &c : normalized)
		if (c >= 'A' && c <= 'Z')
			c += 'a' - 'A';
	return normalized;
}
bool accepts(std::string_view header, std::string_view expected)
{
	while (!header.empty())
	{
		const auto comma = header.find(',');
		auto range = trim(header.substr(0, comma));
		header = comma == std::string_view::npos ? std::string_view{} : header.substr(comma + 1);
		if (mediaType(range) != expected)
			continue;
		double quality = 1;
		auto semicolon = range.find(';');
		while (semicolon != std::string_view::npos)
		{
			range.remove_prefix(semicolon + 1);
			semicolon = range.find(';');
			const auto parameter = trim(range.substr(0, semicolon));
			const auto equal = parameter.find('=');
			if (equal == std::string_view::npos || mediaType(parameter.substr(0, equal)) != "q")
				continue;
			const auto value = trim(parameter.substr(equal + 1));
			// Floating-point from_chars is unavailable on older Apple deployment
			// targets. Keep HTTP numbers independent of the user's numeric locale.
			std::istringstream parser{std::string(value)};
			parser.imbue(std::locale::classic());
			parser >> std::noskipws >> quality;
			if (!parser || !parser.eof() || value.empty() || value.front() == '+' ||
				!(quality >= 0 && quality <= 1))
				quality = 0;
		}
		if (quality > 0)
			return true;
	}
	return false;
}
} // namespace

bool allowedHost(const std::string &host, int port)
{
	return host == "127.0.0.1:" + std::to_string(port) ||
		   host == "localhost:" + std::to_string(port);
}
bool allowedOrigin(const std::string &origin, int port)
{
	return origin.empty() || origin == "http://127.0.0.1:" + std::to_string(port) ||
		   origin == "http://localhost:" + std::to_string(port);
}
struct HttpServer::Impl
{
	asio::io_context io;
	Tcp::acceptor acceptor;
	Protocol &protocol;
	int port;
	std::thread worker;
	std::atomic<bool> stopping{false};
	std::atomic<bool> completed{false};
	struct Session;
	std::set<std::shared_ptr<Session>> sessions;
	Impl(Protocol &p, int n) : acceptor(io), protocol(p), port(n)
	{
		const Tcp::endpoint endpoint(asio::ip::make_address_v4("127.0.0.1"), n);
		acceptor.open(Tcp::v4());
#ifdef _WIN32
		BOOL exclusive = TRUE;
		if (setsockopt(acceptor.native_handle(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
					   reinterpret_cast<const char *>(&exclusive), sizeof(exclusive)) != 0)
			throw boost::system::system_error(
				boost::system::error_code(WSAGetLastError(), asio::error::get_system_category()));
#else
		acceptor.set_option(asio::socket_base::reuse_address(true));
#endif
		acceptor.bind(endpoint);
		port = acceptor.local_endpoint().port();
		acceptor.listen(16);
		accept();
		worker = std::thread(
			[this]
			{
				io.run();
				completed = true;
			});
	}
	~Impl();
	void accept();
	void stop();
};
struct HttpServer::Impl::Session : std::enable_shared_from_this<Session>
{
	Impl &owner;
	beast::tcp_stream stream;
	beast::flat_buffer buffer;
	http::request_parser<http::string_body> parser;
	http::response<http::string_body> response;
	asio::steady_timer timer;
	Reply reply;
	std::future<Result> future;
	char disconnectByte;
	bool closed = false;
	Session(Impl &o, Tcp::socket socket) : owner(o), stream(std::move(socket)), timer(o.io)
	{
		parser.body_limit(1024 * 1024);
	}
	void close()
	{
		if (closed)
			return;
		closed = true;
		if (reply.pending)
			reply.pending->cancelled = true;
		boost::system::error_code ec;
		timer.cancel();
		stream.socket().cancel(ec);
		stream.socket().close(ec);
		owner.sessions.erase(shared_from_this());
	}
	void write(int status, Json body = nullptr)
	{
		if (closed)
			return;
		response.version(11);
		response.result(status);
		response.keep_alive(false);
		response.set(http::field::content_type, "application/json");
		if (status == 405)
			response.set(http::field::allow, "POST");
		if (!body.is_null())
			response.body() = body.dump(-1, ' ', false, Json::error_handler_t::replace);
		response.prepare_payload();
		stream.expires_after(std::chrono::seconds(10));
		http::async_write(stream, response,
						  [self = shared_from_this()](auto, auto) { self->close(); });
	}
	void start()
	{
		stream.expires_after(std::chrono::seconds(10));
		http::async_read(stream, buffer, parser,
						 [self = shared_from_this()](auto ec, auto)
						 {
							 if (ec)
							 {
								 if (ec == http::error::body_limit)
									 self->write(413);
								 else
									 self->close();
								 return;
							 }
							 self->receive();
						 });
	}
	void receive()
	{
		const auto &r = parser.get();
		auto header = [&](beast::string_view name) { return std::string(r[name]); };
		// Distinguish an absent Origin from a present, empty (invalid) Origin.
		if (r.count(http::field::host) != 1 || r.count(http::field::origin) > 1 ||
			!allowedHost(header("Host"), owner.port) ||
			(r.count(http::field::origin) &&
			 (header("Origin").empty() || !allowedOrigin(header("Origin"), owner.port))))
		{
			write(403);
			return;
		}
		const bool duplicatedMetadata = r.count("MCP-Protocol-Version") > 1 ||
										r.count("Mcp-Method") > 1 || r.count("Mcp-Name") > 1;
		if (r.target() != "/mcp")
		{
			write(404);
			return;
		}
		if (r.method() != http::verb::post)
		{
			write(405);
			return;
		}
		const auto content = header("Content-Type");
		std::string accept;
		for (const auto &field : r)
		{
			if (field.name() == http::field::accept)
			{
				if (!accept.empty())
					accept += ',';
				accept.append(field.value().data(), field.value().size());
			}
		}
		if (r.count(http::field::content_type) != 1 || mediaType(content) != "application/json")
		{
			write(415);
			return;
		}
		if (!accepts(accept, "application/json") || !accepts(accept, "text/event-stream"))
		{
			write(406);
			return;
		}
		try
		{
			reply = owner.protocol.receive(r.body(),
										   {header("MCP-Protocol-Version"), header("Mcp-Method"),
											header("Mcp-Name"), duplicatedMetadata});
		}
		catch (const std::exception &)
		{
			write(400, {{"jsonrpc", "2.0"},
						{"error", {{"code", -32602}, {"message", "Invalid parameter types"}}}});
			return;
		}
		if (!reply.pending)
		{
			write(reply.status, reply.body);
			return;
		}
		future = reply.pending->completion.get_future();
		// A second request on this connection is unsupported. EOF cancels outstanding work.
		stream.expires_never();
		stream.socket().async_read_some(asio::buffer(&disconnectByte, 1),
										[self = shared_from_this()](auto, auto) { self->close(); });
		poll();
	}
	void poll()
	{
		try
		{
			pollReady();
		}
		catch (const std::exception &e)
		{
			Result result;
			result.error = std::string("MCP completion failed: ") + e.what();
			if (reply.pending)
				reply.pending->cancelled = true;
			write(200, Protocol::response(reply.id, toolResult(std::move(result)), reply.modern));
		}
	}
	void pollReady()
	{
		if (closed)
			return;
		if (future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
		{
			auto result = future.get();
			std::string png;
			if (result.pixels)
			{
				std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)> output(SDL_IOFromDynamicMem(),
																			 SDL_CloseIO);
				if (!output)
					result.error = SDL_GetError();
				else
				{
					if (!IMG_SavePNG_IO(result.pixels.get(), output.get(), false))
						result.error = SDL_GetError();
					else
					{
						const auto size = SDL_GetIOSize(output.get());
						const auto bytes = SDL_GetPointerProperty(
							SDL_GetIOProperties(output.get()),
							SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr);
						if (size > 0 && bytes)
							png = encodeBase64(bytes, std::size_t(size));
						else
							result.error = "PNG encoding produced no data";
					}
				}
			}
			// UI failures also set Request::cancelled to retire buffered events.
			// They still need their completed tool result published. Transport
			// disconnect is represented by closed, and server retirement by stopping.
			if (owner.stopping)
			{
				close();
				return;
			}
			if (Clock::now() >= reply.pending->deadline)
			{
				reply.pending->cancelled = true;
				result.pixels.reset();
				result.error = "UI operation deadline exceeded during PNG encoding";
				png.clear();
			}
			write(200,
				  Protocol::response(reply.id, toolResult(std::move(result), png), reply.modern));
			return;
		}
		if (Clock::now() >= reply.pending->deadline)
		{
			reply.pending->cancelled = true;
			Result result;
			result.error = "UI operation deadline exceeded";
			result.data["input_started"] = reply.pending->inputStarted.load();
			write(200, Protocol::response(reply.id, toolResult(std::move(result)), reply.modern));
			return;
		}
		timer.expires_after(std::chrono::milliseconds(10));
		timer.async_wait(
			[self = shared_from_this()](auto ec)
			{
				if (!ec)
					self->poll();
			});
	}
};
void HttpServer::Impl::accept()
{
	acceptor.async_accept(
		[this](auto ec, Tcp::socket socket)
		{
			if (!ec)
			{
				if (sessions.size() < 16)
				{
					auto session = std::make_shared<Session>(*this, std::move(socket));
					sessions.insert(session);
					session->start();
				}
				else
				{
					boost::system::error_code ignored;
					socket.close(ignored);
				}
			}
			if (acceptor.is_open())
				accept();
		});
}
void HttpServer::Impl::stop()
{
	if (stopping.exchange(true))
		return;
	// Keep acceptor and Session access confined to io's thread. The owner retains
	// this implementation until stopped(), or joins it at final application shutdown.
	asio::post(io,
			   [this]
			   {
				   boost::system::error_code ec;
				   acceptor.close(ec);
				   auto retiring = sessions;
				   for (const auto &session : retiring)
					   session->close();
			   });
}
HttpServer::Impl::~Impl()
{
	stop();
	if (worker.joinable())
		worker.join();
}
HttpServer::HttpServer(Protocol &p, int port) : impl(std::make_unique<Impl>(p, port)) {}
HttpServer::~HttpServer() = default;
void HttpServer::stop()
{
	impl->stop();
}
bool HttpServer::stopped() const
{
	return impl->completed.load();
}
int HttpServer::boundPort() const
{
	return impl->port;
}
} // namespace MCP
