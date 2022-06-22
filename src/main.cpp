#include "camera.hpp"
#include "entities.hpp"
#include "font.hpp"
#include "globals.hpp"
#include "math.hpp"
#include "types.hpp"
#include "toml.hpp"

#include <SFML/Graphics.hpp>
#include <SFML/Network.hpp>

#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <future>
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
			nicknamePacket << Packets::Nickname << name;
			serverSocket->send(nicknamePacket);
			break;
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
		out << "DEBUG: whether to enable debug mode, prints extra info to console (bool)" << std::endl;
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
			std::cin >> name;
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

		star = new Attractor(4000.f, 8000.0);
		star->setPosition(0.0, 0.0);
		star->setColor(255, 229, 97);
		int planets = (int)rand_f(3.f, 7.f);
		for (int i = 0; i < planets; i++) {
			double spawnDst = rand_f(30000.f, 300000.f);
			float spawnAngle = rand_f(-PI, PI);
			float radius = rand_f(250.f, 800.f);
			Attractor* planet = new Attractor(radius, radius * radius / 500.f);
			planet->setPosition(star->x + spawnDst * std::cos(spawnAngle), star->y + spawnDst * std::sin(spawnAngle));
			double vel = sqrt(G * star->mass / spawnDst);
			planet->addVelocity(star->velX + vel * std::cos(spawnAngle + PI / 2.0), -star->velY - vel * std::sin(spawnAngle + PI / 2.0));
			planet->setColor((int)rand_f(64.f, 255.f), (int)rand_f(64.f, 255.f), (int)rand_f(64.f, 255.f));
			if (radius >= 600.f) {
				int moons = (int)(rand_f(0.f, 4.f) * radius * radius / 800 / 800);
				for (int it = 0; it < moons; it++) {
					double spawnDst = planet->radius + rand_f(1500.f, 4000.f);
					float spawnAngle = rand_f(-PI, PI);
					float radius = rand_f(20.f, 120.f);
					Attractor* moon = new Attractor(radius, radius * radius / 500.f);
					moon->setPosition(planet->x + spawnDst * std::cos(spawnAngle), planet->y + spawnDst * std::sin(spawnAngle));
					double vel = sqrt(G * planet->mass / spawnDst);
					moon->addVelocity(planet->velX + vel * std::cos(spawnAngle + PI / 2.0), -planet->velY - vel * std::sin(spawnAngle + PI / 2.0));
					moon->setColor((int)rand_f(64.f, 255.f), (int)rand_f(64.f, 255.f), (int)rand_f(64.f, 255.f));
					obf::planets.push_back(moon);
				}
			}
			obf::planets.push_back(planet);
		}
	} else {
		window = new sf::RenderWindow(sf::VideoMode(500, 500), "Test");

		g_camera.scale = 1;
		g_camera.resize();

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
				nicknamePacket << Packets::Nickname << name;
				serverSocket->send(nicknamePacket);
			}
		} else {
			connectToServer();
		}
	}

	while (headless || window->isOpen()) {
		if (headless) {
			if(!inputWaiting){
				if(!inputBuffer.empty()){
					std::cout << parseCommand(inputBuffer) << std::endl;
					inputBuffer.clear();
				}
				inputReader = std::async(std::launch::async, inputListen);
				inputWaiting = true;
			}
			sf::Socket::Status status = connectListener->accept(sparePlayer->tcpSocket);
			if (status == sf::Socket::Done) {
				sparePlayer->ip = sparePlayer->tcpSocket.getRemoteAddress().toString();
				sparePlayer->port = sparePlayer->tcpSocket.getRemotePort();
				printf("%s has connected.\n", sparePlayer->name().c_str());
				sparePlayer->lastAck = globalTime;
				playerGroup.push_back(sparePlayer);
				for (Entity* e : updateGroup) {
					sf::Packet packet;
					packet << Packets::CreateEntity;
					e->loadCreatePacket(packet);
					sparePlayer->tcpSocket.send(packet);
				}

				sparePlayer->entity = new Triangle();
				Attractor* planet = planets[(int)rand_f(0, planets.size() - 1)];
				double spawnDst = planet->radius + rand_f(600.f, 1500.f);
				float spawnAngle = rand_f(-PI, PI);
				sparePlayer->entity->setPosition(planet->x + spawnDst * std::cos(spawnAngle), planet->y + spawnDst * std::sin(spawnAngle));
				double vel = sqrt(G * planet->mass / spawnDst);
				sparePlayer->entity->addVelocity(planet->velX + vel * std::cos(spawnAngle + PI / 2.0), -planet->velY - vel * std::sin(spawnAngle + PI / 2.0));
				sparePlayer->entity->player = sparePlayer;
				sparePlayer->entity->syncCreation();
				sf::Packet entityAssign;
				entityAssign << Packets::AssignEntity << sparePlayer->entity->id;
				sparePlayer->tcpSocket.send(entityAssign);
				sparePlayer = new Player;
			} else if (status != sf::Socket::NotReady) {
				printf("An incoming connection has failed.\n");
			}
		} else {
			if (window->hasFocus() && !chatting) {
				mousePos = sf::Mouse::getPosition(*window);
				controls.forward = sf::Keyboard::isKeyPressed(sf::Keyboard::W);
				controls.backward = sf::Keyboard::isKeyPressed(sf::Keyboard::S);
				controls.turnleft = sf::Keyboard::isKeyPressed(sf::Keyboard::A);
				controls.turnright = sf::Keyboard::isKeyPressed(sf::Keyboard::D);
				controls.boost = sf::Keyboard::isKeyPressed(sf::Keyboard::LShift);
				controls.primaryfire = sf::Keyboard::isKeyPressed(sf::Keyboard::Space);
			}

			sf::Event event;
			while (window->pollEvent(event)) {
				switch(event.type) {
				case sf::Event::Closed:
					window->close();
					break;
				case sf::Event::Resized: {
					g_camera.resize();
					sf::Packet resize;
					resize << Packets::ResizeView << (double)g_camera.w * g_camera.scale << (double)g_camera.h * g_camera.scale;
					serverSocket->send(resize);
					break;
				}
				case sf::Event::MouseWheelScrolled: {
					double factor = 1.0 + 0.1 * ((event.mouseWheelScroll.delta < 0) * 2 - 1);
					g_camera.zoom(factor);
					sf::Packet resize;
					resize << Packets::ResizeView << (double)g_camera.w * g_camera.scale << (double)g_camera.h * g_camera.scale;
					serverSocket->send(resize);
					break;
				}
				case sf::Event::KeyPressed: {
					if (event.key.code == sf::Keyboard::Return) {
						if(chatting && (int)chatBuffer.getSize() > 1){
							sf::Packet chatPacket;
							chatPacket << Packets::Chat << chatBuffer.toAnsiString();
							serverSocket->send(chatPacket);
							chatBuffer.clear();
						}
						chatting = !chatting;
					} else if (event.key.code == sf::Keyboard::BackSpace) {
						if(chatting && (int)chatBuffer.getSize() > 1){
							chatBuffer.erase(chatBuffer.getSize() - 1);
						}
					}
					break;
				}
				case sf::Event::TextEntered: {
					if(chatting && (int)chatBuffer.getSize() <= messageLimit && event.text.unicode != 8){
						chatBuffer += event.text.unicode;
					}
					if(chatting && debug){
						printf("chat size: %u\n", (int)chatBuffer.getSize());
					}
					break;
				}
				default:
					break;
				}
			}

			window->clear(sf::Color(20, 16, 50));
			g_camera.bindWorld();
			for (size_t i = 0; i < updateGroup.size(); i++) {
				updateGroup[i]->draw();
			}
			g_camera.bindUI();
			if (ownEntity) {
				g_camera.pos.x = ownEntity->x;
				g_camera.pos.y = ownEntity->y;

				ownEntity->control(controls);

				sf::Text coords;
				coords.setFont(*font);
				coords.setString(std::to_string((int)ownEntity->x).append(" ").append(std::to_string((int)ownEntity->y))
				.append("\nFPS: ").append(std::to_string(60.0 / delta))
				.append("\nPing: ").append(std::to_string((int)(lastPing * 1000.0))).append("ms"));
				coords.setCharacterSize(textCharacterSize);
				coords.setFillColor(sf::Color::White);
				window->draw(coords);
			}
			sf::Text chat;
			chat.setFont(*font);
			std::string chatString;
			for (std::string message : displayMessages) {
				chatString.append(message).append("\n");
			}
			chatString.append(chatBuffer);
			chat.setString(chatString);
			chat.setCharacterSize(textCharacterSize);
			chat.setFillColor(sf::Color::White);
			sf::FloatRect pos = chat.getLocalBounds();
			chat.move(2, g_camera.h - pos.top - pos.height - 10);
			window->draw(chat);
			g_camera.bindWorld();
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
							if(debug){
								printf(", radius %g", radius);
							}
							Attractor* e = new Attractor(radius);
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
					default:
						printf("Unknown packet %d received\n", type);
						break;
					}
				} else if (status == sf::Socket::Disconnected) {
					printf("Connection to server closed.\n");
					connectToServer();
				}
			}
			if (ownEntity && lastControls != controls) {
				sf::Packet controlsPacket;
				controlsPacket << Packets::Controls << *(unsigned char*) &controls;
				serverSocket->send(controlsPacket);
			}
			*(unsigned char*) &lastControls = *(unsigned char*) &controls;
		}

		for (size_t i = 0; i < updateGroup.size(); i++) {
			updateGroup[i]->update();
		}
		for (Entity* e : entityDeleteBuffer) {
			delete e;
		}
		entityDeleteBuffer.clear();
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
						uint16_t type;
						packet >> type;
						if(debug){
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
							if (player->username.empty() || (int)sizeof(player->username) > usernameLimit) {
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
							sf::Packet chatPacket;
							std::string sendMessage;
							sendMessage.append("<").append(player->name()).append("> has joined.");
							chatPacket << Packets::Chat << sendMessage;
							sf::Packet namePacket;
							namePacket << Packets::Name << player->entity->id << player->username;
							for (Player* p : playerGroup) {
								p->tcpSocket.send(colorPacket);
								p->tcpSocket.send(chatPacket);
								p->tcpSocket.send(namePacket);
							}
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
								std::cout << sendMessage << std::endl;
								chatPacket << Packets::Chat << sendMessage;
								for (Player* p : playerGroup) {
									p->tcpSocket.send(chatPacket);
								}
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
					for (Entity* e : updateGroup) {
						if (player->entity && (abs(e->y - player->entity->y) > player->viewH * syncCullThreshold || abs(e->x - player->entity->x) > player->viewW * syncCullThreshold)) {
							continue;
						}
						sf::Packet packet;
						packet << Packets::SyncEntity;
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
		sf::sleep(sf::seconds(std::max((1.0 - delta) / 80.0, 0.0)));
		globalTime = globalClock.getElapsedTime().asSeconds();
	}

	return 0;
}
