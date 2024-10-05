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

#include <cfloat>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <regex>

using namespace obf;

void inputListen() {
	do {
		std::string buffer;
		getline(std::cin, buffer);
		inputBuffer.append(buffer);
	} while (std::cin.eof() || std::cin.fail());
	inputWaiting = false;
}

int main(int argc, char** argv) {
	bool regenConfig = false;
	for (int i = 1; i < argc; i++) {
		headless |= !strcmp(argv[i], "--headless");
		regenConfig |= !strcmp(argv[i], "--regenerate-help");
	}
	authority = headless;
	isServer = headless;
	bool configNotPresent = parseTomlFile(configFile) != 0;
	if (configNotPresent || regenConfig) {
		if (configNotPresent) printf("No config file detected, creating config %s and documentation file %s.\n", configFile.c_str(), configDocFile.c_str());
		std::ofstream out;
		out.open(configDocFile);
		out << "NOTE: configs changed using the console will not be saved" << std::endl;
		out << "predictSteps: As a client, how many steps of [predictDelta] ticks to simulate for trajectory prediction (int)" << std::endl;
		out << "port: Used both as the port to host on and to specify port for autoConnect if server address does not contain port (short uint)" << std::endl;
		out << "predictDelta: As a client, how many ticks to advance every prediction simulation step (double)" << std::endl;
		out << "predictSpacing: As a client, how many seconds to wait between trajectory prediction simulations (double)" << std::endl;
		out << "NOTE: any clients will have to have the same physics-related configs as the server for them to work properly" << std::endl;
		out << "friction: Friction of touching bodies (double)" << std::endl;
		out << "collideRestitution: How bouncy collisions are (double)" << std::endl;
		out << "gravityStrength: How strong gravity is (double)" << std::endl;
		out << "syncSpacing: As a server, how often should clients be synced (double)" << std::endl;
		out << "gen_blackholeChance: As a server, what fraction of stars should instead be black holes (double)" << std::endl;
		out << "gen_extraStarChance: As a server, the chance for an additional star to generate after the previous (double)" << std::endl;
		out << "autorestartSpacing: As a server, if autorestart is enabled, how many seconds to wait between autorestarts (double)" << std::endl;
		out << "autorestartNotifSpacing: As a server, if autorestart is enabled, how many seconds to wait between chat autorestart notifications (double)" << std::endl;
		out << "serverAddress: Used with autoConnect as the address to connect to (string)" << std::endl;
		out << "name: Your ingame name as a client (string)" << std::endl;
		out << "autorestart: As a server, whether to periodically regenerate the solar system (bool)" << std::endl;
		out << "autoConnect: As a client, whether to automatically connect to a server (bool)" << std::endl;
		out << "enableControlLock: As a client, whether to enable using LAlt to lock controls (bool)" << std::endl;
		out << "DEBUG: Whether to enable debug mode, prints extra info to console (bool)" << std::endl;
		if(regenConfig) {
			return 0;
		}
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
			printf("Specify a username.\n");
			getline(std::cin, name);
			out << "\nname = " << name << std::endl;
		}
	}
	out.close();
	if (headless) {
		connectListener = new sf::TcpListener;
		connectListener->setBlocking(false);
		if (connectListener->listen(port) != sf::Socket::Done) {
			printf("Could not host server on port %u.\n", port);
			return 0;
		}
		printf("Hosted server on port %u.\n", port);
		generateSystem();
	} else {
		window = new sf::RenderWindow(sf::VideoMode(800, 800), "Orbitfight");
		g_camera.scale = 1;
		g_camera.resize();
		font = new sf::Font;
		if (!font->loadFromMemory(assets_font_ttf, assets_font_ttf_len)) [[unlikely]] {
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
					if (serverSocket->connect(address, port) != sf::Socket::Done) [[unlikely]] {
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
			if (status == sf::Socket::Done) {
				sparePlayer->ip = sparePlayer->tcpSocket.getRemoteAddress().toString();
				sparePlayer->port = sparePlayer->tcpSocket.getRemotePort();
				printPreferred(sparePlayer->ip + ":" + to_string(sparePlayer->port) + " has connected.");
				sparePlayer->lastAck = globalTime;
				for (Entity* e : updateGroup) {
					sf::Packet packet;
					packet << Packets::CreateEntity;
					e->loadCreatePacket(packet);
					sparePlayer->tcpSocket.send(packet);
				}
				playerGroup.push_back(sparePlayer);
				sparePlayer->entity = new Triangle();
				setupShip(sparePlayer->entity, false);
				sparePlayer->entity->player = sparePlayer;
				sparePlayer->entity->syncCreation();
				sf::Packet entityAssign;
				entityAssign << Packets::AssignEntity << sparePlayer->entity->id;
				sparePlayer->tcpSocket.send(entityAssign);
				sparePlayer = new Player;
			} else if (status != sf::Socket::NotReady) {
				printPreferred("An incoming connection has failed.");
			}
		}
		if (!headless) {
			if (window->hasFocus()) {
				mousePos = sf::Mouse::getPosition(*window);
				if (!activeTextbox) {
					controls.forward = sf::Keyboard::isKeyPressed(sf::Keyboard::W);
					controls.backward = sf::Keyboard::isKeyPressed(sf::Keyboard::S);
					controls.turnleft = sf::Keyboard::isKeyPressed(sf::Keyboard::A);
					controls.turnright = sf::Keyboard::isKeyPressed(sf::Keyboard::D);
					controls.boost = sf::Keyboard::isKeyPressed(sf::Keyboard::LControl);
					controls.primaryfire = sf::Keyboard::isKeyPressed(sf::Keyboard::Space);
					controls.secondaryfire = sf::Keyboard::isKeyPressed(sf::Keyboard::X);
				}
			}
			sf::Event event;
			while (window->pollEvent(event)) {
				switch(event.type) {
				case sf::Event::Closed:
					window->close();
					break;
				case sf::Event::Resized: {
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
					break;
				}
				case sf::Event::MouseWheelScrolled: {
					for (size_t i = 0; i < MouseScrolledListener::listeners.size(); i++) {
						MouseScrolledListener::listeners[i]->onMouseScroll(event.mouseWheelScroll.delta);
					}
					if (!activeTextbox) {
						double factor = 1.0 + 0.1 * ((event.mouseWheelScroll.delta < 0) * 2 - 1);
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
					break;
				}
				case sf::Event::MouseButtonPressed: {
					for (size_t i = 0; i < MousePressListener::listeners.size(); i++) {
						MousePressListener::listeners[i]->onMousePress(event.mouseButton.button);
					}
					break;
				}
				case sf::Event::KeyPressed: {
					for (size_t i = 0; i < KeyPressListener::listeners.size(); i++) {
						KeyPressListener::listeners[i]->onKeyPress(event.key.code);
					}
					handledTextBoxSelect = false;
					if (!activeTextbox){
						if (enableControlLock && event.key.code == sf::Keyboard::LAlt) {
							lockControls = !lockControls;
							if (serverSocket) {
								sf::Packet controlsPacket;
								controlsPacket << Packets::Controls << (lockControls ? (unsigned char) 0 : *(unsigned char*) &controls);
								serverSocket->send(controlsPacket);
							}
						} else if (event.key.code == sf::Keyboard::T) {
							if (!ownEntity || ownEntity->type() != Entities::Triangle) {
								break;
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
						} else if (event.key.code == sf::Keyboard::LShift && ownEntity) {
							controls.slowrotate = !controls.slowrotate;
						}
					}
					if (event.key.code == sf::Keyboard::Tab) {
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
					break;
				}
				case sf::Event::TextEntered: {
					for (size_t i = 0; i < TextEnteredListener::listeners.size(); i++) {
						TextEnteredListener::listeners[i]->onTextEntered(event.text.unicode);
					}
					break;
				}
				default:
					break;
				}
			}
			window->clear(sf::Color(16, 0, 32));
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
			x /= updateGroup.size() * tmass;
			y /= updateGroup.size() * tmass;
			systemCenter->setPosition(x, y);
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
				selection.setOrigin(radius, radius);
				selection.setPosition(g_camera.w * 0.5 + (lastTrajectoryRef->x - ownX) / g_camera.scale, g_camera.h * 0.5 + (lastTrajectoryRef->y - ownY) / g_camera.scale);
				selection.setFillColor(sf::Color(0, 0, 0, 0));
				selection.setOutlineColor(sf::Color(255, 255, 64));
				selection.setOutlineThickness(1.f);
				window->draw(selection);
			}
			if (ownEntity && ((Triangle*)ownEntity)->target != nullptr) {
				Entity* target = ((Triangle*)ownEntity)->target;
				float radius = std::max(5.f, (float)(target->radius / g_camera.scale));
				sf::CircleShape selection(radius, 3);
				selection.setOrigin(radius, radius);
				selection.setPosition(g_camera.w * 0.5 + (target->x - ownX) / g_camera.scale, g_camera.h * 0.5 + (target->y - ownY) / g_camera.scale);
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
				sf::Socket::Status status = sf::Socket::Done;
				while (status != sf::Socket::NotReady) {
					sf::Packet packet;
					serverSocket->setBlocking(false);
					status = serverSocket->receive(packet);
					serverSocket->setBlocking(true);
					if (status == sf::Socket::Done) {
						clientParsePacket(packet);
					} else if (status == sf::Socket::Disconnected) {
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
						if (!p->entity) {
							continue;
						}
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
				if (globalTime - player->lastAck > 1.0 && globalTime - player->lastPingSent > 1.0) {
					if (globalTime - player->lastAck > maxAckTime) {
						printf("Player %s's connection has timed out.\n", player->name().c_str());
						playerGroup.erase(playerGroup.begin() + i);
						i--;
						to--;
						player->tcpSocket.disconnect();
						delete player;
						continue;
					}

					sf::Packet pingPacket;
					pingPacket << Packets::Ping;
					sf::Socket::Status status = player->tcpSocket.send(pingPacket);
					if (status != sf::Socket::Done) {
						if (status == sf::Socket::Disconnected) {
							printf("Player %s has disconnected.\n", player->name().c_str());
							playerGroup.erase(playerGroup.begin() + i);
							i--;
							to--;
							player->tcpSocket.disconnect();
							delete player;
							continue;
						}

						if (status == sf::Socket::Error) {
							printf("Error when trying to send ping packet to player %s.\n", player->name().c_str());
						}
					}
					player->lastPingSent = globalTime;
				}

				sf::Socket::Status status = sf::Socket::Done;
				while (status != sf::Socket::NotReady && status != sf::Socket::Disconnected) {
					sf::Packet packet;
					player->tcpSocket.setBlocking(false);
					status = player->tcpSocket.receive(packet);
					player->tcpSocket.setBlocking(true);
					if (status == sf::Socket::Done) [[likely]]{
						player->lastAck = globalTime;
						serverParsePacket(packet, player);
					} else if (status == sf::Socket::Disconnected) {
						printf("Player %s has disconnected.\n", player->name().c_str());
						i--;
						to--;
						player->tcpSocket.disconnect();
						delete player;
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
