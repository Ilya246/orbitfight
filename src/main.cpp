#include "entities.hpp"
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

void connectToServer(){
	serverSocket = new sf::TcpSocket;
	while(true){
		printf("Specify server port\n");
		std::cin >> port;
		printf("Specify server address\n");
		std::cin >> serverAddress;
		if(serverSocket->connect(serverAddress, port) != sf::Socket::Done){
			printf("Could not connect to %s:%u.\n", serverAddress.c_str(), port);
		}else{
			printf("Connected to %s:%u.\n", serverAddress.c_str(), port);
			sf::Packet nicknamePacket;
			nicknamePacket << (uint16_t)3 << name;
			serverSocket->send(nicknamePacket);
			break;
		}
	}
}

int main(int argc, char** argv){
	for(int i = 1; i < argc; i++){
    	headless |= !std::strcmp(argv[i], "--headless");
	}
	int configExists = parseTomlFile(configFile);
	if(configExists != 0){
		printf("No config file detected, creating %s.\n", configFile.c_str());
	}
	std::ofstream out;
	out.open(configFile, std::ios::app);
	if(configExists != 0){
		out << "syncSpacing = " << syncSpacing << std::endl;
	}
	if(headless){
		if(port == 0){
			printf("Specify the port you will host on.\n");
			std::cin >> port;
			out << "port = " << port << std::endl;
		}
	}else{
		if(name == ""){
			printf("Specify a username.\n");
			std::cin >> name;
			out << "name = " << name << std::endl;
		}
	}
	out.close();
	if(headless){
		connectListener = new sf::TcpListener;
		connectListener->setBlocking(false);
		if(connectListener->listen(port) != sf::Socket::Done){
			printf("Could not host server on port %u.\n", port);
			return 0;
		}
		printf("Hosted server on port %u.\n", port);
	}else{
		window = new sf::RenderWindow(sf::VideoMode(500, 500), "Test");
		connectToServer();
	}
	while(headless || window->isOpen()){
		if(headless){
			sf::Socket::Status status = connectListener->accept(sparePlayer->tcpSocket);
			if(status == sf::Socket::Done){
				sparePlayer->ip = sparePlayer->tcpSocket.getRemoteAddress().toString();
				sparePlayer->port = sparePlayer->tcpSocket.getRemotePort();
				printf("%s has connected.\n", sparePlayer->name().c_str());
				sparePlayer->lastAck = globalTime;
				playerGroup.push_back(sparePlayer);
				sparePlayer->entity = new Triangle();
				sparePlayer->entity->player = sparePlayer;
				for(Entity* e : updateGroup){
					sf::Packet packet;
					packet << (uint16_t)1;
					e->loadCreatePacket(packet);
					sparePlayer->tcpSocket.send(packet);
				}
				sf::Packet entityAssign;
				entityAssign << (uint16_t)5 << (long long)sparePlayer->entity;
				sparePlayer->tcpSocket.send(entityAssign);
				sparePlayer = new Player;
			}else if(status != sf::Socket::NotReady){
				printf("An incoming connection has failed.\n");
			}
		}else{
			if(window->hasFocus()){
				mousePos = sf::Mouse::getPosition(*window);
			}else if(ownEntity != nullptr){
				mousePos = sf::Vector2i((int)(viewSizeX * 0.5 - ownEntity->velX * 200.0), (int)(viewSizeY * 0.5 + ownEntity->velY * 200.0));
			}
			sf::Event event;
			while(window->pollEvent(event)){
				switch(event.type){
				case sf::Event::Closed:
					window->close();
					break;
				case sf::Event::Resized:
					viewSizeX = event.size.width;
					viewSizeY = event.size.height;
				default:
					break;
				}
			}
			if(ownEntity != nullptr){
				sf::View view;
				view.setCenter((float)ownEntity->x, (float)ownEntity->y);
				view.setSize(viewSizeX, viewSizeY);
				window->setView(view);
				window->clear(sf::Color(25, 5, std::min(255, (int)(0.1 * std::sqrt(ownEntity->x * ownEntity->x + ownEntity->y * ownEntity->y)))));
			}
			for(auto* entity : updateGroup){
				entity->draw();
			}
			window->display();
			sf::Socket::Status status = sf::Socket::Done;
			while(status != sf::Socket::NotReady){
				sf::Packet packet;
				serverSocket->setBlocking(false);
				status = serverSocket->receive(packet);
				serverSocket->setBlocking(true);
				if(status == sf::Socket::Done){
					uint16_t type;
					packet >> type;
					switch(type){
						case 0:{
							sf::Packet ackPacket;
							ackPacket << (uint16_t)0;
							serverSocket->send(ackPacket);
						}
						break;
						case 1:{
							unsigned char entityType;
							packet >> entityType;
							switch(entityType){
								case 0:{
									Triangle* e = new Triangle;
									e->unloadCreatePacket(packet);
								}
								break;
							}
						}
						break;
						case 2:{
							long long entityID;
							packet >> entityID;
							for(Entity* e : updateGroup){
								if(e->id == entityID){
									e->unloadSyncPacket(packet);
									break;
								}
							}
						}
						break;
						case 5:{
							packet >> reinterpret_cast<long long&>(ownEntity);
							for(Entity* e : updateGroup){
								if(e->id == reinterpret_cast<long long&>(ownEntity)){
									ownEntity = e;
									break;
								}
							}
						}
						break;
						case 6:{
							long long deleteID;
							packet >> deleteID;
							for(Entity* e : updateGroup){
								if(e->id == deleteID){
									delete e;
									break;
								}
							}
						}
						break;
					}
				}else if(status == sf::Socket::Disconnected){
					printf("Connection to server closed.\n");
					connectToServer();
				}
			}
			if(ownEntity != nullptr){
				sf::Packet mouseSyncPacket;
				mouseSyncPacket << (uint16_t)4 << (double)mousePos.x + ownEntity->x - viewSizeX * 0.5 << viewSizeY * 0.5 - (double)mousePos.y - ownEntity->y;
				serverSocket->send(mouseSyncPacket);
				if(rand_f(0.0f, 1.0f) < 0.02f * delta){
					printf("Coordinates: %f, %f\n", ownEntity->x, ownEntity->y);
				}
			}
		}
		for(auto* entity : updateGroup){
			entity->update();
		}
		if(headless){
			int to = playerGroup.size();
			for(int i = 0; i < to; i++){
				Player* player = playerGroup.at(i);
				if(globalTime - player->lastAck > 1.0 && globalTime - player->lastPingSent > 1.0){
					if(globalTime - player->lastAck > maxAckTime){
						playerGroup.erase(playerGroup.begin() + i);
						i--;
						to--;
						printf("Player %s's connection has timed out.\n", player->name().c_str());
						player->tcpSocket.disconnect();
						delete player;
						continue;
					}
					sf::Packet pingPacket;
					pingPacket << (uint16_t)0;
					sf::Socket::Status status = player->tcpSocket.send(pingPacket);
					if(status != sf::Socket::Done){
						if(status == sf::Socket::Disconnected){
							playerGroup.erase(playerGroup.begin() + i);
							i--;
							to--;
							printf("Player %s has disconnected.\n", player->name().c_str());
							delete player;
							continue;
						}else if(status == sf::Socket::Error){
							printf("Error when trying to send ping packet to player %s.\n", player->name().c_str());
						}
					}
					player->lastPingSent = globalTime;
				}
				sf::Socket::Status status = sf::Socket::Done;
				while(status != sf::Socket::NotReady && status != sf::Socket::Disconnected){
					sf::Packet packet;
					player->tcpSocket.setBlocking(false);
					status = player->tcpSocket.receive(packet);
					player->tcpSocket.setBlocking(true);
					if(status == sf::Socket::Done) [[likely]]{
						player->lastAck = globalTime;
						uint16_t type;
						packet >> type;
						switch(type){
							case 3:
								packet >> player->username;
							case 4:
								packet >> player->mouseX >> player->mouseY;
						}
					}else if(status == sf::Socket::Disconnected){
						playerGroup.erase(playerGroup.begin() + i);
						i--;
						to--;
						printf("Player %s has disconnected.\n", player->name().c_str());
						delete player;
						continue;
					}
				}
				if(globalTime - player->lastSynced > syncSpacing){
					for(Entity* e : updateGroup){
						sf::Packet packet;
						packet << (uint16_t)2;
						e->loadSyncPacket(packet);
						player->tcpSocket.send(packet);
					}
					player->lastSynced = globalTime;
				}
			}
		}
		delta = deltaClock.restart().asSeconds() * 60;
		sf::sleep(sf::seconds(std::max((1.0 - delta) / 60.0, 0.0)));
		globalTime = globalClock.getElapsedTime().asSeconds();
	}
	return 0;
}
