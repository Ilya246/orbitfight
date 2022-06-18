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
		while(notDone){
			window = new sf::RenderWindow(sf::VideoMode(500, 500), "Test");
			std::printf("Specify server port: ");
			std::cin >> port;
			std::printf("Specify server address: ");
			std::cin >> serverAddress;
			serverSocket = new sf::TcpSocket;
			serverSocket->setBlocking(false);
			if(serverSocket->connect(serverAddress, port) != sf::Socket::Done){
				std::printf("Could not connect to %s:%u.\n", serverAddress.c_str(), port);
			}else{
				std::printf("Connected to %s:%u.\n", serverAddress.c_str(), port);
				notDone = false;
			}
		}
	}else{
		while(notDone){
			connectListener = new sf::TcpListener;
			std::printf("Specify port to host on: ");
			std::cin >> port;
			if(connectListener->listen(port) != sf::Socket::Done){
				std::printf("Could not host server on port %u.\n", port);
			}else{
				std::printf("Hosted server on port %u.\n", port);
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
				std::printf("%s:%u has connected.\n", sparePlayer->ip.c_str(), sparePlayer->port);
				playerGroup.push_back(sparePlayer);
				sparePlayer = new Player;
			}else if(status != sf::Socket::NotReady){
				std::printf("An incoming connection has failed.\n");
			}
		}else{
			mousePos = sf::Mouse::getPosition(*window);
		}
		sf::Event event;
		while (window->pollEvent(event)) {
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
		for(auto *entity : updateGroup){
			entity->update();
		}
		if(!headless){
			window->clear(sf::Color(25, 5, 40));
			for (auto *entity : updateGroup){
				entity->draw();
			}
			window->display();
		}
		delta = deltaClock.restart().asSeconds() * 60;
		globalTime = globalClock.getElapsedTime().asSeconds();
	}
	return 0;
}
