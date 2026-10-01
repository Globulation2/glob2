// SPDX-License-Identifier: GPL-3.0-or-later
#include "NetBroadcastListener.h"
#include "NetBroadcaster.h"
#include "NetConsts.h"
#include "SDLCompat.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void announce(UDPsocket socket, const std::string& bytes) {
    UDPpacket* packet = SDLNet_AllocPacket(bytes.size());
    require(packet != nullptr, "packet allocation failed");
    SDLNet_ResolveHost(&packet->address, "127.0.0.1", LAN_BROADCAST_PORT);
    std::copy(bytes.begin(), bytes.end(), packet->data); packet->len = bytes.size();
    const int sent = SDLNet_UDP_Send(socket, -1, packet);
    SDLNet_FreePacket(packet); require(sent == 1, "discovery send failed");
}
}
int main() {
    try {
        require(SDL_Init(0) == 0 && SDLNet_Init() == 0, "network initialization failed");
        NetBroadcastListener listener;
        require(listener.isListening(), "discovery port unavailable");
        UDPsocket socket = SDLNet_UDP_Open(0); require(socket != nullptr, "sender unavailable");
        const std::string id(32, 'a'), endpoint = "wss://127.0.0.1:7489/yog";
        for (const auto& invalid : {"G2D0" + id + endpoint, "G2D1short" + endpoint,
                "G2D1" + id + endpoint + "#sha256=" + std::string(64, 'a'),
                "G2D1" + id + "wss://192.0.2.1/yog", "G2D1" + id + std::string(600, 'x')})
            announce(socket, invalid);
        SDL_Delay(20); listener.update();
        require(listener.getLANHosts().empty(), "untrusted or malformed discovery admitted");
        announce(socket, "G2D1" + id + endpoint);
        announce(socket, "G2D1" + std::string(32, 'b') + endpoint);
        SDL_Delay(20); listener.update();
        require(listener.getLANHosts().size() == 2, "multiple hosts or replacement session lost");
        announce(socket, "G2D1" + id + endpoint); SDL_Delay(20); listener.update();
        require(listener.getLANHosts().size() == 2, "duplicate beacon created another host");
        SDL_Delay(2100); listener.update();
        require(listener.getLANHosts().empty(), "old host identity did not expire");
        listener.disableListening(); require(!listener.isListening(), "discovery denial unavailable");
        listener.update(); listener.enableListening(); require(listener.isListening(), "discovery cannot resume");
        listener.disableListening();
        UDPsocket capture = SDLNet_UDP_Open(LAN_BROADCAST_PORT); require(capture != nullptr, "beacon receiver unavailable");
        {
            NetBroadcaster broadcaster(id, endpoint + "#sha256=" + std::string(64, 'a'));
            UDPpacket* packet = SDLNet_AllocPacket(549); require(packet != nullptr, "beacon allocation failed");
            bool received = false;
            for (unsigned i = 0; i < 100 && !received; ++i) {
                broadcaster.update(); SDL_Delay(10);
                if (SDLNet_UDP_Recv(capture, packet) == 1) {
                    const std::string bytes(reinterpret_cast<const char*>(packet->data), packet->len);
                    require(bytes == "G2D1" + id + endpoint, "broadcast exposed extra metadata or fingerprint");
                    std::cout << "Captured discovery beacon: " << bytes << '\n'; received = true;
                }
            }
            SDLNet_FreePacket(packet); require(received, "broadcast was not received");
        }
        SDLNet_UDP_Close(capture);
        SDLNet_UDP_Close(socket);
        std::cout << "LAN discovery: bounded untrusted hints, multiple sessions, expiry, and permission recovery PASS\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
