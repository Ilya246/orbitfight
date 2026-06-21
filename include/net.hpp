#pragma once
#include "entities.hpp"
#include "packet.hpp"
#include "websocket.hpp"

#include <cstdint>

namespace obf {
    void serverParsePacket(Packet& packet, Player* player);

    void sendToPlayer(Player* player, Packet& packet);

    // send to all connected players
    void broadcastPacket(Packet& packet);

    void relayMessage(const std::string_view&);

    void relayVars(Player* player = nullptr);
}
