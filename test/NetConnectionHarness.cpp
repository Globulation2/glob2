// SPDX-License-Identifier: GPL-3.0-or-later
#include "NetConnection.h"
#include "NetListener.h"
#include "message/AuthMessages.h"
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
            require(SDL_Init(0) == 0 && SDLNet_Init() == 0, "SDL network init failed");
            globalContainer = new GlobalContainer(argv[2]);
            YOGServer server(YOGRequirePassword, YOGMultipleGames);
            require(server.isListening(), "YOG test port is already occupied");
            require(server.registerInformation("transportplayer", "fixture-only", "127.0.0.1", NET_PROTOCOL_VERSION)
                    == YOGLoginSuccessful, "Could not create isolated test account");
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
        auto joined = frame;
        joined.insert(joined.end(), frame.begin(), frame.end());
        wire.input.push_back(joined);
        require(connection.getMessage() && connection.getMessage() && !connection.getMessage(), "Coalesced frames lost boundaries");
        for (const auto& invalid : std::vector<std::vector<uint8_t>>{
            {0, 0}, {0, 1, 255},
            {0, 5, MNetAttemptLogin, 255, 255, 255, 255}, {0, 1, original->getMessageType()},
            {0, 4, original->getMessageType(), 0, 1, 0}}) {
            connection.openConnection("unused", 0); wire.current = NetTransport::State::Connected;
            wire.input.push_back(invalid); connection.update();
            require(!connection.isConnected(), "Malformed message did not close the connection");
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
        if (argc == 2) {
            require(SDL_Init(0) == 0 && SDLNet_Init() == 0, "SDL network init failed");
            {
                NetListener listener(static_cast<Uint16>(std::stoi(argv[1])));
                require(listener.isListening(), "Loopback listener failed");
                NetConnection client("127.0.0.1", static_cast<Uint16>(std::stoi(argv[1]))), server;
                client.sendMessage(original); // Queue before connection completion.
                bool accepted = false, echoed = false;
                const auto deadline = SDL_GetTicks64() + 5000;
                while (SDL_GetTicks64() < deadline && !echoed) {
                    if (!accepted) accepted = listener.attemptConnection(server);
                    client.update();
                    if (auto message = server.getMessage()) server.sendMessage(message);
                    if (auto message = client.getMessage()) echoed = *message == *original;
                    SDL_Delay(1);
                }
                require(accepted && echoed, "Native TCP message round trip failed");
            }
            SDLNet_Quit(); SDL_Quit();
        }
        std::cout << "PASS: shared framing, malformed input, queue limits, queued greeting and TCP round trip\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
