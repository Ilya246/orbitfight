#include "entities.hpp"
#include "globals.hpp"
#include "net.hpp"
#include "packet.hpp"
#include "types.hpp"
#include "websocket.hpp"

#include <ixwebsocket/IXConnectionState.h>
#include <ixwebsocket/IXSocketTLSOptions.h>
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXWebSocketMessage.h>
#include <ixwebsocket/IXWebSocketServer.h>

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace obf {

namespace {

// Single shared server instance. Owned by wsStart/wsStop.
ix::WebSocketServer* wsServer = nullptr;

// Events produced by the IXWebSocket background thread, consumed by wsPoll()
// on the main thread. The library hands us Open/Message/Close callbacks on
// its own thread; we copy out whatever data we need and enqueue it here.
struct WsEvent {
    enum class Type { Connect, Disconnect, Message };
    Type type;
    uint64_t connId = 0;
    std::string ip;
    unsigned short port = 0;
    std::vector<uint8_t> data;
};

std::mutex wsEventsMutex;
std::vector<WsEvent> wsEvents;

// connId -> Player*, populated on Connect, cleared on Disconnect.
std::unordered_map<uint64_t, Player*> connToPlayer;

// connId -> raw WebSocket pointer, populated on Open, erased on Close.
// The library keeps the WebSocket alive in its internal _clients set for as
// long as the connection is open, so this raw pointer is valid while it's in
// the map. findClient() re-validates against getClients() before sending to
// defend against races between the background thread erasing the entry and
// the main thread looking it up.
std::mutex connSocketMutex;
std::unordered_map<uint64_t, ix::WebSocket*> connIdToSocket;

// IXWebSocket assigns each connection a string id that happens to be a
// decimal integer, so we parse it into a uint64_t to keep the rest of the
// codebase (Player::connId, packet headers, etc.) unchanged.
uint64_t connStateIdToInt(const std::string& id) {
    try {
        return std::stoull(id);
    } catch (...) {
        return 0;
    }
}

void enqueueEvent(WsEvent ev) {
    std::lock_guard<std::mutex> lock(wsEventsMutex);
    wsEvents.push_back(std::move(ev));
}

// Handle one drained event. Runs on the main thread, so it can safely touch
// playerGroup / updateGroup / Packet without extra locking.
void dispatchConnect(const WsEvent& ev) {
    Player* player = new Player();
    player->connId = ev.connId;
    player->ip = ev.ip;
    player->port = ev.port;
    player->lastAck = player->lastPingReceived = globalTime;
    connToPlayer[ev.connId] = player;
    playerGroup.push_back(player);

    // Send all existing entities to the new player
    for (Entity* e : updateGroup) {
        Packet packet;
        packet << Packets::CreateEntity;
        e->loadCreatePacket(packet);
        sendToPlayer(player, packet);
    }
    relayVars(player);
}

void dispatchDisconnect(const WsEvent& ev) {
    auto it = connToPlayer.find(ev.connId);
    if (it == connToPlayer.end()) return;

    Player* player = it->second;
    if (player->connected) {
        std::string name = player->name();
        std::string reason = player->disconnectReason;
        if (reason.empty()) reason = "Disconnected";
        relayMessage(std::format("Player {} has disconnected ({}).\n", name, reason));
    }
    connToPlayer.erase(it);
    delete player; // Player destructor removes from playerGroup
}

void dispatchMessage(const WsEvent& ev) {
    auto it = connToPlayer.find(ev.connId);
    if (it == connToPlayer.end()) return;

    Player* player = it->second;
    if (ev.data.size() < 4) return;

    uint32_t packetLen = ((uint32_t)ev.data[0] << 24) |
                         ((uint32_t)ev.data[1] << 16) |
                         ((uint32_t)ev.data[2] << 8) |
                         (uint32_t)ev.data[3];
    if (ev.data.size() < 4 + packetLen) return;

    Packet packet;
    packet.loadPayload(ev.data.data() + 4, packetLen);
    player->lastAck = globalTime;
    try {
        serverParsePacket(packet, player);
    } catch (const std::exception& e) {
        printf("Error parsing packet from player %s: %s\n",
               player->name().c_str(), e.what());
    }
}

void dispatchEvent(const WsEvent& ev) {
    switch (ev.type) {
    case WsEvent::Type::Connect:    dispatchConnect(ev);    break;
    case WsEvent::Type::Disconnect: dispatchDisconnect(ev); break;
    case WsEvent::Type::Message:    dispatchMessage(ev);    break;
    }
}

// Look up a live shared_ptr<WebSocket> for a given connection id, if any.
// We first find the raw pointer in our map, then re-validate it against
// getClients() to make sure the library hasn't disposed of it yet.
std::shared_ptr<ix::WebSocket> findClient(uint64_t connId) {
    if (!wsServer) return nullptr;

    ix::WebSocket* rawPtr = nullptr;
    {
        std::lock_guard<std::mutex> lock(connSocketMutex);
        auto it = connIdToSocket.find(connId);
        if (it == connIdToSocket.end()) return nullptr;
        rawPtr = it->second;
    }

    for (const auto& ws : wsServer->getClients()) {
        if (ws.get() == rawPtr) return ws;
    }
    return nullptr;
}

} // namespace

bool wsStart(int port, const std::string& certPath, const std::string& keyPath) {
    wsServer = new ix::WebSocketServer(port, "0.0.0.0");

    if (!certPath.empty() && !keyPath.empty()) {
        ix::SocketTLSOptions tls;
        tls.tls = true;
        tls.certFile = certPath;
        tls.keyFile = keyPath;
        // We accept whatever cert the client presents (clients are end-users
        // connecting to a known host, not servers we need to authenticate).
        tls.caFile = "NONE";
        wsServer->setTLSOptions(tls);
        printf("TLS enabled (cert: %s, key: %s)\n", certPath.c_str(), keyPath.c_str());
    }

    wsServer->setOnClientMessageCallback(
        [](std::shared_ptr<ix::ConnectionState> state,
           ix::WebSocket& ws,
           const ix::WebSocketMessagePtr& msg) {
            if (!state) return;
            uint64_t connId = connStateIdToInt(state->getId());

            switch (msg->type) {
            case ix::WebSocketMessageType::Open: {
                {
                    std::lock_guard<std::mutex> lock(connSocketMutex);
                    connIdToSocket[connId] = &ws;
                }
                WsEvent ev;
                ev.type = WsEvent::Type::Connect;
                ev.connId = connId;
                ev.ip = state->getRemoteIp();
                ev.port = static_cast<unsigned short>(state->getRemotePort());
                enqueueEvent(std::move(ev));
                break;
            }
            case ix::WebSocketMessageType::Close: {
                {
                    std::lock_guard<std::mutex> lock(connSocketMutex);
                    connIdToSocket.erase(connId);
                }
                WsEvent ev;
                ev.type = WsEvent::Type::Disconnect;
                ev.connId = connId;
                enqueueEvent(std::move(ev));
                break;
            }
            case ix::WebSocketMessageType::Message: {
                WsEvent ev;
                ev.type = WsEvent::Type::Message;
                ev.connId = connId;
                const auto& payload = msg->str;
                ev.data.assign(
                    reinterpret_cast<const uint8_t*>(payload.data()),
                    reinterpret_cast<const uint8_t*>(payload.data()) + payload.size());
                enqueueEvent(std::move(ev));
                break;
            }
            default:
                // Ping/Pong/Fragment/Error are handled by the library itself.
                break;
            }
        });

    if (!wsServer->listenAndStart()) {
        printf("Could not start WebSocket server on port %d.\n", port);
        delete wsServer;
        wsServer = nullptr;
        return false;
    }
    printf("WebSocket server listening on port %d (%s)\n",
           port, (certPath.empty() ? "ws:// plain" : "wss:// TLS"));
    return true;
}

void wsStop() {
    if (wsServer) {
        wsServer->stop();
        delete wsServer;
        wsServer = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(connSocketMutex);
        connIdToSocket.clear();
    }
}

void wsPoll() {
    if (!wsServer) return;

    std::vector<WsEvent> events;
    {
        std::lock_guard<std::mutex> lock(wsEventsMutex);
        events.swap(wsEvents);
    }
    for (const auto& ev : events) {
        dispatchEvent(ev);
    }
}

void wsSend(uint64_t connId, const uint8_t* data, size_t len) {
    auto ws = findClient(connId);
    if (!ws) return;
    // IXWebSocketSendData is a non-owning view; the caller's buffer just has
    // to outlive the call, which it does here.
    ws->sendBinary(ix::IXWebSocketSendData(reinterpret_cast<const char*>(data), len));
}

void wsDisconnect(uint64_t connId) {
    auto ws = findClient(connId);
    if (!ws) return;
    ws->close();
}

} // namespace obf
