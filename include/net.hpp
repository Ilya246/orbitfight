#pragma once
#include "entities.hpp"
#include "packet.hpp"

#include <cstdint>

namespace obf {
    void serverParsePacket(Packet& packet, Player* player);

    void sendToPlayer(Player* player, Packet& packet);

    // send to all connected players
    void broadcastPacket(Packet& packet);

    void relayMessage(const std::string_view&);

    void relayVars(Player* player = nullptr);

    // send raw bytes to player - implemented in main.cpp
    void wsSend(uint64_t connId, const uint8_t* data, size_t len);
    void wsDisconnect(uint64_t connId);
}
