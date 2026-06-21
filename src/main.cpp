#include "entities.hpp"
#include "events.hpp"
#include "globals.hpp"
#include "math.hpp"
#include "net.hpp"
#include "types.hpp"
#include "strings.hpp"
#include "websocket.hpp"

#include <cfenv>
#include <cfloat>
#include <cstring>
#include <fstream>
#include <future>
#include <iostream>

#ifdef __linux__
#include "fenv.h"
#endif

using namespace obf;

// websocket stuff
static ws::WebSocketServer* wsServer = nullptr;
struct WsEvent {
    enum Type { Connect, Disconnect, Message };
    Type type;
    uint64_t connId;
    std::string ip;
    unsigned short port;
    std::vector<uint8_t> data;
};
static std::vector<WsEvent> wsEvents;
static std::mutex wsEventsMutex;
static std::unordered_map<uint64_t, Player*> connToPlayer;

namespace obf {

void wsSend(uint64_t connId, const uint8_t* data, size_t len) {
    if (wsServer) wsServer->send(connId, data, len);
}

void wsDisconnect(uint64_t connId) {
    if (wsServer) wsServer->disconnect(connId);
}

}

static std::string tlsCertPath;
static std::string tlsKeyPath;

void wsStart(int port) {
    wsServer = new ws::WebSocketServer();
    wsServer->setConnectCallback([](uint64_t connId, const std::string& ip, unsigned short port) {
        std::lock_guard<std::mutex> lock(wsEventsMutex);
        WsEvent ev;
        ev.type = WsEvent::Connect;
        ev.connId = connId;
        ev.ip = ip;
        ev.port = port;
        wsEvents.push_back(ev);
    });
    wsServer->setDisconnectCallback([](uint64_t connId) {
        std::lock_guard<std::mutex> lock(wsEventsMutex);
        WsEvent ev;
        ev.type = WsEvent::Disconnect;
        ev.connId = connId;
        wsEvents.push_back(ev);
    });
    wsServer->setMessageCallback([](uint64_t connId, const uint8_t* data, size_t len) {
        std::lock_guard<std::mutex> lock(wsEventsMutex);
        WsEvent ev;
        ev.type = WsEvent::Message;
        ev.connId = connId;
        ev.data.assign(data, data + len);
        wsEvents.push_back(ev);
    });
    if (!wsServer->start(port, tlsCertPath, tlsKeyPath)) {
        printf("Could not start WebSocket server on port %u.\n", port);
        delete wsServer;
        wsServer = nullptr;
    }
}

void wsStop() {
    if (wsServer) {
        wsServer->stop();
        delete wsServer;
        wsServer = nullptr;
    }
}

void wsPoll() {
    if (!wsServer) return;
    wsServer->poll();

    std::vector<WsEvent> events;
    {
        std::lock_guard<std::mutex> lock(wsEventsMutex);
        events.swap(wsEvents);
    }

    for (auto& ev : events) {
        switch (ev.type) {
        case WsEvent::Connect: {
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
            break;
        }
        case WsEvent::Disconnect: {
            auto it = connToPlayer.find(ev.connId);
            if (it != connToPlayer.end()) {
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
            break;
        }
        case WsEvent::Message: {
            auto it = connToPlayer.find(ev.connId);
            if (it == connToPlayer.end()) break;
            Player* player = it->second;
            if (ev.data.size() < 4) break;
            uint32_t packetLen = ((uint32_t)ev.data[0] << 24) |
                                 ((uint32_t)ev.data[1] << 16) |
                                 ((uint32_t)ev.data[2] << 8) |
                                 (uint32_t)ev.data[3];
            if (ev.data.size() < 4 + packetLen) break;
            Packet packet;
            packet.loadPayload(ev.data.data() + 4, packetLen);
            player->lastAck = globalTime;
            try {
                serverParsePacket(packet, player);
            } catch (const std::exception& e) {
                printf("Error parsing packet from player %s: %s\n", player->name().c_str(), e.what());
            }
            break;
        }
        }
    }
}

void inputListen() {
	do {
		std::string buffer;
		getline(std::cin, buffer);
		inputBuffer.append(buffer);
	} while (std::cin.eof() || std::cin.fail());
	inputWaiting = false;
}

// Detect GDB
bool is_debugger_present() {
#ifdef __linux__
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("TracerPid:", 0) == 0) {
            return line.back() != '0';
        }
    }
    return false;
