#pragma once

#include "entities.hpp"
#include "types.hpp"

#include <chrono>
#include <future>
#include <map>
#include <vector>

namespace obf {

// sf::Clock stand-in
class Clock {
public:
    Clock() : start(std::chrono::steady_clock::now()) {}
    double restart() {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - start).count();
        start = now;
        return elapsed;
    }
    double getElapsedTime() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(now - start).count();
    }
private:
    std::chrono::steady_clock::time_point start;
};

inline obf::Player* sparePlayer = new obf::Player;
inline std::vector<Entity*> updateGroup;
inline std::vector<Player*> playerGroup;
inline std::vector<obf::Quad> quadtree;
inline Clock actualDeltaClock, deltaClock, globalClock;
inline std::future<void> inputReader;
inline std::string serverAddress = "", name = "",
inputBuffer = "",
tlsCert = "", tlsKey = "";
inline int32_t port = 7817;
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
    predictSpacing = 0.25, predictDelta = 0.4,
    predictBaseScale = 200.0,
    autorestartSpacing = 30.0 * 60.0 + 1, autorestartNotifSpacing = 5.0 * 60.0,
    G = 6.67e-11,
    gravityAccuracy = 5.0,
    targetFramerate = 90.0,
    lastPing = 0.0, lastPredict = 0.0, lastSweep = 0.0, lastAutorestartNotif = -autorestartNotifSpacing, lastAutorestart = 0.0,
    lastShowFramerate = 0.0,
    predictingFor = 0.0,
    ownX = 0.0, ownY = 0.0;
inline int32_t nextID = 0,
predictSteps = (int)(90.0 / predictDelta),
gen_baseMinPlanets = 10,
gen_baseMaxPlanets = 15;
inline size_t messageLimit = 50, usernameLimit = 24;
inline long long measureFrames = 0, framerate = 0;
inline bool debug = false, autorestart = false,
inputWaiting = false,
autorestartRegenned = true,
printPlanetMerges = true;
inline int32_t worldBrightness = 32, worldBrightnessMax = 32, worldBrightnessMin = 0;

struct Var {
    uint8_t type;
    void* value;
    bool synced = true;
};

using namespace obf::Types;

inline std::map<std::string, Var> vars {
    {"name", {String, &name, false}},
    {"port", {Int32, &port, false}},
    {"serverAddress", {String, &serverAddress, false}},
    {"tlsCert", {String, &tlsCert, false}},
    {"tlsKey", {String, &tlsKey, false}},

    {"predictDelta", {Double, &predictDelta}},
    {"predictSpacing", {Double, &predictSpacing}},
    {"predictBaseScale", {Double, &predictBaseScale}},
    {"predictSteps", {Int32, &predictSteps}},

    {"DEBUG", {Bool, &debug, false}},
    {"printPlanetMerges", {Bool, &printPlanetMerges, false}},

    {"autorestart", {Bool, &autorestart, false}},
    {"autorestartNotifSpacing", {Double, &autorestartNotifSpacing, false}},
    {"autorestartSpacing", {Double, &autorestartSpacing, false}},

    {"maxAckTime", {Double, &maxAckTime, false}},
    {"syncSpacing", {Double, &syncSpacing, false}},
    {"fullSyncSpacing", {Double, &fullsyncSpacing, false}},
    {"targetFramerate", {Double, &targetFramerate, false}},

    {"sweepThreshold", {Double, &sweepThreshold, false}},

    {"gravityAccuracy", {Double, &gravityAccuracy}},

    {"friction", {Double, &friction}},
    {"collideRestitution", {Double, &collideRestitution}},
    {"gravityStrength", {Double, &G}},

    {"deltaOverride", {Double, &deltaOverride}},
    {"timescale", {Double, &timescale}},

    {"shipSpawnDistanceMin", {Double, &shipSpawnDistanceMin, false}},
    {"shipSpawnDistanceMax", {Double, &shipSpawnDistanceMax, false}},

    {"gen_baseDensity", {Double, &gen_baseDensity}},
    {"gen_densityFactor", {Double, &gen_densityFactor}},
    {"gen_starDensityFactor", {Double, &gen_starDensityFactor}},
    {"gen_baseMinPlanets", {Int32, &gen_baseMinPlanets}},
    {"gen_baseMaxPlanets", {Int32, &gen_baseMaxPlanets}},
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
    {"triangle_slowRotateSpeed", {Double, &Triangle::slowRotateSpeed}},
};

inline Entity* trajectoryRef = nullptr;
inline Entity* lastTrajectoryRef = nullptr;
inline Entity* systemCenter = nullptr;

inline const std::string configFile = "config.txt", configDocFile = "confighelp.txt";

}
