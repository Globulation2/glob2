// SPDX-License-Identifier: GPL-3.0-or-later
#include "NetworkConfig.h"
#include <chrono>
#include <algorithm>
#include <iostream>
#include <thread>
#include <csignal>
#include <cstdlib>
namespace { volatile std::sig_atomic_t stopped = 0; void stop(int) { stopped = 1; } }
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    try {
        auto c = makeNetworkConfig(std::string(argv[2]) == "lan");
        auto config = std::string(argv[2]) == "register" ? c.registration : c.lobby;
        config.bindAddress = "127.0.0.1"; config.port = std::stoi(argv[1]);
        if (std::string(argv[2]) == "text") config.messageMode = NetMessageMode::Text;
        auto listener = makeNetTransportListener(config);
        std::signal(SIGTERM, stop); std::signal(SIGINT, stop);
        const auto pin = c.lan ? c.lobbyEndpoint.substr(c.lobbyEndpoint.find('#')) : std::string();
        std::cout << "READY wss://localhost:" << config.port << config.route << pin << std::endl;
        std::vector<std::unique_ptr<NetTransport>> sessions;
        while (!stopped) {
            while (auto session = listener->accept()) sessions.push_back(std::move(session));
            for (auto& session : sessions) {
                std::string message;
                while (session->receiveText(message)) session->sendText(std::move(message));
                std::vector<uint8_t> bytes;
                while (session->receive(bytes)) {
                    if (std::string(argv[2]) == "peer") {
                        const auto peer = session->peerAddress(); bytes.assign(peer.begin(), peer.end());
                    }
                    session->send(std::move(bytes));
                }
            }
            sessions.erase(std::remove_if(sessions.begin(), sessions.end(), [](const auto& session) {
                return session->state() == NetTransport::State::Closed;
            }), sessions.end());
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
