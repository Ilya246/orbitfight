#pragma once
#include "entities.hpp"

#include <SFML/Network.hpp>

namespace obf {
    void onServerConnection();

    void clientParsePacket(sf::Packet&);
    void serverParsePacket(sf::Packet&, Player*);

    void relayMessage(std::string&);
}
