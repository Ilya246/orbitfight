#include "camera.hpp"
#include "entities.hpp"
#include "globals.hpp"
#include "net.hpp"
#include "types.hpp"

#include <SFML/Network.hpp>

#include <iostream>

using namespace obf;

namespace obf {

void clientParsePacket(sf::Packet& packet) {
    uint16_t type;
    packet >> type;
    if(debug && type != Packets::SyncEntity){
        printf("Got packet %d, size %llu\n", type, packet.getDataSize());
    }
    switch (type) {
    case Packets::Ping: {
        sf::Packet ackPacket;
        ackPacket << Packets::Ping;
        serverSocket->send(ackPacket);
        break;
    }
    case Packets::CreateEntity: {
        uint8_t entityType;
        packet >> entityType;
        if(debug){
            printf("Received entity of type %u\n", entityType);
        }
        switch (entityType) {
        case Entities::Triangle: {
            Triangle* e = new Triangle;
            e->unloadCreatePacket(packet);
            break;
        }
        case Entities::Attractor: {
            double radius;
            packet >> radius;
            if (debug) {
                printf(", radius %g", radius);
            }
            Attractor* e = new Attractor(radius);
            e->unloadCreatePacket(packet);
            if (e->star) {
                stars.push_back(e);
            }
            break;
        }
        case Entities::Projectile: {
            Projectile* e = new Projectile;
            e->unloadCreatePacket(packet);
            break;
        }
        default:
            printf("Received entity of unknown entity type %d\n", entityType);
            break;
        }
        break;
    }
    case Packets::SyncEntity: {
        uint32_t entityID;
        packet >> entityID;
        for (Entity* e : updateGroup) {
            if (e->id == entityID) [[unlikely]] {
                e->unloadSyncPacket(packet);
                break;
            }
        }
        break;
    }
    case Packets::AssignEntity: {
        uint32_t entityID;
        packet >> entityID;
        for (Entity* e : updateGroup) {
            if (e->id == entityID) [[unlikely]] {
                ownEntity = e;
                break;
            }
        }
        break;
    }
    case Packets::DeleteEntity: {
        uint32_t deleteID;
        packet >> deleteID;
        for (Entity* e : updateGroup) {
            if (e->id == deleteID) [[unlikely]] {
                delete e;
                break;
            }
        }
        break;
    }
    case Packets::ColorEntity: {
        uint32_t id;
        packet >> id;
        for (Entity* e : updateGroup) {
            if (e->id == id) [[unlikely]] {
                packet >> e->color[0] >> e->color[1] >> e->color[2];
                break;
            }
        }
        break;
    }
    case Packets::Chat: {
        std::string message;
        packet >> message;
        short to = displayMessageCount - 1;
        for (short i = 0; i < to; i++) {
            displayMessages[i] = displayMessages[i + 1];
        }
        displayMessages[to] = sf::String(message);
        break;
    }
    case Packets::PingInfo:
        packet >> lastPing;
        break;
    case Packets::Name: {
        uint32_t id;
        packet >> id;
        for (Entity* e : updateGroup) {
            if (e->id == id) [[unlikely]] {
                packet >> (*(Triangle**)&e)->name;
                break;
            }
        }
        break;
    }
    case Packets::PlanetCollision: {
        uint32_t id;
        packet >> id;
        for (Entity* e : updateGroup) {
            if (e->id == id) [[unlikely]] {
                Attractor* at = (Attractor*)e;
                packet >> at->mass >> at->radius;
                at->shape->setRadius(at->radius);
                at->shape->setOrigin(at->radius, at->radius);
                break;
            }
        }
        break;
    }
    default:
        printf("Unknown packet %d received\n", type);
        break;
    }
}

void serverParsePacket(sf::Packet& packet, Player* player) {
    uint16_t type;
    packet >> type;
    if (debug) {
        printf("Got packet %d from %s, size %llu\n", type, player->name().c_str(), packet.getDataSize());
    }
    switch(type) {
    case Packets::Ping: {
        player->ping = globalTime - player->lastPingSent;
        sf::Packet pingInfoPacket;
        pingInfoPacket << Packets::PingInfo << player->ping;
        player->tcpSocket.send(pingInfoPacket);
        break;
    }
    case Packets::Nickname: {
        packet >> player->username;
        if (player->username.empty() || (int)player->username.size() > usernameLimit) {
            player->username = "impostor";
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
        if ((int)message.size() <= messageLimit && message.size() > 0) {
            sf::Packet chatPacket;
            std::string sendMessage = "";
            sendMessage.append("[").append(player->name()).append("]: ").append(message.substr(1)); // i probably need to implement an alphanumerical regex here
            relayMessage(sendMessage);
        }
        break;
    }
    case Packets::ResizeView:
        packet >> player->viewW >> player->viewH;
        break;
    default:
        printf("Illegal packet %d\n", type);
        break;
    }
}

void relayMessage(std::string& message) {
    sf::Packet chatPacket;
    std::cout << message << std::endl;
    chatPacket << Packets::Chat << message;
    for (Player* p : playerGroup) {
        p->tcpSocket.send(chatPacket);
    }
}

}
