// SPDX-License-Identifier: GPL-3.0-or-later
#include "NetConnection.h"
#include <vector>
#include <string>
#include <memory>
#include <utility>
#include <exception>
#include "NetListener.h"
#include "NetworkConfig.h"
#include "message/AuthMessages.h"
#include "message/FileTransferMessages.h"
#include "message/RegistrationMessages.h"
#include "message/RouterAdminMessages.h"
#include "GlobalContainer.h"
#include "YOGServer.h"
#include "Version.h"
#include <deque>
#include <iostream>
#include <stdexcept>

GlobalContainer* globalContainer = nullptr;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void checkLanDatagrams()
{
    // Upstream native-WSS discovery already uses G2D1; migration keeps that
    // SDL2 wire format and rejects length violations before endpoint decoding.
    NetBroadcastListener listener;
    NET_Address *loopback = NET_ResolveHostname("127.0.0.1");
    require(loopback && NET_WaitUntilResolved(loopback, 1000) == NET_SUCCESS, "LAN resolution failed");
    NET_DatagramSocket *sender = NET_CreateDatagramSocket(loopback, 0, 0);
    require(sender != nullptr, "LAN sender failed");
    const std::string id(32, 'a'), endpoint = "wss://127.0.0.1:7489/yog";
    auto send = [&](const std::string &bytes) {
        require(NET_SendDatagram(sender, loopback, LAN_BROADCAST_PORT, bytes.data(), bytes.size()), "LAN send failed");
        const auto deadline = SDL_GetTicks() + 100;
        do { listener.update(); SDL_Delay(1); } while (SDL_GetTicks() < deadline);
    };
    send("G2D1");
    send("G2D1" + id + std::string(513, 'x'));
    require(listener.getLANHosts().empty(), "Malformed LAN advertisement entered the host list");
    send("G2D1" + id + endpoint);
    require(listener.getLANHosts().size() == 1 && listener.getIPAddress(0) == endpoint,
        "Existing SDL2 WSS LAN advertisement was not discovered");
    {
        const std::string second(32, 'b');
        NetBroadcaster broadcaster(second, endpoint);
        const auto deadline = SDL_GetTicks() + 1500;
        while (SDL_GetTicks() < deadline && listener.getLANHosts().size() != 2) {
            broadcaster.update(); listener.update(); SDL_Delay(1);
        }
        require(listener.getLANHosts().size() == 2, "SDL3 broadcaster lost IPv4 discovery");
    }
    NET_DestroyDatagramSocket(sender);
    NET_UnrefAddress(loopback);
}
class FakeTransport : public NetTransport {
public:
    State current = State::Closed;
    std::deque<std::vector<uint8_t>> input;
    std::vector<std::vector<uint8_t>> output;
    void open(const std::string&, uint16_t) override { current = State::Connecting; }
    void close() override { current = State::Closed; input.clear(); }
    State state() const override { return current; }
    bool send(std::vector<uint8_t> bytes) override { output.push_back(std::move(bytes)); return true; }
    bool receive(std::vector<uint8_t>& bytes) override {
        if (input.empty()) return false;
        bytes = std::move(input.front()); input.pop_front(); return true;
    }
};
}
int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--serve") {
            require(SDL_Init(0) && NET_Init(), "SDL network init failed");
            globalContainer = new GlobalContainer(argv[2]);
            YOGServer server(YOGRequirePassword, YOGMultipleGames);
            require(server.isListening(), "YOG test port is already occupied");
            for (const auto* name : {"transportplayer", "transportguest"})
                require(server.registerInformation(name, "fixture-only", "127.0.0.1", NET_PROTOCOL_VERSION)
                        == YOGLoginSuccessful, "Could not create isolated test account");
            for (Uint16 version : {Uint16(NET_PROTOCOL_VERSION-1), Uint16(NET_PROTOCOL_VERSION+1)}) {
                require(server.verifyLoginInformation("transportplayer", "fixture-only", "127.0.0.1", version)
                        == YOGClientVersionTooOld, "Incompatible version passed account verification");
                require(server.registerInformation("incompatibleregistration", "fixture-only", "127.0.0.1", version)
                        == YOGClientVersionTooOld, "Incompatible version created an account");
            }
            std::cout << "YOG test server ready" << std::endl;
            for (;;) { server.update(); SDL_Delay(10); }
        }
        const std::string secret = "must-not-appear-in-logs";
        require(NetAttemptLogin("test", secret).format().find(secret) == std::string::npos &&
                NetRegistrationRequest("test", secret).format().find(secret) == std::string::npos &&
                NetRouterAdministratorLogin(secret).format().find(secret) == std::string::npos,
                "Credential message formatting exposed a password");
        auto selected = std::make_unique<FakeTransport>();
        auto& wire = *selected;
        NetConnection connection(std::move(selected));
        auto original = std::make_shared<NetSendClientInformation>();
        connection.openConnection("unused", 0);
        connection.sendMessage(original);
        require(wire.output.empty(), "Connecting must retain, not send, greeting");
        wire.current = NetTransport::State::Connected;
        connection.update();
        require(wire.output.size() == 1, "Greeting lost while connecting");
        const auto frame = wire.output.front();
        // Split at every possible byte boundary, including the length prefix.
        for (size_t cut = 1; cut < frame.size(); ++cut) {
            wire.input.emplace_back(frame.begin(), frame.begin() + cut);
            require(!connection.getMessage(), "Partial frame was decoded");
            wire.input.emplace_back(frame.begin() + cut, frame.end());
            const auto decoded = connection.getMessage();
            require(decoded && *decoded == *original, "Fragmented frame changed the message");
        }
        const auto serverInfo = std::make_shared<NetSendServerInformation>(YOGRequirePassword, YOGMultipleGames, 17);
        connection.sendMessage(serverInfo);
        wire.input.push_back(wire.output.back());
        const auto decodedInfo = connection.getMessage();
        require(decodedInfo && *decodedInfo == *serverInfo, "Server version or player identity lost in greeting");
        wire.input.push_back({0,5,MNetSendServerInformation,YOGRequirePassword,YOGMultipleGames,0,17});
        const auto legacyInfo = std::dynamic_pointer_cast<NetSendServerInformation>(connection.getMessage());
        require(legacyInfo && legacyInfo->getNetVersion() == 0, "Legacy greeting must advertise incompatible version zero");
        auto joined = frame;
        joined.insert(joined.end(), frame.begin(), frame.end());
        wire.input.push_back(joined);
        require(connection.getMessage() && connection.getMessage() && !connection.getMessage(), "Coalesced frames lost boundaries");
        for (const auto& invalid : std::vector<std::vector<uint8_t>>{
            {0, 0}, {0, 1, 255},
            {0, 13, MNetSendMapHeader, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 124},
            {0, 5, MNetSendGamePlayerInfo, 0, 0, 0, 33},
            {0, 5, MNetAttemptLogin, 255, 255, 255, 255},
            {0, 5, MNetDownloadableMapInfos, 255, 255, 255, 255},
            {0, 5, MNetDownloadableMapInfos, 0, 0, 0, 1}, {0, 1, original->getMessageType()},
            {0, 4, original->getMessageType(), 0, 1, 0},
            {0, 6, MNetSendServerInformation, YOGRequirePassword, YOGMultipleGames, 0, 17, 0}}) {
            connection.openConnection("unused", 0); wire.current = NetTransport::State::Connected;
            wire.input.push_back(invalid); connection.update();
            require(!connection.isConnected(), "Malformed message did not close the connection");
        }
        std::vector<uint8_t> invalidGame{0, 44, MNetSendGameHeader, 0, 0, 0, 0, 6};
        invalidGame.insert(invalidGame.end(), 32, 0);
        invalidGame.insert(invalidGame.end(), {0, 0, 0, 0, 1, 255});
        connection.openConnection("unused", 0); wire.current = NetTransport::State::Connected;
        wire.input.push_back(std::move(invalidGame)); connection.update();
        require(!connection.isConnected(), "Invalid winning condition was accepted");
        // Supply the entire oversized payload: truncation checks alone cannot protect
        // the fixed chunk buffer. The exact 4096-byte capacity must still work.
        for (unsigned size : {4096u, 4097u}) {
            const unsigned length = 1 + 4 + size + 2;
            std::vector<uint8_t> chunk{uint8_t(length >> 8), uint8_t(length),
                MNetSendFileChunk, 0, 0, uint8_t(size >> 8), uint8_t(size)};
            chunk.insert(chunk.end(), size, 0x5a);
            chunk.insert(chunk.end(), {0, 17});
            connection.openConnection("unused", 0); wire.current = NetTransport::State::Connected;
            wire.input.push_back(std::move(chunk)); connection.update();
            if (size == 4096)
                require(connection.isConnected() && connection.getMessage(), "Maximum valid file chunk was rejected");
            else
                require(!connection.isConnected(), "Oversized complete file chunk was accepted");
        }
        connection.openConnection("unused", 0); wire.current = NetTransport::State::Connected;
        joined.clear();
        for (unsigned i = 0; i < 257; ++i) joined.insert(joined.end(), frame.begin(), frame.end());
        wire.input.push_back(joined); connection.update();
        require(!connection.isConnected() && !connection.getMessage(), "Inbound queue was not bounded");
        connection.openConnection("unused", 0);
        auto large = std::make_shared<NetAttemptLogin>(std::string(20000, 'x'), "");
        for (unsigned i = 0; i < 100 && connection.isConnecting(); ++i) connection.sendMessage(large);
        require(!connection.isConnecting(), "Connecting output queue was not bounded");
        connection.openConnection("unused", 0); wire.current = NetTransport::State::Connected;
        connection.sendMessage(std::make_shared<NetAttemptLogin>(std::string(70000, 'x'), ""));
        require(!connection.isConnected(), "Oversized frame length was truncated");
        if (argc == 2 || (argc == 3 && std::string(argv[1]) != "--serve")) {
            require(SDL_Init(0) && NET_Init(), "SDL network init failed");
            {
                auto config = makeNetworkConfig(true); config.lobby.bindAddress = "::";
                config.lobby.port = std::stoi(argv[1]);
                NetListener listener(config.lobby);
                require(listener.isListening(), "Loopback listener failed");
                const std::string host = argc == 3 ? argv[2] : "localhost";
                const std::string authority = host.find(':') == std::string::npos ? host : "[" + host + "]";
                NetConnection client("wss://" + authority + ":" + std::string(argv[1]) + "/yog" +
                    config.lobbyEndpoint.substr(config.lobbyEndpoint.find('#')), 0), server;
                client.sendMessage(original); // Queue before connection completion.
                bool accepted = false, echoed = false;
                const auto deadline = SDL_GetTicks() + 5000;
                while (SDL_GetTicks() < deadline && !echoed) {
                    if (!accepted) accepted = listener.attemptConnection(server);
                    client.update();
                    if (auto message = server.getMessage()) server.sendMessage(message);
                    if (auto message = client.getMessage()) echoed = *message == *original;
                    SDL_Delay(1);
                }
                require(accepted && echoed, "Native WSS message round trip failed");
                // A one-way burst must drain without waiting for replies between frames.
                const auto burstStart = SDL_GetTicks();
                for (unsigned i = 0; i < 200; ++i) client.sendMessage(original);
                unsigned delivered = 0;
                while (delivered < 200 && SDL_GetTicks() - burstStart < 2000) {
                    client.update();
                    while (auto message = server.getMessage()) {
                        require(*message == *original, "WSS burst changed a message");
                        ++delivered;
                    }
                    SDL_Delay(1);
                }
                require(delivered == 200, "WSS burst stalled waiting for unrelated incoming traffic");
            }
            {
                // An overlong DNS label fails locally without depending on a public resolver.
                auto failing = makeNetTransport();
                failing->open(std::string(256, 'x') + ".invalid", 7489);
                const auto timeout = SDL_GetTicks() + 5000;
                while (failing->state() == NetTransport::State::Connecting && SDL_GetTicks() < timeout)
                    SDL_Delay(1);
                require(failing->state() == NetTransport::State::Closed, "DNS failure did not close transport");
                auto cancelled = makeNetTransport();
                cancelled->open("192.0.2.1", 7489);
                const auto start = SDL_GetTicks();
                cancelled->close();
                require(SDL_GetTicks() - start < 1000 && cancelled->state() == NetTransport::State::Closed,
                        "Connection cancellation blocked or retained a live transport");
            }
            {
                NetListener listener(static_cast<Uint16>(std::stoi(argv[1])));
                require(listener.isListening(), "Dual-stack TCP listener failed");
                NetConnection client(argc == 3 ? argv[2] : "127.0.0.1", static_cast<Uint16>(std::stoi(argv[1]))), server;
                bool accepted = false;
                const auto deadline = SDL_GetTicks() + 5000;
                while (!accepted && SDL_GetTicks() < deadline) {
                    accepted = listener.attemptConnection(server);
                    SDL_Delay(1);
                }
                require(accepted, "TCP accept failed");
                // The address must be available on return, without waiting for
                // the transport worker: YOG logging and bans read it immediately.
                require(server.getIPAddress() == "127.0.0.1" || server.getIPAddress() == "::1",
                    "Accepted TCP peer was empty or an uncanonicalized mapped address");
            }
            {
                // Keep an accepted peer unread so SDL_net itself must queue writes.
                NET_Server *listener = NET_CreateServer(nullptr, static_cast<Uint16>(std::stoi(argv[1])), 0);
                require(listener != nullptr, "Backpressure listener failed");
                auto sender = makeNetTransport();
                sender->open(argc == 3 ? argv[2] : "127.0.0.1", static_cast<Uint16>(std::stoi(argv[1])));
                NET_StreamSocket *peer = nullptr;
                const auto timeout = SDL_GetTicks() + 5000;
                while ((!peer || sender->state() == NetTransport::State::Connecting) && SDL_GetTicks() < timeout) {
                    if (!peer) require(NET_AcceptClient(listener, &peer), "Backpressure accept failed");
                    SDL_Delay(1);
                }
                require(peer && sender->state() == NetTransport::State::Connected, "Backpressure connection failed");
                bool bounded = false;
                for (unsigned i = 0; i < 1000 && !bounded; ++i) {
                    bounded = !sender->send(std::vector<uint8_t>(64 * 1024, 42));
                    SDL_Delay(1);
                }
                require(bounded, "Stalled writes exceeded the bounded transport queue");
                NET_DestroyStreamSocket(peer);
                const auto disconnected = SDL_GetTicks() + 5000;
                while (sender->state() != NetTransport::State::Closed && SDL_GetTicks() < disconnected)
                    SDL_Delay(1);
                require(sender->state() == NetTransport::State::Closed, "Peer disconnect retained transport");
                sender->close();
                NET_DestroyServer(listener);
            }
            checkLanDatagrams();
            NET_Quit(); SDL_Quit();
        }
        std::cout << "PASS: shared framing, malformed input, queue limits, queued greeting and WSS round trip\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
