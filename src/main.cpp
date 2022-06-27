#include "camera.hpp"
#include "entities.hpp"
#include "font.hpp"
#include "globals.hpp"
#include "math.hpp"
#include "net.hpp"
#include "types.hpp"
#include "toml.hpp"

#include <SFML/Graphics.hpp>
#include <SFML/Network.hpp>

#include <cfloat>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>

using namespace obf;

void connectToServer() {
	serverSocket = new sf::TcpSocket;
	printf("See https://github.com/Ilya246/orbitfight/blob/master/SERVERS.md for 24/7 servers\n");
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

void generateSystem() {
	int starsN = 1;
	while (rand_f(0.f, 1.f) < extraStarChance) {
		starsN += 1;
	}
	double angleSpacing = TAU / starsN, angle = 0.0;
	double starsMass = starMass * starsN, dist = (starsN - 1) * starR * 2.0;
	for (int i = 0; i < starsN; i++) {
		Attractor* star = nullptr;
		double posX = std::cos(angle) * dist, posY = std::sin(angle) * dist;
		if (rand_f(0.f, 1.f) < blackholeChance) {
			star = new Attractor(2.0 * G * starMass / (CC), starMass * 1.0001);
			star->setColor(0, 0, 0);
			star->blackhole = true;
		} else {
			star = new Attractor(starR, starMass);
			star->setColor(255, 229, 97);
		}
		star->setPosition(posX, posY);
		star->star = true;
		stars.push_back(star);
		angle += angleSpacing;
	}
	if (starsN > 1) {
		double aX = 0.0, aY = 0.0;
		for (int i = 1; i < starsN; i++) {
			double xdiff = stars[i]->x - stars[0]->x, ydiff = stars[i]->y - stars[0]->y,
			factor = stars[i]->mass * G / pow(xdiff * xdiff + ydiff * ydiff, 1.5);
			aX += factor * xdiff;
			aY += factor * ydiff;
		}
		double vel = sqrt(dst(aX, aY) * dist);
		angle = 0.0;
		for (int i = 0; i < starsN; i++) {
			stars[i]->addVelocity(vel * std::cos(angle + PI / 2.0), -vel * std::sin(angle + PI / 2.0));
			angle += angleSpacing;
		}
	}
	float minrange = 120000.0 + starsN * starR * 2.0;
	float maxrange = 4000000.0 + starsN * starR * 30.0;
	int planets = (int)rand_f(5.f, 10.f);
	int totalMoons = 0;
	for (int i = 0; i < planets; i++) {
		double spawnDst = rand_f(minrange, maxrange);
		double factor = sqrt(spawnDst) / (600.0 + starsN * 100.0);
		float spawnAngle = rand_f(-PI, PI);
		float radius = rand_f(600.f, 6000.f * factor);
		double density = 8e9 / pow(radius, 1.0 / 3.0);
		Attractor* planet = new Attractor(radius, radius * radius * density);
		planet->setPosition(spawnDst * std::cos(spawnAngle), spawnDst * std::sin(spawnAngle));
		double vel = sqrt(G * starsMass / spawnDst);
		planet->addVelocity(vel * std::cos(spawnAngle + PI / 2.0), -vel * std::sin(spawnAngle + PI / 2.0));
		planet->setColor((int)rand_f(64.f, 255.f), (int)rand_f(64.f, 255.f), (int)rand_f(64.f, 255.f));
		if (radius >= 4000.f) {
			int moons = (int)(rand_f(0.f, 4.f) * radius * radius / (13000.0 * 13000.0));
			totalMoons += moons;
			for (int it = 0; it < moons; it++) {
				double mSpawnDst = planet->radius + rand_f(6000.f, 80000.f) * factor;
				float spawnAngle = rand_f(-PI, PI);
				float radius = rand_f(120.f, planet->radius / 6.f);
				double density = 8e9 / pow(radius, 1.0 / 3.0);
				Attractor* moon = new Attractor(radius, radius * radius * density);
				moon->setPosition(planet->x + mSpawnDst * std::cos(spawnAngle), planet->y + mSpawnDst * std::sin(spawnAngle));
				double vel = sqrt(G * planet->mass / mSpawnDst);
				moon->addVelocity(planet->velX + vel * std::cos(spawnAngle + PI / 2.0), -planet->velY - vel * std::sin(spawnAngle + PI / 2.0));
				moon->setColor((int)rand_f(64.f, 255.f), (int)rand_f(64.f, 255.f), (int)rand_f(64.f, 255.f));
				obf::planets.push_back(moon);
			}
		}
		obf::planets.push_back(planet);
	}
	printf("Generated system: %u stars, %u planets, %u moons\n", starsN, planets, totalMoons);
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
		out << "port: Used both as the port to host on and to specify port for autoConnect (short uint)" << std::endl;
		out << "predictSteps: How many steps of [predictDelta] ticks to simulate for trajectory prediction (int)" << std::endl;
		out << "NOTE: any clients will have to have the same physics-related configs as the server for them to work properly" << std::endl;
		out << "friction: friction of touching bodies (double)" << std::endl;
		out << "collideRestitution: how bouncy collisions are (double)" << std::endl;
		out << "gravityStrength: how strong gravity is (double)" << std::endl;
		out << "syncSpacing: As a server, how often should clients be synced (double)" << std::endl;
		out << "serverAddress: Used with autoConnect as the address to connect to (string)" << std::endl;
		out << "name: Your ingame name as a client (string)" << std::endl;
		out << "autoConnect: As a client, whether to automatically connect to a server (bool)" << std::endl;
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

		generateSystem();
	} else {
		window = new sf::RenderWindow(sf::VideoMode(500, 500), "Orbitfight");

		systemCenter = new Attractor(true);

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
					std::string_view view(inputBuffer);
					parseCommand(view);
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
						for (Entity* e : updateGroup) {
							delete e;
						}
						entityDeleteBuffer.clear();
						generateSystem();
					}
					autorestartRegenned = true;
				} else {
					if (lastAutorestart + autorestartSpacing < globalTime) {
						delta = 0.0;
						for (Entity* e :updateGroup) {
							if (e->type() != Entities::Triangle) {
								delete e;
							}
						}
						generateSystem();
						for (Entity* e : updateGroup) {
							if (e->type() != Entities::Triangle) {
								e->syncCreation();
							}
						}
						for (Player* p : playerGroup) {
							setupShip(p->entity);
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
				setupShip(sparePlayer->entity);
				sparePlayer->entity->player = sparePlayer;
				sparePlayer->entity->syncCreation();
				sf::Packet entityAssign;
				entityAssign << Packets::AssignEntity << sparePlayer->entity->id;
				sparePlayer->tcpSocket.send(entityAssign);
				for (Player* p: playerGroup) {
					sf::Packet namePacket;
					namePacket << Packets::Name << p->entity->id << p->username;
					sparePlayer->tcpSocket.send(namePacket);
				}
				sparePlayer = new Player;
			} else if (status != sf::Socket::NotReady) {
				printf("An incoming connection has failed.\n");
			}
		} else {
			if (window->hasFocus()) {
				mousePos = sf::Mouse::getPosition(*window);
				if (!chatting) {
					controls.forward = sf::Keyboard::isKeyPressed(sf::Keyboard::W);
					controls.backward = sf::Keyboard::isKeyPressed(sf::Keyboard::S);
					controls.turnleft = sf::Keyboard::isKeyPressed(sf::Keyboard::A);
					controls.turnright = sf::Keyboard::isKeyPressed(sf::Keyboard::D);
					controls.boost = sf::Keyboard::isKeyPressed(sf::Keyboard::LControl);
					controls.primaryfire = sf::Keyboard::isKeyPressed(sf::Keyboard::Space);
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
					if (!chatting && event.key.code == sf::Keyboard::LShift && ownEntity) {
						controls.hyperboost = !controls.hyperboost;
					} else if (!chatting && enableControlLock && event.key.code == sf::Keyboard::LAlt) {
						lockControls = !lockControls;
						sf::Packet controlsPacket;
						controlsPacket << Packets::Controls << (lockControls ? (unsigned char) 0 : *(unsigned char*) &controls);
						serverSocket->send(controlsPacket);
					} else if (event.key.code == sf::Keyboard::Tab && ownEntity) {
						double minDst = DBL_MAX;
						Entity* closestEntity = nullptr;
						for (Entity* e : updateGroup) {
							double dst = dst2(e->x - ownEntity->x - (mousePos.x - g_camera.w * 0.5) * g_camera.scale, e->y - ownEntity->y - (mousePos.y - g_camera.h * 0.5) * g_camera.scale) - e->radius * e->radius;
							if (dst < minDst) {
								minDst = dst;
								closestEntity = e;
							}
						}
						if (dst2(systemCenter->x - ownEntity->x - (mousePos.x - g_camera.w * 0.5) * g_camera.scale, systemCenter->y - ownEntity->y - (mousePos.y - g_camera.h * 0.5) * g_camera.scale) < minDst) {
							closestEntity = systemCenter;
						}
						if (closestEntity == trajectoryRef) {
							trajectoryRef = nullptr;
							lastTrajectoryRef = nullptr;
						} else {
							trajectoryRef = closestEntity;
						}
					} else if (event.key.code == sf::Keyboard::Return) {
						if(chatting && (int)chatBuffer.getSize() > 1){
							std::string sendMessage = chatBuffer.toAnsiString();
							if (chatBuffer[1] == '/') {
								parseCommand(sendMessage.substr(2, sendMessage.size() - 2));
								chatting = !chatting;
								chatBuffer.clear();
								break;
							}
							sf::Packet chatPacket;
							chatPacket << Packets::Chat << sendMessage;
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
					if(chatting && (int)chatBuffer.getSize() < messageLimit && event.text.unicode != 8){
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

			window->clear(sf::Color(16, 0, 32));
			g_camera.bindWorld();
			if (ownEntity) [[likely]] {
				drawShiftX = -ownEntity->x, drawShiftY = -ownEntity->y;
				g_camera.pos.x = 0;
				g_camera.pos.y = 0;
			} else {
				drawShiftX = 0, drawShiftY = 0;
			}
			trajectoryOffset = floor((globalTime - lastPredict) / predictDelta * 60.0);
			for (size_t i = 0; i < ghostTrajectories.size(); i++) {
				std::vector<Point> traj = ghostTrajectories[i];
				if (lastTrajectoryRef && traj.size() > 0) [[likely]] {
					sf::Color trajColor = ghostTrajectoryColors[i];
					sf::VertexArray lines(sf::LineStrip, traj.size());
					float lastAlpha = 255;
					float decBy = (255.f - 64.f) / traj.size();
					for (size_t i = 0; i < traj.size(); i++) {
						Point point = traj[i];
						lines[i].position = sf::Vector2f(lastTrajectoryRef->x + point.x + drawShiftX, lastTrajectoryRef->y + point.y + drawShiftY);
						lines[i].color = trajColor;
						lines[i].color.a = lastAlpha;
						lastAlpha -= decBy;
					}
					window->draw(lines);
				}
			}
			if (!stars.empty()) {
				double x = 0.0, y = 0.0;
				for (Attractor* star : stars) {
					x += star->x;
					y += star->y;
				}
				x /= stars.size();
				y /= stars.size();
				systemCenter->setPosition(x, y);
			}
			for (size_t i = 0; i < updateGroup.size(); i++) {
				updateGroup[i]->draw();
			}
			g_camera.bindUI();
			if (ownEntity) {
				if (!lockControls) {
					ownEntity->control(controls);
				}

				sf::Text coords;
				coords.setFont(*font);
				std::string info = "";
				info.append("FPS: ").append(std::to_string(60.0 / delta))
				.append("\nPing: ").append(std::to_string((int)(lastPing * 1000.0))).append("ms");
				if (lastTrajectoryRef) {
					info.append("\nrX: ").append(std::to_string((int)(ownEntity->x - lastTrajectoryRef->x)))
					.append(", rY: ").append(std::to_string((int)(ownEntity->y - lastTrajectoryRef->y)))
					.append("\nrVx: ").append(std::to_string((int)(ownEntity->velX - lastTrajectoryRef->velX)))
					.append(", rVy: ").append(std::to_string((int)(ownEntity->velY - lastTrajectoryRef->velY)));
				}
				coords.setString(info);
				coords.setCharacterSize(textCharacterSize);
				coords.setFillColor(sf::Color::White);
				window->draw(coords);
				if (lastTrajectoryRef) {
					float radius = std::max(5.f, (float)(lastTrajectoryRef->radius / g_camera.scale));
					sf::CircleShape selection(radius, 4);
					selection.setOrigin(radius, radius);
					selection.setPosition(g_camera.w * 0.5 + (lastTrajectoryRef->x - ownEntity->x) / g_camera.scale, g_camera.h * 0.5 + (lastTrajectoryRef->y - ownEntity->y) / g_camera.scale);
					selection.setFillColor(sf::Color(0, 0, 0, 0));
					selection.setOutlineColor(sf::Color(255, 255, 64));
					selection.setOutlineThickness(1.f);
					window->draw(selection);
				}
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
			chat.move(2, g_camera.h - (textCharacterSize + 4) * 6);
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
					clientParsePacket(packet);
				} else if (status == sf::Socket::Disconnected) {
					printf("Connection to server closed.\n");
					connectToServer();
				}
			}
			if (ownEntity && lastControls != controls && !lockControls) {
				sf::Packet controlsPacket;
				controlsPacket << Packets::Controls << *(unsigned char*) &controls;
				serverSocket->send(controlsPacket);
				*(unsigned char*) &lastControls = *(unsigned char*) &controls;
			}
		}

		for (size_t i = 0; i < updateGroup.size(); i++) {
			updateGroup[i]->update1();
		}
		for (size_t i = 0; i < updateGroup.size(); i++) {
			updateGroup[i]->update2();
		}

		if (headless && lastSweep + projectileSweepSpacing < globalTime) {
			for (Entity* e : updateGroup) {
				if (e->type() != Entities::Projectile) {
					continue;
				}
				double closest = DBL_MAX;
				for (Player* p : playerGroup) {
					if (!p->entity) {
						continue;
					}
					closest = std::min(closest, dst2(e->x - p->entity->x, e->y - p->entity->y));
				}
				if (closest > sweepThreshold && std::find(entityDeleteBuffer.begin(), entityDeleteBuffer.end(), e) == entityDeleteBuffer.end()) {
					entityDeleteBuffer.push_back(e);
				}
			}
			lastSweep = globalTime;
		}
		for (Entity* e : entityDeleteBuffer) {
			delete e;
		}
		entityDeleteBuffer.clear();
		if (!headless && globalTime - lastPredict > predictSpacing && trajectoryRef) [[unlikely]] {
			double resdelta = delta;
			double resTime = globalTime;
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
				std::copy(std::begin(ownEntity->color), std::end(ownEntity->color), std::begin(ghost->color));
				simCleanupBuffer.push_back(ghost);
			}
			for (Entity* e : updateGroup) {
				e->simSetup();
				e->trajectory->clear();
			}
			for (int i = 0; i < predictSteps; i++) {
				predictingFor = predictDelta * predictSteps;
				globalTime += predictDelta / 60.0;
				for (size_t i = 0; i < updateGroup.size(); i++) {
					updateGroup[i]->update1();
				}
				for (size_t i = 0; i < updateGroup.size(); i++) {
					updateGroup[i]->update2();
				}
				if (!stars.empty()) [[likely]] {
					double x = 0.0, y = 0.0;
					for (Attractor* star : stars) {
						x += star->x;
						y += star->y;
					}
					x /= stars.size();
					y /= stars.size();
					systemCenter->setPosition(x, y);
				}
				for (Entity* e : updateGroup) {
					e->trajectory->push_back({e->x - trajectoryRef->x, e->y - trajectoryRef->y});
				}
				if (ownEntity) {
					ownEntity->control(controls);
				}
				for (Entity* en : entityDeleteBuffer) {
					for (size_t i = 0; i < updateGroup.size(); i++) {
						Entity* e = updateGroup[i];
						if (e == en) [[unlikely]] {
							updateGroup[i] = updateGroup[updateGroup.size() - 1];
							updateGroup.pop_back();
						} else {
							for (size_t i = 0; i < e->near.size(); i++){
								if (e->near[i] == en) [[unlikely]] {
									e->near[i] = e->near[e->near.size() - 1];
									e->near.pop_back();
									break;
								}
							}
						}
					}
				}
				entityDeleteBuffer.clear();
			}
			for (Entity* en : simCleanupBuffer) {
				ghostTrajectories.push_back(*en->trajectory);
				ghostTrajectoryColors.push_back(sf::Color(en->color[0] * 0.7, en->color[1] * 0.7, en->color[2] * 0.7));
				entityDeleteBuffer.push_back(en);
			}
			simCleanupBuffer.clear();
			updateGroup = retUpdateGroup;
			for (Entity* e : updateGroup) {
				e->simReset();
			}
			delta = resdelta;
			simulating = false;
			globalTime = resTime;
			lastPredict = globalTime;
			lastTrajectoryRef = trajectoryRef;
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
					bool fullsync = player->lastFullsynced + fullsyncSpacing < globalTime;
					for (Entity* e : updateGroup) {
						if (player->entity && !fullsync && (abs(e->y - player->entity->y) - syncCullOffset > player->viewH * syncCullThreshold || abs(e->x - player->entity->x) - syncCullOffset > player->viewW * syncCullThreshold)) {
							continue;
						}
						sf::Packet packet;
						packet << Packets::SyncEntity;
						e->loadSyncPacket(packet);
						player->tcpSocket.send(packet);
					}
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

		delta = deltaClock.restart().asSeconds() * 60;
		sf::sleep(sf::seconds(std::max((1.0 - delta) / 80.0, 0.0)));
		globalTime = globalClock.getElapsedTime().asSeconds();
	}

	return 0;
}
