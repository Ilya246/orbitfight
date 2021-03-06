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
inline sf::Text* posInfo = nullptr;
inline sf::Text* chat = nullptr;
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
inline unsigned short port = 7817;
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
	targetFramerate = 90.0,
	lastPing = 0.0, lastPredict = 0.0, lastSweep = 0.0, lastAutorestartNotif = -autorestartNotifSpacing, lastAutorestart = 0.0,
	lastShowFramerate = 0.0,
	predictingFor = 0.0,
	drawShiftX = 0.0, drawShiftY = 0.0,
	ownX = 0.0, ownY = 0.0;
inline const int displayMessageCount = 7, storedMessageCount = 40;
inline int usernameLimit = 24,
messageLimit = 50,
textCharacterSize = 18,
nextID = 0,
predictSteps = (int)(30.0 / predictDelta * 60.0),
messageCursorPos = storedMessageCount - displayMessageCount;
inline long long measureFrames = 0, framerate = 0;
inline size_t trajectoryOffset = 0;
inline bool headless = false, autoConnect = false, debug = false, autorestart = false,
inputWaiting = false, chatting = false, lockControls = false,
enableControlLock = false,
simulating = false,
autorestartRegenned = true, fullclearing = false;

struct Var {
	uint8_t type;
	void* value;
};

using namespace obf::Types;

inline std::map<std::string, Var> vars {{"port", {Int, &port}},
	{"predictSteps", {Int, &predictSteps}},
	{"targetFramerate", {Double, &targetFramerate}},
	{"syncSpacing", {Double, &syncSpacing}},
	{"friction", {Double, &friction}},
	{"collideRestitution", {Double, &collideRestitution}},
	{"gravityStrength", {Double, &G}},
	{"blackholeChance", {Double, &blackholeChance}},
	{"extraStarChance", {Double, &extraStarChance}},
	{"predictDelta", {Double, &predictDelta}},
	{"predictSpacing", {Double, &predictSpacing}},
	{"autorestartSpacing", {Double, &autorestartSpacing}},
	{"autorestartNotifSpacing", {Double, &autorestartNotifSpacing}},
	{"name", {String, &name}},
	{"serverAddress", {String, &serverAddress}},
	{"autoConnect", {Bool, &autoConnect}},
	{"autorestart", {Bool, &autorestart}},
	{"enableControlLock", {Bool, &enableControlLock}},
	{"DEBUG", {Bool, &debug}}};

inline sf::String storedMessages[storedMessageCount];

inline std::vector<Attractor*> stars;
inline Entity* trajectoryRef = nullptr;
inline Entity* lastTrajectoryRef = nullptr;
inline Entity* systemCenter = nullptr;

inline const std::string configFile = "config.txt", configDocFile = "confighelp.txt";

}
