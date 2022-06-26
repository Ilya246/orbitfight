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
inline std::vector<Entity*> simCleanupBuffer;
inline std::vector<Attractor*> planets;
inline std::vector<std::vector<Point>> ghostTrajectories;
inline std::vector<sf::Color> ghostTrajectoryColors;
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
	syncSpacing = 0.1, fullsyncSpacing = 5.0, projectileSweepSpacing = 30.0,
	collideScanSpacing = 0.5, collideScanDistance2 = 60.0 * 60.0,
	collideRestitution = 1.2, // how "bouncy" collisions should be
	friction = 0.002, // friction of colliding bodies, stops infinite sliding
	extraStarChance = 0.3, blackholeChance = 1.0 / 3.0, starMass = 5.0e20, starR = 6.0e4,
	syncCullThreshold = 0.6, syncCullOffset = 100000.0, sweepThreshold = 4e6 * 4e6,
	predictSpacing = 0.2, predictDelta = 6.0,
	autorestartSpacing = 30.0 * 60.0 + 1, autorestartNotifSpacing = 5.0 * 60.0,
	G = 6.67e-11,
	lastPing = 0.0, lastPredict = 0.0, lastSweep = 0.0, lastAutorestartNotif = -autorestartNotifSpacing, lastAutorestart = 0.0,
	predictingFor = 0.0,
	drawShiftX, drawShiftY;
inline const int displayMessageCount = 5;
inline int usernameLimit = 24,
messageLimit = 50,
textCharacterSize = 18,
nextID = 0,
predictSteps = (int)(30.0 / predictDelta * 60.0);
inline size_t trajectoryOffset = 0;
inline bool headless = false, autoConnect = false, debug = false, autorestart = false,
inputWaiting = false, chatting = false, lockControls = false,
simulating = false,
autorestartRegenned = true;

struct Var {
	uint8_t type;
	void* value;
};

using namespace obf::Types;

inline std::map<std::string, Var> vars {{"port", {Int, &port}},
	{"predictSteps", {Int, &predictSteps}},
	{"syncSpacing", {Double, &syncSpacing}},
	{"friction", {Double, &friction}},
	{"collideRestitution", {Double, &collideRestitution}},
	{"gravityStrength", {Double, &G}},
	{"blackholeChance", {Double, &blackholeChance}},
	{"extraStarChance", {Double, &extraStarChance}},
	{"predictDelta", {Double, &predictDelta}},
	{"predictSpacing", {Double, &predictSpacing}},
	{"autorestartSpacing", {Double, &autorestartSpacing}},
	{"name", {String, &name}},
	{"serverAddress", {String, &serverAddress}},
	{"autoConnect", {Bool, &autoConnect}},
	{"DEBUG", {Bool, &debug}},
	{"autorestart", {Bool, &autorestart}}};

inline sf::String displayMessages[displayMessageCount];

inline std::vector<Attractor*> stars;
inline Entity* trajectoryRef = nullptr;
inline Entity* lastTrajectoryRef = nullptr;
inline Entity* systemCenter = nullptr;

inline const std::string configFile = "config.txt", configDocFile = "confighelp.txt";

}
