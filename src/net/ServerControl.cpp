// SPDX-License-Identifier: GPL-3.0-or-later
#include "ServerControl.h"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <csignal>
#include <filesystem>
#include <stdexcept>
#include <vector>
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#endif
namespace
{
volatile std::sig_atomic_t requested = 0;
void stop(int)
{
	requested = 1;
}
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;
using Error = boost::system::error_code;
} // namespace
struct ServerControl::Impl
{
	asio::io_context io;
	tcp::acceptor acceptor{io};
	struct Request
	{
		beast::tcp_stream socket;
		beast::flat_buffer buffer{8192};
		http::request_parser<http::empty_body> request;
		http::response<http::string_body> response;
		bool finished = false;
		explicit Request(tcp::socket socket) : socket(std::move(socket)) {}
		void prepareResponse(const ServerStatus &status)
		{
			const auto target = request.get().target();
			response.version(11);
			response.keep_alive(false);
			response.result(http::status::ok);
			response.set(http::field::content_type, "text/plain");
			if (request.get().method() != http::verb::get)
				response.result(http::status::method_not_allowed);
			else if (target == "/livez")
				response.body() = "alive\n";
			else if (target == "/readyz")
			{
				response.result(status.ready ? http::status::ok
											 : http::status::service_unavailable);
				response.body() = status.ready ? "ready\n" : "unready\n";
			}
			else if (target == "/metrics")
			{
				response.body() = "glob2_ready " + std::to_string(status.ready) +
								  "\nglob2_draining " + std::to_string(status.draining) +
								  "\nglob2_connections " + std::to_string(status.connections) +
								  "\nglob2_games " + std::to_string(status.games) + "\n";
			}
			else
				response.result(http::status::not_found);
			response.prepare_payload();
		}

		void serve(ServerStatus status)
		{
			request.header_limit(4096);
			request.body_limit(0);
			socket.expires_after(std::chrono::seconds(2));
			http::async_read(socket, buffer, request,
							 [this, status](Error ec, size_t)
							 {
								 if (ec)
								 {
									 finished = true;
									 return;
								 }
								 prepareResponse(status);
								 http::async_write(socket, response,
												   [this](Error, size_t)
												   {
													   Error ignored;
													   socket.socket().close(ignored);
													   finished = true;
												   });
							 });
		}
	};
	std::vector<std::unique_ptr<Request>> requests;
	Impl(const std::string &address, unsigned short port)
	{
		tcp::endpoint endpoint(asio::ip::make_address(address), port);
		acceptor.open(endpoint.protocol());
		acceptor.set_option(asio::socket_base::reuse_address(true));
		acceptor.bind(endpoint);
		acceptor.listen();
		acceptor.non_blocking(true);
	}
	void update(const ServerStatus &status)
	{
		for (unsigned i = 0; i < 8 && requests.size() < 32; ++i)
		{
			tcp::socket socket(io);
			Error ec;
			acceptor.accept(socket, ec);
			if (ec)
				break;
			auto request = std::make_unique<Request>(std::move(socket));
			request->serve(status);
			requests.push_back(std::move(request));
		}
		io.restart();
		for (unsigned i = 0; i < 64 && io.poll_one(); ++i)
		{
		}
		requests.erase(std::remove_if(requests.begin(), requests.end(),
									  [](const auto &request) { return request->finished; }),
					   requests.end());
	}
};
ServerControl::ServerControl(const std::string &address, unsigned short port)
	: impl(std::make_unique<Impl>(address, port))
{
}
ServerControl::~ServerControl() = default;
void ServerControl::update(const ServerStatus &status)
{
	impl->update(status);
}
void ServerControl::installSignals()
{
	requested = 0;
	std::signal(SIGTERM, stop);
	std::signal(SIGINT, stop);
}
bool ServerControl::shutdownRequested()
{
	return requested != 0;
}
struct ServerDataLock::Impl
{
#ifdef _WIN32
	HANDLE file = INVALID_HANDLE_VALUE;
	~Impl()
	{
		if (file != INVALID_HANDLE_VALUE)
			CloseHandle(file);
	}
#else
	int file = -1;
	~Impl()
	{
		if (file >= 0)
		{
			flock(file, LOCK_UN);
			::close(file);
		}
	}
#endif
};
ServerDataLock::ServerDataLock(const std::string &directory) : impl(std::make_unique<Impl>())
{
	std::filesystem::create_directories(directory);
	const auto path = std::filesystem::path(directory) / ".yog-owner.lock";
#ifdef _WIN32
	impl->file = CreateFileW(path.wstring().c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
							 OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (impl->file == INVALID_HANDLE_VALUE)
		throw std::runtime_error("YOG data directory already owned or unavailable");
#else
	impl->file = ::open(path.c_str(), O_CREAT | O_RDWR, 0600);
	if (impl->file < 0 || flock(impl->file, LOCK_EX | LOCK_NB) != 0)
		throw std::runtime_error("YOG data directory already owned or unavailable");
#endif
}
ServerDataLock::~ServerDataLock() = default;
