#include "assets.hpp"
#include "camera.hpp"
#include "entities.hpp"
#include "events.hpp"
#include "globals.hpp"
#include "math.hpp"
#include "net.hpp"
#include "types.hpp"
#include "ui.hpp"
#include "strings.hpp"

#include <SFML/Graphics.hpp>
#include <SFML/Network.hpp>

#include <SFML/Network/IpAddress.hpp>
#include <cfenv>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <fstream>
#include <future>
#include <iostream>
#include <regex>

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
	for (int i = 1; i < argc; i++) {
		headless |= !strcmp(argv[i], "--headless");
	}

#ifdef __linux__
	// Crash on NaN or OF if we're under GDB
	if (is_debugger_present()) {
		feenableexcept(FE_INVALID | FE_OVERFLOW);
	}
#endif

	authority = headless;
	isServer = headless;
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
	if (headless) {
		if (port == 0) {
			printf("Specify the port you will host on.\n");
			std::cin >> port;
			out << "\nport = " << port << std::endl;
		}
	} else {
		if (name.empty()) {
			printf("Type in an username (we didn't have budget for UI).\n");
			getline(std::cin, name);
			out << "\nname = " << name << std::endl;
		}
	}
	out.close();
	if (headless) {
		connectListener = new sf::TcpListener;
		connectListener->setBlocking(false);
		if (connectListener->listen(port) != sf::Socket::Status::Done) {
			printf("Could not host server on port %u.\n", port);
			return 0;
		}
		printf("Hosted server on port %u.\n", port);
		generateSystem();
	} else {
		window = new sf::RenderWindow(sf::VideoMode({800, 800}), "Orbitfight");
		g_camera.scale = 1;
		g_camera.resize();
		font = new sf::Font;
		if (!font->openFromMemory(assets_font_ttf, assets_font_ttf_len)) [[unlikely]] {
			puts("Failed to load font");
			return 1;
		}
		uiGroup.push_back(new MiscInfoUI());
		uiGroup.push_back(new ChatUI());
		menuUI = new MenuUI();
		uiGroup.push_back(menuUI);
		for (UIElement* e : uiGroup) {
			e->resized();
		}
		systemCenter = new CelestialBody(true);
		if (autoConnect && !serverAddress.empty()) {
			std::vector<std::string> addressPort;
			splitString(serverAddress, addressPort, ':');
			std::string address = addressPort[0];
			if (addressPort.size() == 1) {
				addressPort.push_back(to_string(port));
			}
			if (addressPort.size() == 2) {
				if (std::regex_match(addressPort[1], int_regex)) {
					port = stoi(addressPort[1]);
					printPreferred("Connecting automatically to " + address + ":" + addressPort[1] + ".");
					serverSocket = new sf::TcpSocket;
					if (serverSocket->connect(sf::IpAddress::resolve(address).value(), port) != sf::Socket::Status::Done) [[unlikely]] {
						printPreferred("Could not connect to " + address + ":" + addressPort[1] + ".");
						delete serverSocket;
						serverSocket = nullptr;
					} else {
						printPreferred("Connected to " + address + ":" + addressPort[1] + ".");
						onServerConnection();
					}
				} else {
					printPreferred("Specified server port " + addressPort[1] + " is not an integer.");
				}
			}
		}
	}

	while (headless || window->isOpen()) {
		if(headless && !inputWaiting){
			if(!inputBuffer.empty()){
				parseCommand(inputBuffer);
				inputBuffer.clear();
			}
			inputReader = std::async(std::launch::async, inputListen);
			inputWaiting = true;
		}
		if (isServer) {
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
		}
		if (!headless) {
			if (window->hasFocus()) {
				mousePos = sf::Mouse::getPosition(*window);
				if (!activeTextbox) {
					controls.forward = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::W);
					controls.backward = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::S);
					controls.turnleft = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::A);
					controls.turnright = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::D);
					controls.boost = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LControl);
					controls.primaryfire = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Space);
					controls.secondaryfire = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::X);
				}
			}
			std::optional<sf::Event> eventOpt;
			while ((eventOpt = window->pollEvent()) && eventOpt.has_value()) {
				sf::Event& event = eventOpt.value();

				if (event.is<sf::Event::Closed>()) {
					window->close();
				} else if (event.is<sf::Event::Resized>()) {
					g_camera.resize();
					if (debug) [[unlikely]] {
						printf("Resized view, new size: %g * %g\n", (double)g_camera.w * g_camera.scale, (double)g_camera.h * g_camera.scale);
					}
					if (serverSocket) {
						sf::Packet resize;
						resize << Packets::ResizeView << (double)g_camera.w * g_camera.scale << (double)g_camera.h * g_camera.scale;
						serverSocket->send(resize);
					}
					g_camera.bindUI();
					for (UIElement* e : uiGroup) {
						e->resized();
					}
					g_camera.bindWorld();
				} else if (const auto castEv = event.getIf<sf::Event::MouseWheelScrolled>()) {
					for (size_t i = 0; i < MouseScrolledListener::listeners.size(); i++) {
						MouseScrolledListener::listeners[i]->onMouseScroll(castEv->delta);
					}
					if (!activeTextbox) {
						double factor = 1.0 + 0.1 * ((castEv->delta < 0) * 2 - 1);
						g_camera.zoom(factor);
						if (debug) [[unlikely]] {
							printf("Resized view, new size: %g * %g\n", (double)g_camera.w * g_camera.scale, (double)g_camera.h * g_camera.scale);
						}
						if (serverSocket) {
							sf::Packet resize;
							resize << Packets::ResizeView << (double)g_camera.w * g_camera.scale << (double)g_camera.h * g_camera.scale;
							serverSocket->send(resize);
						}
					}
				} else if (const auto castEv = event.getIf<sf::Event::MouseButtonPressed>()) {
					for (size_t i = 0; i < MousePressListener::listeners.size(); i++) {
						MousePressListener::listeners[i]->onMousePress(castEv->button);
					}
				} else if (const auto castEv = event.getIf<sf::Event::KeyPressed>()) {
					for (size_t i = 0; i < KeyPressListener::listeners.size(); i++) {
						KeyPressListener::listeners[i]->onKeyPress(castEv->code);
					}
					handledTextBoxSelect = false;
					if (!activeTextbox) {
						if (enableControlLock && castEv->code == sf::Keyboard::Key::LAlt) {
							lockControls = !lockControls;
							if (serverSocket) {
								sf::Packet controlsPacket;
								controlsPacket << Packets::Controls << (lockControls ? (unsigned char) 0 : *(unsigned char*) &controls);
								serverSocket->send(controlsPacket);
							}
						} else if (castEv->code == sf::Keyboard::Key::T) {
							if (!ownEntity || ownEntity->type() != Entities::Triangle) {
								continue;
							}
							double minDst = DBL_MAX;
							Entity* closestEntity = nullptr;
							for (Entity* e : updateGroup) {
								if (e == ownEntity) {
									continue;
								}
								double dst = dst2(e->x - ownX - (mousePos.x - g_camera.w * 0.5) * g_camera.scale, e->y - ownY - (mousePos.y - g_camera.h * 0.5) * g_camera.scale) - e->radius * e->radius;
								if (dst < minDst) {
									minDst = dst;
									closestEntity = e;
								}
							}
							bool unset = closestEntity == ((Triangle*)ownEntity)->target;
							((Triangle*)ownEntity)->target = unset ? nullptr : closestEntity;
							if (serverSocket) {
								sf::Packet targetPacket;
								targetPacket << Packets::SetTarget << (unset ? numeric_limits<uint32_t>::max() : closestEntity->id);
								serverSocket->send(targetPacket);
							}
						} else if (castEv->code == sf::Keyboard::Key::LShift && ownEntity) {
							controls.slowrotate = !controls.slowrotate;
						}
					}
					if (castEv->code == sf::Keyboard::Key::Tab) {
						double minDst = DBL_MAX;
						Entity* closestEntity = nullptr;
						for (Entity* e : updateGroup) {
							double dst = dst2(e->x - ownX - (mousePos.x - g_camera.w * 0.5) * g_camera.scale, e->y - ownY - (mousePos.y - g_camera.h * 0.5) * g_camera.scale) - e->radius * e->radius;
							if (dst < minDst) {
								minDst = dst;
								closestEntity = e;
							}
						}
						if (dst2(systemCenter->x - ownX - (mousePos.x - g_camera.w * 0.5) * g_camera.scale, systemCenter->y - ownY - (mousePos.y - g_camera.h * 0.5) * g_camera.scale) < minDst) {
							closestEntity = systemCenter;
						}
						if (closestEntity == trajectoryRef) {
							trajectoryRef = nullptr;
							lastTrajectoryRef = nullptr;
						} else {
							trajectoryRef = closestEntity;
							printf("Selected entity id %u as reference body\n", trajectoryRef->id);
						}
					}
				} else if (const auto castEv = event.getIf<sf::Event::TextEntered>()) {
					for (size_t i = 0; i < TextEnteredListener::listeners.size(); i++) {
						TextEnteredListener::listeners[i]->onTextEntered(castEv->unicode);
					}
				}
			}
			window->clear(sf::Color(worldBrightness / 2, 0, worldBrightness));
			if (ownEntity) [[likely]] {
				ownX = ownEntity->x;
				ownY = ownEntity->y;
				drawShiftX = -ownX, drawShiftY = -ownY;
			}
			g_camera.bindWorld();
			g_camera.pos.x = 0;
			g_camera.pos.y = 0;
			trajectoryOffset = floor((globalTime - lastPredict) / predictDelta);
			for (size_t i = 0; i < ghostTrajectories.size(); i++) {
				drawTrajectory(ghostTrajectoryColors[i], ghostTrajectories[i]);
			}
			double x = 0.0, y = 0.0, tmass = 0.0;
			for (Entity* e : updateGroup) {
				x += e->x * e->mass;
				y += e->y * e->mass;
				tmass += e->mass;
			}
			if (updateGroup.size() != 0 && tmass != 0.0) {
				x /= updateGroup.size() * tmass;
				y /= updateGroup.size() * tmass;
				systemCenter->setPosition(x, y);
			}
			for (size_t i = 0; i < updateGroup.size(); i++) {
				updateGroup[i]->draw();
			}
			if (ownEntity) {
				if (lockControls) {
					unsigned char zero = 0;
					ownEntity->control(*(movement*)&zero);
				} else {
					ownEntity->control(controls);
				}
			}
			g_camera.bindUI();
			if (lastTrajectoryRef) {
				float radius = std::max(5.f, (float)(lastTrajectoryRef->radius / g_camera.scale));
				sf::CircleShape selection(radius, 4);
				selection.setOrigin({(float)radius, (float)radius});
				selection.setPosition({(float)(g_camera.w * 0.5 + (lastTrajectoryRef->x - ownX) / g_camera.scale), (float)(g_camera.h * 0.5 + (lastTrajectoryRef->y - ownY) / g_camera.scale)});
				selection.setFillColor(sf::Color(0, 0, 0, 0));
				selection.setOutlineColor(sf::Color(255, 255, 64));
				selection.setOutlineThickness(1.f);
				window->draw(selection);
			}
			if (ownEntity && ((Triangle*)ownEntity)->target != nullptr) {
				Entity* target = ((Triangle*)ownEntity)->target;
				float radius = std::max(5.f, (float)(target->radius / g_camera.scale));
				sf::CircleShape selection(radius, 3);
				selection.setOrigin({(float)radius, (float)radius});
				selection.setPosition({(float)(g_camera.w * 0.5 + (target->x - ownX) / g_camera.scale), (float)(g_camera.h * 0.5 + (target->y - ownY) / g_camera.scale)});
				selection.setFillColor(sf::Color(0, 0, 0, 0));
				selection.setOutlineColor(sf::Color(255, 0, 0));
				selection.setOutlineThickness(1.f);
				window->draw(selection);
			}
			if (debug && quadtree[0].used) [[unlikely]] {
				quadtree[0].draw();
			}
			for (UIElement* e : uiGroup) {
				if (e->active) {
					e->update();
				}
			}
			g_camera.bindWorld();
			window->display();

			if (serverSocket) {
				sf::Socket::Status status = sf::Socket::Status::Done;
				while (status != sf::Socket::Status::NotReady) {
					sf::Packet packet;
					serverSocket->setBlocking(false);
					status = serverSocket->receive(packet);
					serverSocket->setBlocking(true);
					if (status == sf::Socket::Status::Done) {
						clientParsePacket(packet);
					} else if (status == sf::Socket::Status::Disconnected) {
						printPreferred("Connection to server closed. Continuing simulation locally.");
						setAuthority(true);
						delete serverSocket;
						serverSocket = nullptr;
						break;
					}
				}
				if (ownEntity && lastControls != controls && !lockControls && serverSocket) {
					sf::Packet controlsPacket;
					controlsPacket << Packets::Controls << *(unsigned char*) &controls;
					serverSocket->send(controlsPacket);
					*(unsigned char*) &lastControls = *(unsigned char*) &controls;
				}
			}
		}

		buildQuadtree();
		for (Entity* e : updateGroup) {
			e->update1();
		}
		updateEntities();

		if (authority && lastSweep + projectileSweepSpacing < globalTime) {
			for (Entity* e : updateGroup) {
				if (e->type() != Entities::Projectile && e->type() != Entities::Missile) {
					continue;
				}
				double closest = DBL_MAX;
				if (isServer) {
					for (Player* p : playerGroup) {
						if (p->entity)
							closest = std::min(closest, dst2(e->x - p->entity->x, e->y - p->entity->y));
					}
				}
				if (ownEntity) {
					closest = std::min(closest, dst2(e->x - ownEntity->x, e->y - ownEntity->y));
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
			if (isServer) {
				for (Player* p : playerGroup) {
					sf::Packet despawnPacket;
					despawnPacket << Packets::DeleteEntity << d->id;
					p->tcpSocket.send(despawnPacket);
				}
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
		if (!headless && globalTime - lastPredict > predictSpacing && trajectoryRef) [[unlikely]] {
			double resdelta = delta;
			double resTime = globalTime;
			bool resAuthority = authority;
			bool resIsServer = isServer;
			movement resControls = controls;
			authority = true;
			isServer = false;
			std::vector<Entity*> retUpdateGroup(updateGroup);
			delta = predictDelta;
			simulating = true;
			ghostTrajectories.clear();
			ghostTrajectoryColors.clear();
			bool controlsActive = *(unsigned char*) &controls != 0;
			Triangle* ghost = nullptr;
			if (ownEntity && controlsActive) {
				ghost = new Triangle();
				ghost->x = ownEntity->x;
				ghost->y = ownEntity->y;
				ghost->velX = ownEntity->velX;
				ghost->velY = ownEntity->velY;
				ghost->parent_id = ownEntity->id;
				std::copy(std::begin(ownEntity->color), std::end(ownEntity->color), std::begin(ghost->color));
				simCleanupBuffer.push_back(ghost);
			}
			for (Entity* e : updateGroup) {
				e->simSetup();
				e->trajectory.clear();
			}
			for (int i = 0; i < predictSteps; i++) {
				predictingFor = predictDelta * predictSteps;
				globalTime += predictDelta;
				buildQuadtree();
				for (Entity* e : updateGroup) {
					e->update1();
				}
				updateEntities();
				double x = 0.0, y = 0.0, tmass = 0.0;
				for (Entity* e : updateGroup) {
					x += e->x * e->mass;
					y += e->y * e->mass;
					tmass += e->mass;
				}
				x /= updateGroup.size() * tmass;
				y /= updateGroup.size() * tmass;
				systemCenter->setPosition(x, y);
				for (Entity* e : updateGroup) {
					e->trajectory.push_back({e->x - trajectoryRef->x, e->y - trajectoryRef->y});
				}
				if (ownEntity) {
					ownEntity->control(controls);
				}
				for (size_t i = 0; i < updateGroup.size(); i++) {
					if (!updateGroup[i]->active) [[unlikely]] {
						updateGroup[i]->active = true;
						updateGroup.erase(updateGroup.begin() + i);
						i--;
					}
				}
			}
			for (Entity* en : simCleanupBuffer) {
				ghostTrajectories.push_back(en->trajectory);
				ghostTrajectoryColors.push_back(sf::Color(en->color[0] * 0.7, en->color[1] * 0.7, en->color[2] * 0.7));
				en->active = false;
			}
			simCleanupBuffer.clear();
			updateGroup = retUpdateGroup;
			for (Entity* e : updateGroup) {
				e->simReset();
			}
			delta = resdelta;
			simulating = false;
			authority = resAuthority;
			isServer = resIsServer;
			controls = resControls;
			globalTime = resTime;
			lastPredict = globalTime;
			lastTrajectoryRef = trajectoryRef;
		}
		if (isServer) {
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