#else
    return false;
#endif
}

int main(int argc, char** argv) {
#ifdef __linux__
	// Crash on NaN or OF if we're under GDB
	if (is_debugger_present()) {
		feenableexcept(FE_INVALID | FE_OVERFLOW);
	}
#endif

	// Parse command-line args for TLS
	for (int i = 1; i < argc; i++) {
		std::string arg = argv[i];
		if ((arg == "--tls-cert" || arg == "--cert") && i + 1 < argc) {
			tlsCertPath = argv[++i];
		} else if ((arg == "--tls-key" || arg == "--key") && i + 1 < argc) {
			tlsKeyPath = argv[++i];
		} else if (arg == "--help" || arg == "-h") {
			printf("Usage: orbitfight_server [--tls-cert CERT.pem --tls-key KEY.pem]\n\n"
				"Options:\n"
				"  --tls-cert PATH   TLS certificate file (PEM format) for wss://\n"
				"  --tls-key  PATH   TLS private key file (PEM format) for wss://\n"
				"  --help            Show this help\n\n"
				"Without --tls-cert/--tls-key, the server uses plain ws://.\n"
				"With TLS, the server uses wss:// (required when the web client\n"
				"is served over HTTPS — browsers block mixed-content ws://).\n");
			return 0;
		}
	}

	bool configNotPresent = parseTomlFile(configFile) != 0;
	if (configNotPresent) {
		if (configNotPresent) printf("No config file detected, creating config %s and documentation file %s.\n", configFile.c_str(), configDocFile.c_str());
		std::ofstream out;
		out.open(configDocFile);
		out << "TUTORIAL:\n" <<
		"    - W - forwards\n"
		"    - A - rotate left\n"
		"    - S - backwards\n"
		"    - D - rotate right\n"
		"    - X - fire railgun\n"
		"    - LCtrl - boost\n"
		"    - Spacebar - fire\n"
		"    - T - target body closest to cursor\n"
		"    - Tab - set/change/unset reference body to predict trajectories against" << endl;
	}
	std::ofstream out;
	out.open(configFile, std::ios::app);

	if (port == 0) {
		printf("Specify the port you will host on.\n");
		std::cin >> port;
		out << "\nport = " << port << std::endl;
	}

	out.close();

	wsStart(port);
	if (!wsServer) {
		return 1;
	}

	generateSystem();

	while (true) {
		if (!inputWaiting){
			if(!inputBuffer.empty()){
				parseCommand(inputBuffer);
				inputBuffer.clear();
			}
			inputReader = std::async(std::launch::async, inputListen);
			inputWaiting = true;
		}
		if (autorestart) {
			if (playerGroup.size() == 0) {
				delta = 0.0;
				lastAutorestartNotif = -autorestartNotifSpacing;
				lastAutorestart = globalTime;
				if (!autorestartRegenned) {
					fullClear(false);
					generateSystem();
				}
				autorestartRegenned = true;
			} else {
				if (lastAutorestart + autorestartSpacing < globalTime) {
					delta = 0.0;
					fullClear(false);
					generateSystem();
					for (Entity* e : updateGroup) {
						if (e->type() != Entities::Triangle) {
							e->syncCreation();
						}
					}
					for (Player* p : playerGroup) {
						setupShip(p->entity, true);
					}
					std::string sendMessage = "ANNOUNCEMENT: The system has been regenerated.";
					relayMessage(sendMessage);
					lastAutorestartNotif = -autorestartNotifSpacing;
					lastAutorestart = globalTime;
				} else if (lastAutorestartNotif + autorestartNotifSpacing < globalTime) {
					std::string sendMessage = "";
					sendMessage.append("ANNOUNCEMENT: ").append(std::to_string((int)(autorestartSpacing - globalTime + lastAutorestart))).append("s until autorestart.");
					relayMessage(sendMessage);
					lastAutorestartNotif = globalTime;
				}
				autorestartRegenned = false;
			}
		}

		wsPoll();

		buildQuadtree();
		updateEntities();

		if (lastSweep + projectileSweepSpacing < globalTime) {
			for (Entity* e : updateGroup) {
				if (e->type() != Entities::Projectile && e->type() != Entities::Missile) {
					continue;
				}
				double closest = DBL_MAX;

				for (Player* p : playerGroup) {
					if (p->entity)
						closest = std::min(closest, dst2(e->x - p->entity->x, e->y - p->entity->y));
				}

				if (closest > sweepThreshold) {
					e->active = false;
				}
			}
			lastSweep = globalTime;
		}
		std::vector<Entity*> deleted;
		for (size_t i = 0; i < updateGroup.size(); i++) {
			if (!updateGroup[i]->active) [[unlikely]] {
				deleted.push_back(updateGroup[i]);
				updateGroup.erase(updateGroup.begin() + i);
				i--;
			}
		}
		for (size_t i = 0; i < deleted.size(); i++) {
			Entity* d = deleted[i];
			for (size_t i = 0; i < EntityDeleteListener::listeners.size(); i++) {
				EntityDeleteListener::listeners[i]->onEntityDelete(d);
			}

			Packet despawnPacket;
			despawnPacket << Packets::DeleteEntity << d->id;
			broadcastPacket(despawnPacket);

			if (d == lastTrajectoryRef) {
				lastTrajectoryRef = nullptr;
			}
			if (d == trajectoryRef) {
				trajectoryRef = nullptr;
			}
			delete d;
		}
		deleted.clear();
		int to = playerGroup.size();
		for (int i = 0; i < to; i++) {
			Player* player = playerGroup[i];
			if (globalTime - player->lastPingReceived > 1.0 && globalTime - player->lastPingSent > 1.0) {
				if (globalTime - player->lastAck > maxAckTime || globalTime - player->lastPingReceived > maxAckTime) {
					player->disconnectReason = "Timed out";
					wsDisconnect(player->connId);
					// The disconnect will be processed in wsPoll next frame
				} else {
					Packet pingPacket;
					pingPacket << Packets::Ping;
					sendToPlayer(player, pingPacket);
					player->lastPingSent = globalTime;
				}
			}

			if (globalTime - player->lastSynced > syncSpacing) {
				bool fullsync = globalTime - player->lastFullsynced > fullsyncSpacing;
				for (Entity* e : updateGroup) {
					if (player->entity && !fullsync && (std::abs(e->y - player->entity->y) - syncCullOffset > player->viewH * syncCullThreshold || std::abs(e->x - player->entity->x) - syncCullOffset > player->viewW * syncCullThreshold)) {
							continue;
					}
					Packet packet;
					packet << Packets::SyncEntity;
					e->loadSyncPacket(packet);
					sendToPlayer(player, packet);
				}
				Packet syncDone;
				syncDone << Packets::SyncDone;
				sendToPlayer(player, syncDone);
				player->lastSynced = globalTime;
				if (fullsync) {
					player->lastFullsynced = globalTime;
				}
			}

			if (player->entity) {
				player->entity->control(player->controls);
			}
		}

		delta = deltaClock.restart();
		measureFrames++;
		if (globalTime > lastShowFramerate + 1.0) {
			lastShowFramerate = globalTime;
			framerate = measureFrames;
			measureFrames = 0;
		}
		double actualDelta = actualDeltaClock.restart();
		double sleepTime = std::max((1.0 / targetFramerate - actualDelta), 0.0);
		if (sleepTime > 0) {
			std::this_thread::sleep_for(std::chrono::microseconds((long long)(sleepTime * 1000000)));
		}
		actualDeltaClock.restart();
		if (deltaOverride > 0.0) {
			delta = deltaOverride;
		} else {
			delta *= timescale;
		}
		globalTime = globalClock.getElapsedTime();
	}

	return 0;
}
