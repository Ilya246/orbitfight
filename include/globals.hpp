#pragma once

#include "entities.hpp"
#include "types.hpp"

#include <future>
#include <map>
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
inline std::vector<Entity*> entityDeleteBuffer;
inline std::vector<Attractor*> planets;
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
	collideScanSpacing = 0.5, collideScanDistance2 = 60.0 * 60.0,
	collideRestitution = 1.2, // how "bouncy" collisions should be
	friction = 0.002, // friction of colliding bodies, stops infinite sliding
	syncCullThreshold = 0.6,
	predictSpacing = 0.2, predictDelta = 10.0,
	G = 50.0,
	lastPing = 0.0, lastPredict = 0.0,
	predictingFor = 0.0;
inline const int displayMessageCount = 5;
inline int usernameLimit = 24 * 8,
messageLimit = 50,
textCharacterSize = 18,
nextID = 0,
predictSteps = (int)(30.0 / predictDelta * 60.0);
inline bool headless = false, autoConnect = false, debug = false,
inputWaiting = false,
chatting = false,
simulating = false;

struct Var {
	uint8_t type;
	void* value;
};

using namespace obf::Types;

inline std::map<std::string, Var> vars {{"port", {Int, &port}},
	{"syncSpacing", {Double, &syncSpacing}},
	{"collideRestitution", {Double, &collideRestitution}},
	{"gravityStrength", {Double, &G}},
	{"name", {String, &name}},
	{"serverAddress", {String, &serverAddress}},
	{"autoConnect", {Bool, &autoConnect}},
	{"DEBUG", {Bool, &debug}}};

inline sf::String displayMessages[displayMessageCount];

// prespawned entities

inline Attractor* star = nullptr;
inline Triangle* ghost = nullptr;

inline const std::string configFile = "config.txt", configDocFile = "confighelp.txt";

}
