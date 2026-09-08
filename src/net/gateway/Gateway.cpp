// SPDX-License-Identifier: GPL-3.0-or-later
#include <utility>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/websocket.hpp>
#include <array>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;
using Error = boost::system::error_code;

struct Configuration
{
    std::string listen = "127.0.0.1";
    unsigned short port = 8080;
    std::string lobbyHost = "127.0.0.1", lobbyPort = "7489";
    std::string routerHost = "127.0.0.1", routerPort = "7491";
    std::string origin = "http://127.0.0.1:8765";
    unsigned limit = 256;
};

struct Metrics
{
    unsigned active = 0;
    unsigned long long accepted = 0, rejected = 0, bytesToBackend = 0, bytesToClient = 0;
};

class Session : public std::enable_shared_from_this<Session>
{
    websocket::stream<beast::tcp_stream> ws;
    beast::tcp_stream backend;
    tcp::resolver resolver;
    beast::flat_buffer input{65536};
    http::request_parser<http::empty_body> parser;
    http::response<http::string_body> response;
    std::array<char, 16384> backendBuffer{};
    const Configuration& config;
    std::shared_ptr<Metrics> metrics;
    bool stopped = false;

    void stop()
    {
        if (stopped) return;
        stopped = true;
        resolver.cancel();
        Error ignored;
        backend.socket().cancel(ignored);
        backend.socket().close(ignored);
        beast::get_lowest_layer(ws).socket().cancel(ignored);
        beast::get_lowest_layer(ws).socket().close(ignored);
    }

    void reply(http::status status, std::string body)
    {
        response = {status, 11};
        response.set(http::field::content_type, "text/plain");
        response.keep_alive(false);
        response.body() = std::move(body);
        response.prepare_payload();
        http::async_write(ws.next_layer(), response, [self=shared_from_this()](Error, size_t) { self->stop(); });
    }

    void connectBackend(const std::string& host, const std::string& port)
    {
        backend.expires_after(std::chrono::seconds(10));
        resolver.async_resolve(host, port, [self=shared_from_this()](Error ec, tcp::resolver::results_type endpoints) {
            if (ec) { self->reply(http::status::bad_gateway,"Backend unavailable\n"); return; }
            self->backend.async_connect(endpoints, [self](Error error, const tcp::resolver::results_type::endpoint_type&) {
                if (error) { self->reply(http::status::bad_gateway,"Backend unavailable\n"); return; }
                self->backend.expires_never();
                beast::get_lowest_layer(self->ws).expires_never();
                self->ws.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
                self->ws.read_message_max(65536);
                self->ws.binary(true);
                self->ws.async_accept(self->parser.get(), [self](Error accepted) {
                    if (accepted) { self->stop(); return; }
                    ++self->metrics->accepted;
                    self->readClient();
                    self->readBackend();
                });
            });
        });
    }

    void readClient()
    {
        ws.async_read(input, [self=shared_from_this()](Error ec, size_t length) {
            if (ec || !self->ws.got_binary()) { self->stop(); return; }
            self->metrics->bytesToBackend += length;
            // One write in flight: stalled TCP applies backpressure to WebSocket reads.
            asio::async_write(self->backend.socket(), self->input.data(), [self](Error error, size_t) {
                if (error) { self->stop(); return; }
                self->input.consume(self->input.size());
                self->readClient();
            });
        });
    }

    void readBackend()
    {
        backend.async_read_some(asio::buffer(backendBuffer), [self=shared_from_this()](Error ec, size_t length) {
            if (ec) { self->stop(); return; }
            self->metrics->bytesToClient += length;
            // TCP chunk boundaries are not protocol message boundaries.
            self->ws.async_write(asio::buffer(self->backendBuffer.data(),length), [self](Error error, size_t) {
                if (error) { self->stop(); return; }
                self->readBackend();
            });
        });
    }

public:
    Session(tcp::socket socket, const Configuration& configuration, std::shared_ptr<Metrics> counters)
        : ws(std::move(socket)), backend(ws.get_executor()), resolver(ws.get_executor()), config(configuration), metrics(counters)
    {
        ++metrics->active;
        parser.header_limit(8192);
        parser.body_limit(0);
    }
    ~Session() { --metrics->active; }

