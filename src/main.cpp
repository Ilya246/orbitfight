#include "entities.hpp"
#include "font.hpp"
#include "globals.hpp"
#include "math.hpp"
#include "toml.hpp"

#include <SFML/Graphics.hpp>
#include <SFML/Network.hpp>

#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>

using namespace obf;

void connectToServer() {
	serverSocket = new sf::TcpSocket;
	while (true) {
		printf("Specify server port\n");
		std::cin >> port;
		printf("Specify server address\n");
		std::cin >> serverAddress;
		if (serverSocket->connect(serverAddress, port) != sf::Socket::Done) {
			printf("Could not connect to %s:%u.\n", serverAddress.c_str(), port);
		} else {
			printf("Connected to %s:%u.\n", serverAddress.c_str(), port);
			sf::Packet nicknamePacket;
			nicknamePacket << (uint16_t)3 << name;
			serverSocket->send(nicknamePacket);
			break;
		}
	}
}

int main(int argc, char** argv) {
	for (int i = 1; i < argc; i++) {
		headless |= !strcmp(argv[i], "--headless");
	}
	bool configNotPresent = parseTomlFile(configFile) != 0;
	if (configNotPresent) {
		printf("No config file detected, creating config %s and documentation file %s.\n", configFile.c_str(), configDocFile.c_str());
		std::ofstream out;
		out.open(configDocFile);
		out << "syncSpacing: As a server, how often should clients be synced (double)" << std::endl;
		out << "autoConnect: As a client, whether to automatically connect to a server (bool)" << std::endl;
		out << "serverAddress: Used with autoConnect as the address to connect to (string)" << std::endl;
		out << "port: Used both as the port to host on and to specify port for autoConnect (short)" << std::endl;
		out << "name: Your ingame name as a client (string)" << std::endl;
	}

	std::ofstream out;
	out.open(configFile, std::ios::app);
	if (headless) {
		if (port == 0) {
			printf("Specify the port you will host on.\n");
			std::cin >> port;
			out << "port = " << port << std::endl;
		}
	} else {
		if (name.empty()) {
			printf("Specify a username.\n");
			std::cin >> name;
			out << "name = " << name << std::endl;
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

		Attractor* a = new Attractor;
		a->setPosition(1000.0, 1000.0);
	} else {
		window = new sf::RenderWindow(sf::VideoMode(500, 500), "Test");
		font = new sf::Font;
		if (!font->loadFromMemory(font_ttf, font_ttf_len)) [[unlikely]] {
			puts("Failed to load font");
			return 1;
		}

		if (autoConnect && !serverAddress.empty() && port != 0) {
			printf("Connecting automatically to %s:%u.\n", serverAddress.c_str(), port);
			serverSocket = new sf::TcpSocket;
			if (serverSocket->connect(serverAddress, port) != sf::Socket::Done) [[unlikely]] {
				printf("Could not connect to %s:%u.\n", serverAddress.c_str(), port);
				connectToServer();
			} else {
				printf("Connected to %s:%u.\n", serverAddress.c_str(), port);
				sf::Packet nicknamePacket;
				nicknamePacket << (uint16_t)3 << name;
				serverSocket->send(nicknamePacket);
			}
		} else {
			connectToServer();
		}
	}

	while (headless || window->isOpen()) {
		if (headless) {
			sf::Socket::Status status = connectListener->accept(sparePlayer->tcpSocket);
			if (status == sf::Socket::Done) {
				sparePlayer->ip = sparePlayer->tcpSocket.getRemoteAddress().toString();
				sparePlayer->port = sparePlayer->tcpSocket.getRemotePort();
				printf("%s has connected.\n", sparePlayer->name().c_str());
				sparePlayer->lastAck = globalTime;
				playerGroup.push_back(sparePlayer);
				for (Entity* e : updateGroup) {
					sf::Packet packet;
					packet << (uint16_t)1;
					e->loadCreatePacket(packet);
					sparePlayer->tcpSocket.send(packet);
				}
				sparePlayer->entity = new Triangle();
				sparePlayer->entity->setPosition(0.0, 0.0);
				sparePlayer->entity->addVelocity(0.8, 0.8);
				sparePlayer->entity->player = sparePlayer;
				sf::Packet entityAssign;
				entityAssign << (uint16_t)5 << sparePlayer->entity->id;
				sparePlayer->tcpSocket.send(entityAssign);
				sparePlayer = new Player;
			} else if (status != sf::Socket::NotReady) {
				printf("An incoming connection has failed.\n");
			}
		} else {
			if (window->hasFocus()) {
				mousePos = sf::Mouse::getPosition(*window);
				controls.forward = sf::Keyboard::isKeyPressed(sf::Keyboard::W);
				controls.backward = sf::Keyboard::isKeyPressed(sf::Keyboard::S);
				controls.turnleft = sf::Keyboard::isKeyPressed(sf::Keyboard::A);
				controls.turnright = sf::Keyboard::isKeyPressed(sf::Keyboard::D);
			}

			sf::Event event;
			while (window->pollEvent(event)) {
				switch(event.type) {
				case sf::Event::Closed:
					window->close();
					break;
				case sf::Event::Resized:
					viewSizeX = event.size.width * zoom;
					viewSizeY = event.size.height * zoom;
					break;
				case sf::Event::MouseWheelScrolled: {
					double factor = 1.0 + 0.1 * ((event.mouseWheelScroll.delta < 0) * 2 - 1);
					zoom *= factor;
					viewSizeX *= factor;
					viewSizeY *= factor;
					break;
				}
				default:
					break;
				}
			}

			if (ownEntity) {
				sf::View view;
				view.setCenter((float)ownEntity->x, (float)ownEntity->y);
				view.setSize(viewSizeX, viewSizeY);
				window->setView(view);
				window->clear(sf::Color(25, 5, std::min(255, (int)(0.1 * sqrt(ownEntity->x * ownEntity->x + ownEntity->y * ownEntity->y)))));
				ownEntity->control(controls);
				sf::Text coords;
				coords.setFont(*font);
				coords.setString(std::to_string((int)ownEntity->x).append(" ").append(std::to_string((int)ownEntity->y)));
				coords.setCharacterSize((int)(26.0 * zoom));
				coords.setPosition(ownEntity->x - viewSizeX / 2, (float)ownEntity->y - viewSizeY / 2);
				coords.setFillColor(sf::Color::Red);
				window->draw(coords);
			}
			for (auto* entity : updateGroup) {
				entity->draw();
			}

			window->display();

			sf::Socket::Status status = sf::Socket::Done;
			while (status != sf::Socket::NotReady) {
				sf::Packet packet;
				serverSocket->setBlocking(false);
				status = serverSocket->receive(packet);

				serverSocket->setBlocking(true);
				if (status == sf::Socket::Done) {
					uint16_t type;
					packet >> type;
					switch (type) {
					case 0:{
						sf::Packet ackPacket;
						ackPacket << (uint16_t)0;
						serverSocket->send(ackPacket);
						break;
					}
					case 1: {
						unsigned char entityType;
						packet >> entityType;
						switch (entityType) {
						case 0: {
							Triangle* e = new Triangle;
							e->unloadCreatePacket(packet);
							break;
						}
						case 1: {
							Attractor* e = new Attractor;
							e->unloadCreatePacket(packet);
							break;
						}
						default:
							printf("Unknown entity type %d\n", entityType);
							break;
						}
						break;
					}
					case 2: {
						int entityID;
						packet >> entityID;
						for (Entity* e : updateGroup) {
							if (e->id == entityID) [[unlikely]] {
								e->unloadSyncPacket(packet);
								break;
							}
						}
						break;
					}
					case 5: {
						int entityID;
						packet >> entityID;
						for (Entity* e : updateGroup) {
							if (e->id == entityID) {
								ownEntity = e;
								break;
							}
						}
						break;
					}
					case 6: {
						int deleteID;
						packet >> deleteID;
						for (Entity* e : updateGroup) {
							if (e->id == deleteID) [[unlikely]] {
								delete e;
								break;
							}
						}
						break;
					}
					default:
						printf("Unknown packet %d\n", type);
						break;
					}
				} else if (status == sf::Socket::Disconnected) {
					printf("Connection to server closed.\n");
					connectToServer();
				}
			}
			if (ownEntity && lastControls != controls) {
				sf::Packet controlsPacket;
				controlsPacket << (uint16_t)4 << *(unsigned char*) &controls;
				serverSocket->send(controlsPacket);
			}
			*(unsigned char*) &lastControls = *(unsigned char*) &controls;
		}

		for (auto* entity : updateGroup) {
			entity->update();
		}
		if (headless) {
			int to = playerGroup.size();
			for (int i = 0; i < to; i++) {
				Player* player = playerGroup.at(i);
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
					pingPacket << (uint16_t)0;
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
						uint16_t type;
						packet >> type;
						switch(type) {
						case 0:
							break;
						case 3:
							packet >> player->username;
							break;
						case 4:
							packet >> *(unsigned char*) &(player->controls);
							break;
						default:
							printf("Illegal packet %d\n", type);
							break;
						}
					} else if (status == sf::Socket::Disconnected) {
						printf("Player %s has disconnected.\n", player->name().c_str());
						playerGroup.erase(playerGroup.begin() + i);
						i--;
						to--;
						player->tcpSocket.disconnect();
						delete player;
						goto egg;
					}
				}

				if (globalTime - player->lastSynced > syncSpacing) {
					for (Entity* e : updateGroup) {
						sf::Packet packet;
						packet << (uint16_t)2;
						e->loadSyncPacket(packet);
						player->tcpSocket.send(packet);
					}

					player->lastSynced = globalTime;
				}

				if (player->entity) {
					player->entity->control(player->controls);
				}

			egg:
				continue;
			}
		}

		delta = deltaClock.restart().asSeconds() * 60;
		sf::sleep(sf::seconds(std::max((1.0 - delta) / 60.0, 0.0)));
		globalTime = globalClock.getElapsedTime().asSeconds();
	}

	return 0;
}
