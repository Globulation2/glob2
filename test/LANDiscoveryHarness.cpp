// SPDX-License-Identifier: GPL-3.0-or-later
#include "NetBroadcastListener.h"
#include "NetBroadcaster.h"
#include "NetConsts.h"
#include <SDL3/SDL.h>
#include <Environment.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void announce(NET_DatagramSocket * socket, const std::string& bytes) {
    NET_Address *address = NET_ResolveHostname("127.0.0.1");
    require(address && NET_WaitUntilResolved(address, 1000) == NET_SUCCESS, "loopback resolution failed");
    const bool sent = NET_SendDatagram(socket, address, LAN_BROADCAST_PORT, bytes.data(), bytes.size());
    NET_UnrefAddress(address);
    require(sent, "discovery send failed");
}
}
int main() {
    try {
        require(SDL_Init(0) && NET_Init(), "network initialization failed");
        NetBroadcastListener listener;
        require(listener.isListening(), "discovery port unavailable");
        NET_DatagramSocket * socket = NET_CreateDatagramSocket(nullptr, 0, 0); require(socket != nullptr, "sender unavailable");
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
        NET_Address *bind = NET_ResolveHostname("0.0.0.0");
        require(bind && NET_WaitUntilResolved(bind, 1000) == NET_SUCCESS, "IPv4 bind resolution failed");
        NET_DatagramSocket * capture = NET_CreateDatagramSocket(bind, LAN_BROADCAST_PORT, 0);
        NET_UnrefAddress(bind); require(capture != nullptr, "beacon receiver unavailable");
        {
            NetBroadcaster broadcaster(id, endpoint + "#sha256=" + std::string(64, 'a'));
            NET_Datagram *packet = nullptr;
            bool received = false;
            for (unsigned i = 0; i < 100 && !received; ++i) {
                broadcaster.update(); SDL_Delay(10);
                if (NET_ReceiveDatagram(capture, &packet) && packet) {
                    const std::string bytes(reinterpret_cast<const char*>(packet->buf), packet->buflen);
                    require(bytes == "G2D1" + id + endpoint, "broadcast exposed extra metadata or fingerprint");
                    std::cout << "Captured discovery beacon: " << bytes << '\n'; received = true;
                }
            }
            if (packet) NET_DestroyDatagram(packet); require(received, "broadcast was not received");
        }
        NET_DestroyDatagramSocket(capture);
        NET_DestroyDatagramSocket(socket);
        std::cout << "LAN discovery: bounded untrusted hints, multiple sessions, expiry, and permission recovery PASS\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
