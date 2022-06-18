#pragma once

#include <vector>

#include <SFML/Graphics.hpp>
#include <SFML/Network.hpp>

namespace obf{

struct Entity;

inline sf::TcpSocket* serverSocket = nullptr;
inline sf::TcpListener* connectListener = nullptr;
inline sf::RenderWindow* window = nullptr;
inline obf::Player* sparePlayer = new obf::Player;
inline std::vector<Entity*> updateGroup;
inline std::vector<Player*> playerGroup;
inline sf::Vector2i mousePos;
inline sf::Clock deltaClock, globalClock;
inline std::string serverAddress;
inline unsigned short port;
inline float delta = 1.0 / 60.0,
	globalTime = 0;
inline bool headless = false;

}