    void run()
    {
        beast::get_lowest_layer(ws).expires_after(std::chrono::seconds(10));
        http::async_read(ws.next_layer(), input, parser, [self=shared_from_this()](Error ec, size_t) {
            if (ec) { self->stop(); return; }
            const auto& request = self->parser.get();
            if (request.method() != http::verb::get) {
                self->reply(http::status::method_not_allowed,"GET required\n"); return;
            }
            if (request.target() == "/healthz") { self->reply(http::status::ok,"ok\n"); return; }
            if (request.target() == "/metrics") {
                self->reply(http::status::ok,
                    "glob2_gateway_connections " + std::to_string(self->metrics->active) + "\n" +
                    "glob2_gateway_accepted_total " + std::to_string(self->metrics->accepted) + "\n" +
                    "glob2_gateway_rejected_total " + std::to_string(self->metrics->rejected) + "\n" +
                    "glob2_gateway_bytes_to_backend_total " + std::to_string(self->metrics->bytesToBackend) + "\n" +
                    "glob2_gateway_bytes_to_client_total " + std::to_string(self->metrics->bytesToClient) + "\n");
                return;
            }
            const auto origin = request[http::field::origin];
            if (!websocket::is_upgrade(request) || (!origin.empty() && origin != self->config.origin)) {
                ++self->metrics->rejected;
                self->reply(http::status::forbidden,"Upgrade or origin rejected\n"); return;
            }
            if (request.target() == "/yog") self->connectBackend(self->config.lobbyHost,self->config.lobbyPort);
            else if (request.target() == "/router") self->connectBackend(self->config.routerHost,self->config.routerPort);
            else { ++self->metrics->rejected; self->reply(http::status::not_found,"Unknown backend\n"); }
        });
    }
};

class Listener
{
    tcp::acceptor acceptor;
    const Configuration& config;
    std::shared_ptr<Metrics> metrics = std::make_shared<Metrics>();
public:
    Listener(asio::io_context& context, const Configuration& configuration)
        : acceptor(context), config(configuration)
    {
        tcp::endpoint endpoint(asio::ip::make_address(config.listen), config.port);
        acceptor.open(endpoint.protocol());
        acceptor.set_option(asio::socket_base::reuse_address(true));
        acceptor.bind(endpoint);
        acceptor.listen();
        std::cout << "gateway listening on " << acceptor.local_endpoint() << std::endl;
    }
    void run()
    {
        acceptor.async_accept([this](Error ec, tcp::socket socket) {
            if (!ec) {
                if (metrics->active < config.limit) std::make_shared<Session>(std::move(socket),config,metrics)->run();
                else { ++metrics->rejected; Error ignored; socket.close(ignored); }
            }
            if (acceptor.is_open()) run();
        });
    }
};

int main(int argc, char** argv)
{
    try {
        Configuration config;
        for (int i=1;i<argc;++i) {
            std::string option=argv[i];
            if (option=="--help") {
                std::cout << "glob2-ws-gateway [--listen ADDRESS] [--port PORT] [--origin ORIGIN] "
                             "[--lobby-host HOST] [--lobby-port PORT] [--router-host HOST] [--router-port PORT]\n";
                return 0;
            }
            if (++i == argc) throw std::runtime_error("Missing option value");
            std::string value=argv[i];
            if(option=="--listen") config.listen=value;
            else if(option=="--port") {
                size_t consumed=0; int port=std::stoi(value,&consumed);
                if(consumed!=value.size() || port<0 || port>65535) throw std::runtime_error("Invalid port");
                config.port=static_cast<unsigned short>(port);
            }
            else if(option=="--origin") config.origin=value;
            else if(option=="--lobby-host") config.lobbyHost=value;
            else if(option=="--lobby-port") config.lobbyPort=value;
            else if(option=="--router-host") config.routerHost=value;
            else if(option=="--router-port") config.routerPort=value;
            else throw std::runtime_error("Unknown option: "+option);
        }
        asio::io_context context;
        asio::signal_set signals(context,SIGINT,SIGTERM);
        signals.async_wait([&](Error,int){context.stop();});
        Listener listener(context,config);
        listener.run();
        context.run();
    } catch(const std::exception& error) {
        std::cerr << "gateway: " << error.what() << '\n';
        return 1;
    }
}
