#pragma once

#include "entities.hpp"
#include "types.hpp"
#include "ui.hpp"

#include <future>
#include <map>
#include <thread>
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
inline obf::TextBoxElement* activeTextbox = nullptr;
inline std::vector<Entity*> updateGroup;
inline std::vector<Player*> playerGroup;
inline std::vector<UIElement*> uiGroup;
inline obf::MenuUI* menuUI = nullptr;
inline std::vector<Entity*> simCleanupBuffer;
inline std::vector<CelestialBody*> planets;
inline std::vector<std::vector<Point>> ghostTrajectories;
inline std::vector<sf::Color> ghostTrajectoryColors;
inline sf::Vector2i mousePos;
inline sf::Clock deltaClock, globalClock;
inline std::future<void> inputReader;
inline std::vector<std::thread*> updateThreads;
inline std::string serverAddress = "", name = "",
inputBuffer = "";
inline unsigned short port = 7817;
inline movement lastControls, controls;
inline double delta = 1.0 / 60.0,
	globalTime = 0.0,
	deltaOverride = -1.0, // disabled when < 0
	timescale = 1.0,
	maxAckTime = 15.0,
	syncSpacing = 0.2, fullsyncSpacing = 5.0, projectileSweepSpacing = 30.0,
	collideRestitution = 1.6, // how "bouncy" collisions should be
	friction = 0.002, // friction of colliding bodies, stops infinite sliding
	gen_extraStarChance = 0.3, gen_blackholeChance = 1.0 / 3.0, gen_starMass = 5.0e20, gen_starRadius = 6.0e4,
	gen_firstPlanetDistance = 120000.0, gen_minNextRadius = 1.2, gen_maxNextRadius = 1.7, gen_minPlanetRadius = 600.0, gen_maxPlanetRadius = 9000.0,
	gen_baseDensity = 8.0e9, gen_moonFactor = gen_maxPlanetRadius * 0.24, gen_minMoonDistance = 2.0, gen_maxMoonDistance = 9.0,
	gen_minMoonRadius = 120.0, gen_maxMoonRadiusFrac = 1.0 / 6.0,
	syncCullThreshold = 0.6, syncCullOffset = 100000.0, sweepThreshold = 10e6 * 10e6,
	predictSpacing = 0.2, predictDelta = 6.0,
	extraQuadAllocation = 2.0, quadReallocateThreshold = 0.6, quadtreeShrinkThreshold = 0.2,
	autorestartSpacing = 30.0 * 60.0 + 1, autorestartNotifSpacing = 5.0 * 60.0,
	G = 6.67e-11,
	gravityAccuracy = 5.0,
	targetFramerate = 90.0,
	lastPing = 0.0, lastPredict = 0.0, lastSweep = 0.0, lastAutorestartNotif = -autorestartNotifSpacing, lastAutorestart = 0.0,
	lastShowFramerate = 0.0,
	predictingFor = 0.0,
	drawShiftX = 0.0, drawShiftY = 0.0,
	ownX = 0.0, ownY = 0.0;
inline int textCharacterSize = 18,
nextID = 0,
predictSteps = (int)(30.0 / predictDelta * 60.0),
gen_baseMinPlanets = 5,
gen_baseMaxPlanets = 10,
quadsConstructed = 100, minQuadtreeSize = 80,
quadsAllocated = (int)(quadsConstructed * extraQuadAllocation),
updateThreadCount = 1;
inline size_t minThreadEntities = 100,
messageLimit = 50, usernameLimit = 24;
inline long long measureFrames = 0, framerate = 0;
inline size_t trajectoryOffset = 0;
inline bool headless = false, isServer = false, authority = false, autoConnect = false, debug = false, autorestart = false,
inputWaiting = false, lockControls = false,
handledTextBoxSelect = false,
enableControlLock = false,
simulating = false,
autorestartRegenned = true,
printPlanetMerges = true;

inline obf::Quad* quadtree = (Quad*)malloc((size_t)(sizeof(Quad) * quadsAllocated));

struct Var {
	uint8_t type;
	void* value;
};

using namespace obf::Types;

inline std::map<std::string, Var> vars {
	{"name", {String, &name}},
	{"port", {Int, &port}},
	{"serverAddress", {String, &serverAddress}},

	{"predictDelta", {Double, &predictDelta}},
	{"predictSpacing", {Double, &predictSpacing}},
	{"predictSteps", {Int, &predictSteps}},

	{"autoConnect", {Bool, &autoConnect}},
	{"DEBUG", {Bool, &debug}},
	{"enableControlLock", {Bool, &enableControlLock}},
	{"printPlanetMerges", {Bool, &printPlanetMerges}},

	{"autorestart", {Bool, &autorestart}},
	{"autorestartNotifSpacing", {Double, &autorestartNotifSpacing}},
	{"autorestartSpacing", {Double, &autorestartSpacing}},

	{"maxAckTime", {Double, &maxAckTime}},
	{"syncSpacing", {Double, &syncSpacing}},
	{"fullSyncSpacing", {Double, &fullsyncSpacing}},
	{"targetFramerate", {Double, &targetFramerate}},
	{"updateThreadCount", {Int, &updateThreadCount}},
	{"minThreadEntities", {Int, &minThreadEntities}},

	{"sweepThreshold", {Double, &sweepThreshold}},

	{"gravityAccuracy", {Double, &gravityAccuracy}},

	{"friction", {Double, &friction}},
	{"collideRestitution", {Double, &collideRestitution}},
	{"gravityStrength", {Double, &G}},

	{"deltaOverride", {Double, &deltaOverride}},
	{"timescale", {Double, &timescale}},

	{"gen_baseDensity", {Double, &gen_baseDensity}},
	{"gen_baseMinPlanets", {Int, &gen_baseMinPlanets}},
	{"gen_baseMaxPlanets", {Int, &gen_baseMaxPlanets}},
	{"gen_blackholeChance", {Double, &gen_blackholeChance}},
	{"gen_extraStarChance", {Double, &gen_extraStarChance}},
	{"gen_firstPlanetDistance", {Double, &gen_firstPlanetDistance}},
	{"gen_minNextRadius", {Double, &gen_minNextRadius}},
	{"gen_maxNextRadius", {Double, &gen_maxNextRadius}},
	{"gen_minPlanetRadius", {Double, &gen_minPlanetRadius}},
	{"gen_maxPlanetRadius", {Double, &gen_maxPlanetRadius}},
	{"gen_minMoonDistance", {Double, &gen_minMoonDistance}},
	{"gen_maxMoonDistance", {Double, &gen_maxMoonDistance}},
	{"gen_moonFactor", {Double, &gen_moonFactor}},
	{"gen_starMass", {Double, &gen_starMass}},
	{"gen_starRadius", {Double, &gen_starRadius}}};

inline std::vector<CelestialBody*> stars;
inline Entity* trajectoryRef = nullptr;
inline Entity* lastTrajectoryRef = nullptr;
inline Entity* systemCenter = nullptr;

inline const std::string configFile = "config.txt", configDocFile = "confighelp.txt";

}
