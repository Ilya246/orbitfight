#pragma once

#include "entities.hpp"

#include <future>
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
inline std::future<void> inputReader;
inline std::string serverAddress = "", name = "",
inputBuffer = "";
inline sf::String chatBuffer = "";
inline unsigned short port = 0;
inline movement lastControls, controls;
inline double delta = 1.0 / 60.0,
	globalTime = 0.0,
	maxAckTime = 15.0,
	syncSpacing = 0.1,
	G = 50;
inline const int displayMessageCount = 5;
inline int usernameLimit = 24 * 8,
messageLimit = 50 * 8,
textCharacterSize = 18,
nextID = 0;
inline bool headless = false, autoConnect = false, debug = false,
inputWaiting = false,
chatting = false;

inline sf::String displayMessages[displayMessageCount];

// prespawned entities - temporary

inline Attractor* star = nullptr;
inline Attractor* planet = nullptr;

inline const std::string configFile = "config.txt", configDocFile = "confighelp.txt";

}
