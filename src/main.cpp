#include "entities.hpp"
#include "events.hpp"
#include "globals.hpp"
#include "math.hpp"
#include "net.hpp"
#include "types.hpp"
#include "strings.hpp"

#include <SFML/Network.hpp>
#include <SFML/Network/IpAddress.hpp>

#include <cfenv>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <fstream>
#include <future>
#include <iostream>

#ifdef __linux__
#include "fenv.h"
#endif

using namespace obf;

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

	connectListener = new sf::TcpListener;
	connectListener->setBlocking(false);
	if (connectListener->listen(port) != sf::Socket::Status::Done) {
		printf("Could not host server on port %u.\n", port);
		return 0;
	}
	printf("Hosted server on port %u.\n", port);
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
		sf::Socket::Status status = connectListener->accept(sparePlayer->tcpSocket);
		if (status == sf::Socket::Status::Done) {
			sparePlayer->ip = sparePlayer->tcpSocket.getRemoteAddress().value().toString();
			sparePlayer->port = sparePlayer->tcpSocket.getRemotePort();
			sparePlayer->lastAck = sparePlayer->lastPingReceived = globalTime;
			playerGroup.push_back(sparePlayer);
			for (Entity* e : updateGroup) {
				sf::Packet packet;
				packet << Packets::CreateEntity;
				e->loadCreatePacket(packet);
				sparePlayer->tcpSocket.send(packet);
			}
			relayVars(sparePlayer);
			sparePlayer = new Player;
		} else if (status != sf::Socket::Status::NotReady) {
			printPreferred("An incoming connection has failed.");
		}

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

			for (Player* p : playerGroup) {
				sf::Packet despawnPacket;
				despawnPacket << Packets::DeleteEntity << d->id;
				p->tcpSocket.send(despawnPacket);
			}

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
			sf::Socket::Status status;
			if (globalTime - player->lastPingReceived > 1.0 && globalTime - player->lastPingSent > 1.0) {
				if (globalTime - player->lastAck > maxAckTime || globalTime - player->lastPingReceived > maxAckTime) {
					player->tcpSocket.disconnect();
					player->disconnectReason = "Timed out";
				} else {
					sf::Packet pingPacket;
					pingPacket << Packets::Ping;
					status = player->tcpSocket.send(pingPacket);
					if (status == sf::Socket::Status::Error) {
						printf("Error when trying to send ping packet to player %s.\n", player->name().c_str());
					}
					player->lastPingSent = globalTime;
				}
			}

			status = sf::Socket::Status::Done;
			while (status == sf::Socket::Status::Done) {
				sf::Packet packet;
				player->tcpSocket.setBlocking(false);
				status = player->tcpSocket.receive(packet);
				player->tcpSocket.setBlocking(true);
				if (status == sf::Socket::Status::Done) {
					player->lastAck = globalTime;
					serverParsePacket(packet, player);
				} else if (status != sf::Socket::Status::NotReady && status != sf::Socket::Status::Partial) {
					string name = player->name();
					string reason = player->disconnectReason;
					if (reason.empty())
						reason = status == sf::Socket::Status::Disconnected ? "Disconnected" : "Errored";
					i--;
					to--;
					player->tcpSocket.disconnect();
					delete player;
					if (player->connected)
						relayMessage(std::format("Player {} has disconnected ({}).\n", name, reason));
					goto egg;
				}
			}

			if (globalTime - player->lastSynced > syncSpacing) {
				bool fullsync = globalTime - player->lastFullsynced > fullsyncSpacing;
				for (Entity* e : updateGroup) {
					if (player->entity && !fullsync && (std::abs(e->y - player->entity->y) - syncCullOffset > player->viewH * syncCullThreshold || std::abs(e->x - player->entity->x) - syncCullOffset > player->viewW * syncCullThreshold)) {
						continue;
					}
					sf::Packet packet;
					packet << Packets::SyncEntity;
					e->loadSyncPacket(packet);
					player->tcpSocket.send(packet);
				}
				sf::Packet syncDone;
				syncDone << Packets::SyncDone;
				player->tcpSocket.send(syncDone);
				player->lastSynced = globalTime;
				if (fullsync) {
					player->lastFullsynced = globalTime;
				}
			}

			if (player->entity) {
				player->entity->control(player->controls);
			}

		egg:
			continue;
		}

		delta = deltaClock.restart().asSeconds();
		measureFrames++;
		if (globalTime > lastShowFramerate + 1.0) {
			lastShowFramerate = globalTime;
			framerate = measureFrames;
			measureFrames = 0;
		}
		double actualDelta = actualDeltaClock.restart().asSeconds();
		sf::sleep(sf::seconds(std::max((1.0 / targetFramerate - actualDelta), 0.0)));
		actualDeltaClock.restart();
		if (deltaOverride > 0.0) {
			delta = deltaOverride;
		} else {
			delta *= timescale;
		}
		globalTime = globalClock.getElapsedTime().asSeconds();
	}

	return 0;
}
