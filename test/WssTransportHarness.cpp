// SPDX-License-Identifier: GPL-3.0-or-later
#include "NetTransport.h"
#include <vector>
#include <string>
#include <exception>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    using Clock = std::chrono::steady_clock;
    try {
        const std::string mode = argv[2];
        const bool textMode = mode == "text" || mode == "textbadframe";
        auto transport = makeNetTransport({}, textMode ? NetMessageMode::Text : NetMessageMode::Binary);
        const auto start = Clock::now();
        transport->open(argv[1], mode == "router" ? 7491 : 7489);
        while (transport->state() == NetTransport::State::Connecting && Clock::now() - start < std::chrono::seconds(12)) {
            if (mode == "cancel" && Clock::now() - start > std::chrono::milliseconds(100)) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (mode == "cancel") {
            const auto closing = Clock::now(); transport->close();
            if (Clock::now() - closing > std::chrono::milliseconds(500)) throw std::runtime_error("Close blocked");
        } else if (mode == "refuse" || mode == "timeout") {
            if (transport->state() != NetTransport::State::Closed) throw std::runtime_error("Invalid peer accepted or timeout missing");
        } else if (textMode) {
            if (transport->state() != NetTransport::State::Connected) throw std::runtime_error("Secure connection failed: " + transport->error());
            if (transport->send(std::vector<uint8_t>(4))) throw std::runtime_error("Binary send accepted in text mode");
            if (transport->sendText(std::string(NetTransport::textMessageLimit + 1, 'x'))) throw std::runtime_error("Text limit missing");
            if (transport->sendText(std::string("a\0b", 3))) throw std::runtime_error("NUL accepted in text");
            std::string large(200 * 1024, ' ');
            for (size_t i = 0; i < large.size(); ++i) large[i] = 'a' + i % 26;
            const std::vector<std::string> sent = {"{\"type\":\"hello\",\"name\":\"Caf\xc3\xa9\"}", "", large, "[1,2,3]"};
            for (const auto& message : sent)
                if (!transport->sendText(message) && mode == "text") throw std::runtime_error("Text send failed");
            std::vector<std::string> received;
            std::string part;
            const auto deadline = Clock::now() + std::chrono::seconds(5);
            while (Clock::now() < deadline && received.size() < sent.size() && transport->state() == NetTransport::State::Connected) {
                while (transport->receiveText(part)) received.push_back(part);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if (mode == "textbadframe") {
                if (transport->state() != NetTransport::State::Closed) throw std::runtime_error("Binary frame accepted in text mode");
            } else if (sent != received) throw std::runtime_error("Text echo differed: " + std::to_string(received.size()) + " messages");
            transport->close();
        } else {
            if (transport->state() != NetTransport::State::Connected) throw std::runtime_error("Secure connection failed: " + transport->error());
            if (transport->sendText("text")) throw std::runtime_error("Text send accepted in binary mode");
            if (transport->send(std::vector<uint8_t>(NetTransport::queueLimit + 1))) throw std::runtime_error("Outbound limit missing");
            std::vector<uint8_t> sent(64000), received, part;
            for (size_t i = 0; i < sent.size(); ++i) sent[i] = i % 251;
            if (!transport->send(sent)) throw std::runtime_error("Send failed");
            const auto deadline = Clock::now() + std::chrono::seconds(5);
            while (Clock::now() < deadline && received.size() < sent.size() && transport->state() == NetTransport::State::Connected) {
                while (transport->receive(part)) received.insert(received.end(), part.begin(), part.end());
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if (mode == "badframe") {
                if (transport->state() != NetTransport::State::Closed) throw std::runtime_error("Invalid frame accepted");
            } else if (sent != received) throw std::runtime_error("Secure echo differed");
            transport->close();
        }
        std::cout << "PASS: native WSS " << mode << '\n';
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
