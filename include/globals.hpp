#pragma once

#include "entities.hpp"
#include "types.hpp"
#include "ui.hpp"

#include <future>
#include <map>
#include <shared_mutex>
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
inline std::vector<std::vector<Point>> ghostTrajectories;
inline std::vector<sf::Color> ghostTrajectoryColors;
inline std::vector<obf::Quad> quadtree;
inline sf::Vector2i mousePos;
inline sf::Clock actualDeltaClock, deltaClock, globalClock;
inline std::future<void> inputReader;
inline std::vector<std::thread> updateThreads;
inline std::vector<size_t> updateThreadRanges;
inline std::mutex updateRangesMutex;
inline std::shared_mutex waitMutex, updateMutex, postUpdateMutex, preUpdateMutex;
inline std::unique_lock uWaitLock(waitMutex, std::defer_lock_t()), uUpdateLock(updateMutex), uPostLock(postUpdateMutex, std::defer_lock_t()), uPreLock(preUpdateMutex, std::defer_lock_t());
inline std::string serverAddress = "", name = "",
inputBuffer = "",
assetsFolder = "assets";
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
	gen_extraStarChance = 0.3, gen_blackholeChance = 1.0 / 3.0, gen_chandrasekharLimit = 2e26, gen_starMass = 4.0e22, gen_starRadius = 4.5e4,
	gen_firstPlanetDistance = 3.0e4, gen_minNextRadius = 1.15, gen_maxNextRadius = 1.33, gen_minPlanetRadius = 300.0, gen_maxPlanetRadius = 18000.0,
	gen_starMassReq = 0.1,
	gen_baseDensity = 2.0e9, gen_densityFactor = 3.0, gen_starDensityFactor = 1.25, gen_starColorFactor = 0.25,
	gen_moonFactor = gen_maxPlanetRadius * 0.2, gen_moonPower = 1.5, gen_minMoonDistance = 1.5, gen_maxMoonDistance = 9.0,
	gen_minMoonRadius = 120.0, gen_maxMoonRadiusFrac = 1.0 / 6.0,
	shipSpawnDistanceMin = 1.4, shipSpawnDistanceMax = 3.0,
	syncCullThreshold = 0.6, syncCullOffset = 100000.0, sweepThreshold = 3.0e6 * 3.0e6,
	predictSpacing = 0.25, predictDelta = 0.2,
	predictBaseScale = 200.0,
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
predictSteps = (int)(90.0 / predictDelta),
gen_baseMinPlanets = 10,
gen_baseMaxPlanets = 15,
updateThreadCount = 1, updateThreadsActual = 0;
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

struct Var {
	uint8_t type;
	void* value;
};

using namespace obf::Types;

inline std::map<std::string, Var> vars {
	{"name", {String, &name}},
	{"port", {Int, &port}},
	{"serverAddress", {String, &serverAddress}},

	{"assetsFolder", {String, &assetsFolder}},

	{"predictDelta", {Double, &predictDelta}},
	{"predictSpacing", {Double, &predictSpacing}},
	{"predictBaseScale", {Double, &predictBaseScale}},
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

	{"shipSpawnDistanceMin", {Double, &shipSpawnDistanceMin}},
	{"shipSpawnDistanceMax", {Double, &shipSpawnDistanceMax}},

	{"gen_baseDensity", {Double, &gen_baseDensity}},
	{"gen_densityFactor", {Double, &gen_densityFactor}},
	{"gen_starDensityFactor", {Double, &gen_starDensityFactor}},
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
	{"gen_moonPower", {Double, &gen_moonPower}},
	{"gen_starMass", {Double, &gen_starMass}},
	{"gen_starRadius", {Double, &gen_starRadius}},
	{"gen_starMassReq", {Double, &gen_starMassReq}},
	{"gen_starColorFactor", {Double, &gen_starColorFactor}},
	{"gen_chandrasekharLimit", {Double, &gen_chandrasekharLimit}},

	{"projectile_mass", {Double, &Projectile::mass}},
	{"missile_mass", {Double, &Missile::mass}},
	{"missile_accel", {Double, &Missile::accel}},
	{"missile_rotateSpeed", {Double, &Missile::rotateSpeed}},
	{"missile_maxThrustAngle", {Double, &Missile::maxThrustAngle}},
	{"missile_leastItimeDecrease", {Double, &Missile::leastItimeDecrease}},
	{"missile_fullThrustThreshold", {Double, &Missile::fullThrustThreshold}},
	{"missile_startingFuel", {Double, &Missile::startingFuel}},
	{"triangle_mass", {Double, &Triangle::mass}},
	{"triangle_accel", {Double, &Triangle::accel}},
	{"triangle_rotateSlowSpeedMult", {Double, &Triangle::rotateSlowSpeedMult}},
	{"triangle_rotateSpeed", {Double, &Triangle::rotateSpeed}},
	{"triangle_boostCooldown", {Double, &Triangle::boostCooldown}},
	{"triangle_boostStrength", {Double, &Triangle::boostStrength}},
	{"triangle_reload", {Double, &Triangle::reload}},
	{"triangle_shootPower", {Double, &Triangle::shootPower}},
	{"triangle_secondaryShootPower", {Double, &Triangle::secondaryShootPower}},
	{"triangle_secondaryRegen", {Double, &Triangle::secondaryRegen}},
	{"triangle_secondaryReload", {Double, &Triangle::secondaryReload}},
	{"triangle_maxSecondaryAngle", {Double, &Triangle::maxSecondaryAngle}},
	{"triangle_slowRotateSpeed", {Double, &Triangle::slowRotateSpeed}}};

inline Entity* trajectoryRef = nullptr;
inline Entity* lastTrajectoryRef = nullptr;
inline Entity* systemCenter = nullptr;

inline const std::string configFile = "config.txt", configDocFile = "confighelp.txt";

}
