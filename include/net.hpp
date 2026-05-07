#pragma once
#include "entities.hpp"

#include <SFML/Network.hpp>

namespace obf {
    void onServerConnection();

    void setAuthority(bool);

    void clientParsePacket(sf::Packet&);
    void serverParsePacket(sf::Packet&, Player*);

    void relayMessage(const std::string_view&);

    void relayVars(Player* player = nullptr);
}
