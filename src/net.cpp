#include "entities.hpp"
#include "globals.hpp"
#include "net.hpp"
#include "strings.hpp"
#include "types.hpp"

#include <SFML/Network.hpp>


using namespace obf;

namespace obf {

void serverParsePacket(sf::Packet& packet, Player* player) {
    uint16_t type;
    packet >> type;
    if (debug) {
        printf("Got packet %d from %s, size %llu\n", type, player->name().c_str(), packet.getDataSize());
    }
    switch(type) {
    case Packets::Ping: {
        player->ping = globalTime - player->lastPingSent;
        player->lastPingReceived = globalTime;
        sf::Packet pingInfoPacket;
        pingInfoPacket << Packets::PingInfo << player->ping;
        player->tcpSocket.send(pingInfoPacket);
        break;
    }
    case Packets::Nickname: {
        if (player->connected) break;

        player->connected = true;

        printPreferred(player->ip + ":" + to_string(player->port) + " has connected.");
        player->entity = new Triangle();
        setupShip(player->entity, false);
        player->entity->player = player;
        player->entity->syncCreation();
        sf::Packet entityAssign;
        entityAssign << Packets::AssignEntity << player->entity->id;
        player->tcpSocket.send(entityAssign);

        packet >> player->username;
        stripSpecialChars(player->username);
        if (player->username.empty() || player->username.size() > usernameLimit) {
            player->username = "unnamed";
        }

        std::hash<std::string> hasher;
        size_t hash = hasher(player->username);
        unsigned char color[3] = {
            (unsigned char) hash,
            (unsigned char) (hash >> 8),
            (unsigned char) (hash >> 16)
        };

        sf::Packet colorPacket;
        colorPacket << Packets::ColorEntity << player->entity->id << color[0] << color[1] << color[2];
        sf::Packet namePacket;
        namePacket << Packets::Name << player->entity->id << player->username;
        for (Player* p : playerGroup) {
            p->tcpSocket.send(colorPacket);
            p->tcpSocket.send(namePacket);
        }
        ((Triangle*)player->entity)->name = player->username;
        player->entity->setColor(color[0], color[1], color[2]);
        std::string sendMessage;
        sendMessage.append("<").append(player->name()).append("> has joined.");
        relayMessage(sendMessage);
        break;
    }
    case Packets::Controls:
        packet >> *(unsigned char*) &(player->controls);
        break;
    case Packets::Chat: {
        std::string message;
        packet >> message;
        if (message.size() <= messageLimit && message.size() > 0) {
            stripSpecialChars(message);
            sf::Packet chatPacket;
            std::string sendMessage = "";
            sendMessage.append("[").append(player->name()).append("]: ").append(message); // i probably need to implement an alphanumeric regex here
            relayMessage(sendMessage);
        }
        break;
    }
    case Packets::ResizeView:
        packet >> player->viewW >> player->viewH;
        break;
    case Packets::SetTarget: {
        uint32_t entityID;
        packet >> entityID;
        if (!player->entity) break;

        if (entityID == numeric_limits<uint32_t>::max()) {
            ((Triangle*)player->entity)->target = nullptr;
            break;
        }
        ((Triangle*)player->entity)->target = idLookup(entityID);
        break;
    }
    default:
        printf("Illegal packet %d\n", type);
        break;
    }
}

void relayMessage(const std::string_view& message) {
    sf::Packet chatPacket;
    printPreferred(message);
    chatPacket << Packets::Chat << (std::string)message;
    for (Player* p : playerGroup) {
        p->tcpSocket.send(chatPacket);
    }
}

void relayVars(Player* player) {
    sf::Packet packet;
    packet << Packets::VarChange;
    for (const auto& [name, var] : vars) {
        if (!var.synced) continue;

        packet << name;
        switch (var.type) {
            // TODO make this not terrible
            case String: {
                packet << *(string*)var.value;
                break;
            }
            case Double: {
                packet << *(double*)var.value;
                break;
            }
            case Int8: {
                packet << *(int8_t*)var.value;
                break;
            }
            case Int32: {
                packet << *(int32_t*)var.value;
                break;
            }
            case Bool: {
                packet << *(bool*)var.value;
                break;
            }
        }
    }

    if (!player) {
        for (Player* p : playerGroup) {
            p->tcpSocket.send(packet);
        }
    } else {
        player->tcpSocket.send(packet);
    }
}

}
