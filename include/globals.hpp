#pragma once

#include "entities.hpp"

#include <vector>

#include <SFML/Graphics.hpp>
#include <SFML/Network.hpp>

namespace obf {

inline sf::TcpSocket* serverSocket = nullptr;
inline sf::TcpListener* connectListener = nullptr;
inline sf::RenderWindow* window = nullptr;
inline obf::Entity* ownEntity = nullptr;
inline sf::Font* font = nullptr;
inline obf::Player* sparePlayer = new obf::Player;
inline std::vector<Entity*> updateGroup;
inline std::vector<Player*> playerGroup;
inline sf::Vector2i mousePos;
inline sf::Clock deltaClock, globalClock;
inline std::string serverAddress = "", name = "";
inline unsigned short port = 0;
inline movement lastControls, controls;
inline double delta = 1.0 / 60.0,
	globalTime = 0.0,
	maxAckTime = 15.0,
	syncSpacing = 0.02,
	G = 50;
inline int nextID = 0;
inline bool headless = false, autoConnect = false;

inline const std::string configFile = "config.txt", configDocFile = "confighelp.txt";

}
