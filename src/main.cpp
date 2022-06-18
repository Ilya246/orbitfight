#include "entities.hpp"
#include "globals.hpp"

#include <SFML/Graphics.hpp>
#include <SFML/Network.hpp>

#include <cstring>
#include <iostream>

using namespace obf;

int main(int argc, char** argv){
	for(int i = 1; i < argc; i++){
    	headless |= !std::strcmp(argv[i], "--headless");
	}
	bool notDone = true;
	if(!headless){
		serverSocket = new sf::TcpSocket;
		while(notDone){
			window = new sf::RenderWindow(sf::VideoMode(500, 500), "Test");
			printf("Specify server port: ");
			std::cin >> port;
			printf("Specify server address: ");
			std::cin >> serverAddress;
			if(serverSocket->connect(serverAddress, port) != sf::Socket::Done){
				printf("Could not connect to %s:%u.\n", serverAddress.c_str(), port);
			}else{
				printf("Connected to %s:%u.\n", serverAddress.c_str(), port);
				serverSocket->setBlocking(false);
				notDone = false;
			}
		}
	}else{
		connectListener = new sf::TcpListener;
		connectListener->setBlocking(false);
		while(notDone){
			printf("Specify port to host on: ");
			std::cin >> port;
			if(connectListener->listen(port) != sf::Socket::Done){
				printf("Could not host server on port %u.\n", port);
			}else{
				printf("Hosted server on port %u.\n", port);
				notDone = false;
			}
		}
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
				sparePlayer = new Player;
			}else if(status != sf::Socket::NotReady){
				printf("An incoming connection has failed.\n");
			}
		}else{
			mousePos = sf::Mouse::getPosition(*window);
			sf::Event event;
			while(window->pollEvent(event)) {
				switch (event.type) {
				case sf::Event::Closed:
					window->close();
					break;
				case sf::Event::Resized:
					window->setView(sf::View(sf::FloatRect(0, 0, event.size.width, event.size.height)));
				default:
					break;
				}
			}
			window->clear(sf::Color(25, 5, 40));
			for(auto* entity : updateGroup){
				entity->draw();
			}
			window->display();
			sf::Socket::Status status = sf::Socket::Done;
			while(status != sf::Socket::NotReady){
				sf::Packet packet;
				status = serverSocket->receive(packet);
				if(status == sf::Socket::Done){
					uint16_t type;
					packet >> type;
					serverSocket->setBlocking(true);
					switch(type){
						case 0:
							sf::Packet ackPacket;
							ackPacket << (uint16_t)0;
							serverSocket->send(ackPacket);
							break;
					}
					serverSocket->setBlocking(false);
				}
			}
		}
		for(auto* entity : updateGroup){
			entity->update();
		}
		int to = playerGroup.size();
		for(int i = 0; i < to; i++){
			Player* player = playerGroup.at(i);
			if(headless){
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
						unsigned char type;
						packet >> type;
						switch(type){
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
			}
		}
		delta = deltaClock.restart().asSeconds() * 60;
		sf::sleep(sf::seconds(std::max(1.0 / 60.0 - delta, 0.0)));
		globalTime = globalClock.getElapsedTime().asSeconds();
	}
	return 0;
}
