// SPDX-License-Identifier: GPL-3.0-or-later
#include "NetTransport.h"
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <deque>
#include <stdexcept>

namespace {
namespace asio = boost::asio;
namespace ssl = asio::ssl;
namespace beast = boost::beast;
namespace ws = beast::websocket;
using tcp = asio::ip::tcp;
using Error = boost::system::error_code;

// The same application thread that owns NetConnection pumps asynchronous I/O.
// No blocking connect, TLS handshake, read, or write runs on that thread.
class WssTransport final : public NetTransport {
    struct Session {
        asio::io_context io;
        ssl::context tls{ssl::context::tls_client};
        beast::flat_buffer buffer{64 * 1024};
        ws::stream<beast::ssl_stream<beast::tcp_stream>> socket{io, tls};
        tcp::resolver resolver{io};
        asio::steady_timer connectDeadline{io}, writeDeadline{io};
        State status = State::Connecting;
        std::deque<std::vector<uint8_t>> incoming, outgoing;
        size_t incomingBytes = 0, outgoingBytes = 0;
        std::string host, authority, service, route;
        bool writing = false;

        Session(const std::string& address, uint16_t port) {
            // This is a gateway origin, never a credential-bearing URL or a
            // backend chosen by an invitation/server packet.
            if (address.rfind("wss://", 0) != 0) throw std::invalid_argument("Expected wss origin");
            authority = address.substr(6);
            if (!authority.empty() && authority.back() == '/') authority.pop_back();
            if (authority.empty() || authority.find_first_of("/@?#\\ \t\r\n") != std::string::npos)
                throw std::invalid_argument("Invalid wss origin");
            service = "443";
            if (authority.front() == '[') {
                const auto end = authority.find(']');
                if (end == std::string::npos) throw std::invalid_argument("Invalid IPv6 origin");
                host = authority.substr(1, end - 1);
                if (end + 1 < authority.size()) {
                    if (authority[end + 1] != ':') throw std::invalid_argument("Invalid origin port");
                    service = authority.substr(end + 2);
                }
            } else {
                const auto colon = authority.find(':');
                host = authority.substr(0, colon);
                if (colon != std::string::npos) service = authority.substr(colon + 1);
            }
            if (host.empty() || service.empty() || service.find_first_not_of("0123456789") != std::string::npos
                || std::stoul(service) == 0 || std::stoul(service) > 65535)
                throw std::invalid_argument("Invalid origin host or port");
            route = port == 7491 ? "/router" : "/yog";
            SSL_set_min_proto_version(socket.next_layer().native_handle(), TLS1_2_VERSION);
            tls.set_default_verify_paths();
            socket.next_layer().set_verify_mode(ssl::verify_peer);
            socket.next_layer().set_verify_callback(ssl::host_name_verification(host));
            if (!SSL_set_tlsext_host_name(socket.next_layer().native_handle(), host.c_str()))
                throw std::runtime_error("Could not set TLS server name");
            socket.read_message_max(64 * 1024);
            socket.binary(true);
            connectDeadline.expires_after(std::chrono::seconds(10));
            connectDeadline.async_wait([this](Error error) { if (!error) cancel(); });
            resolver.async_resolve(host, service, [this](Error error, tcp::resolver::results_type results) {
                if (!active(error)) return;
                beast::get_lowest_layer(socket).async_connect(results,
                    [this](Error error, const tcp::endpoint&) {
                        if (!active(error)) return;
                        socket.next_layer().async_handshake(ssl::stream_base::client, [this](Error error) {
                            if (!active(error)) return;
                            socket.set_option(ws::stream_base::timeout{std::chrono::seconds(10),
                                std::chrono::seconds(30), true});
                            socket.async_handshake(authority, route, [this](Error error) {
                                if (!active(error)) return;
                                connectDeadline.cancel();
                                status = State::Connected;
                                read();
                            });
                        });
                    });
            });
        }
        ~Session() { cancel(); io.stop(); }
        bool active(Error error) {
            if (error) cancel();
            return status != State::Closed;
        }
        void cancel() {
            status = State::Closed;
            resolver.cancel();
            connectDeadline.cancel(); writeDeadline.cancel();
            Error ignored;
            beast::get_lowest_layer(socket).socket().close(ignored);
        }
        void poll() {
            io.restart();
            for (unsigned i = 0; i < 16 && io.poll_one(); ++i) {}
        }
        void read() {
            socket.async_read(buffer, [this](Error error, size_t size) {
                if (!active(error)) return;
                if (!socket.got_binary() || size > queueLimit - incomingBytes || incoming.size() >= 256) { cancel(); return; }
                if (!size) { read(); return; }
                std::vector<uint8_t> bytes(size);
                asio::buffer_copy(asio::buffer(bytes), buffer.data());
                buffer.consume(size);
                incomingBytes += size;
                incoming.push_back(std::move(bytes));
                read();
            });
        }
        void write() {
            if (writing || outgoing.empty() || status != State::Connected) return;
            writing = true;
            writeDeadline.expires_after(std::chrono::seconds(10));
            writeDeadline.async_wait([this](Error error) { if (!error) cancel(); });
            socket.async_write(asio::buffer(outgoing.front()), [this](Error error, size_t) {
                if (!active(error)) return;
                writeDeadline.cancel();
                outgoingBytes -= outgoing.front().size();
                outgoing.pop_front();
                writing = false;
                write();
            });
        }
    };
    std::unique_ptr<Session> session;
public:
    void open(const std::string& address, uint16_t port) override {
        close();
        try { session = std::make_unique<Session>(address, port); }
        catch (const std::exception&) { session.reset(); }
    }
    void close() override { session.reset(); }
    State state() const override {
        if (!session) return State::Closed;
        session->poll();
        return session->status;
    }
    bool send(std::vector<uint8_t> bytes) override {
        if (state() != State::Connected || bytes.size() > queueLimit - session->outgoingBytes) return false;
        session->outgoingBytes += bytes.size();
        for (size_t offset = 0; offset < bytes.size(); offset += chunkLimit) {
            const auto end = std::min(bytes.size(), offset + chunkLimit);
            session->outgoing.emplace_back(bytes.begin() + offset, bytes.begin() + end);
        }
        session->write();
        return true;
    }
    bool receive(std::vector<uint8_t>& bytes) override {
        if (!session) return false;
        session->poll();
        if (session->incoming.empty()) return false;
        bytes = std::move(session->incoming.front()); session->incoming.pop_front();
        session->incomingBytes -= bytes.size();
        return true;
    }
};
}
std::unique_ptr<NetTransport> makeWssTransport() { return std::make_unique<WssTransport>(); }
