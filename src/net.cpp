#include "camera.hpp"
#include "entities.hpp"
#include "globals.hpp"
#include "net.hpp"
#include "strings.hpp"
#include "types.hpp"

#include <SFML/Network.hpp>

#include <iostream>

using namespace obf;

namespace obf {

void onServerConnection() {
    authority = false;
    isServer = false;
    delete connectListener;
    connectListener = nullptr;
    sf::Packet nicknamePacket;
    nicknamePacket << Packets::Nickname << name;
    serverSocket->send(nicknamePacket);
    sf::Packet resize;
    resize << Packets::ResizeView << (double)g_camera.w * g_camera.scale << (double)g_camera.h * g_camera.scale;
    serverSocket->send(resize);
}

void setAuthority(bool to) {
    authority = to;
    if (menuUI && menuUI->state == MenuStates::Main) {
        menuUI->setState(MenuStates::Main);
    }
}

void clientParsePacket(sf::Packet& packet) {
    uint16_t type;
    packet >> type;
    if (debug && type != Packets::SyncEntity) [[unlikely]] {
        printf("Got packet %d, size %llu\n", type, packet.getDataSize());
    }
    switch (type) {
    case Packets::Ping: {
        if (serverSocket) {
            sf::Packet ackPacket;
            ackPacket << Packets::Ping;
            serverSocket->send(ackPacket);
        }
        break;
    }
    case Packets::CreateEntity: {
        uint8_t entityType;
        packet >> entityType;
        if (debug) [[unlikely]] {
            printf("Received entity of type %u\n", entityType);
        }
        switch (entityType) {
        case Entities::Triangle: {
            Triangle* e = new Triangle;
            e->unloadCreatePacket(packet);
            break;
        }
        case Entities::CelestialBody: {
            double radius;
            packet >> radius;
            if (debug) {
                printf(", radius %g", radius);
            }
            CelestialBody* e = new CelestialBody(radius);
            e->unloadCreatePacket(packet);
            break;
        }
        case Entities::Missile: {
            Missile* e = new Missile;
            e->unloadCreatePacket(packet);
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
        Entity* entity = idLookup(entityID);
        if (entity) [[likely]] {
            entity->unloadSyncPacket(packet);
            entity->synced = true;
        } else {
            printf("Server has referred to invalid entity %u in packet of type %u.\n", entityID, type);
        }
        break;
    }
    case Packets::SyncDone: {
        for (Entity* e: updateGroup) {
            if (!e->synced) {
                continue;
            }
            e->x = e->syncX;
            e->y = e->syncY;
            e->velX = e->syncVelX;
            e->velY = e->syncVelY;
            e->synced = false;
        }
        break;
    }
    case Packets::AssignEntity: {
        uint32_t entityID;
        packet >> entityID;
        ownEntity = idLookup(entityID);
        break;
    }
    case Packets::DeleteEntity: {
        uint32_t entityID;
        packet >> entityID;
        Entity* e = idLookup(entityID);
        if (e) {
            e->active = false;
        }
        break;
    }
    case Packets::ColorEntity: {
        uint32_t entityID;
        packet >> entityID;
        Entity* e = idLookup(entityID);
        packet >> e->color[0] >> e->color[1] >> e->color[2];
        break;
    }
    case Packets::Chat: {
        std::string message;
        packet >> message;
        displayMessage(message);
        break;
    }
    case Packets::PingInfo:
        packet >> lastPing;
        break;
    case Packets::Name: {
        uint32_t entityID;
        packet >> entityID;
        Entity* e = idLookup(entityID);
        if (e) [[likely]] {
            packet >> ((Triangle*)e)->name;
        } else {
            printf("Server has referred to invalid entity %u in packet of type %u.\n", entityID, type);
        }
        break;
    }
    case Packets::PlanetCollision: {
        uint32_t entityID;
        packet >> entityID;
        CelestialBody* e = (CelestialBody*)idLookup(entityID);
        if (e) [[likely]] {
            packet >> e->mass;
            e->postMassUpdate();
        } else {
            printf("Server has referred to invalid entity %u in packet of type %u.\n", entityID, type);
        }
        break;
    }
    case Packets::FullClear: {
        fullClear(false);
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
        if (player->entity) {
            ((Triangle*)player->entity)->name = player->username;
            player->entity->setColor(color[0], color[1], color[2]);
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

void relayMessage(std::string& message) {
    sf::Packet chatPacket;
    printPreferred(message);
    chatPacket << Packets::Chat << message;
    for (Player* p : playerGroup) {
        p->tcpSocket.send(chatPacket);
    }
}

}
