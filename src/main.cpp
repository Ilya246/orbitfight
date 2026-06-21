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

int main(int, char**) {
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

    if (!wsStart(port, tlsCert, tlsKey)) {
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

    wsStop();
    return 0;
}
