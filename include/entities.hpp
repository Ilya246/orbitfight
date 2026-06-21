#pragma once
#include "events.hpp"
#include "packet.hpp"

#include <limits>
#include <vector>

namespace obf {

struct Player;

struct Entity;
struct Point;

void setupShip(Entity* ship, bool sync);

void generateSystem();

void fullClear(bool clearTriangles);

void updateEntities();

void buildQuadtree();

struct movement {
    int forward: 1 = 0;
    int backward: 1 = 0;
    int turnright: 1 = 0;
    int turnleft: 1 = 0;
    int boost: 1 = 0;
    int slowrotate: 1 = 0;
    int primaryfire: 1 = 0;
    int secondaryfire: 1 = 0;
};

struct Point {
    double x;
    double y;
};

bool operator ==(movement& mov1, movement& mov2);

struct Entity : EntityDeleteListener {
    Entity();
    virtual ~Entity() noexcept;

    virtual void control(movement& cont);
    virtual void update1();
    virtual void update2();
    virtual void update3();

    virtual void collide(Entity* with, bool collideOther);

    std::vector<uint32_t> collided;

    void syncCreation();

    virtual void loadCreatePacket(Packet& packet) = 0;
    virtual void loadSyncPacket(Packet& packet) = 0;

    void onEntityDelete(Entity* d) override;

    inline void setPosition(double x, double y) {
        this->x = x;
        this->y = y;
    }
    inline void setVelocity(double x, double y) {
        velX = x;
        velY = y;
    }
    inline void addVelocity(double dx, double dy) {
        velX += dx;
        velY += dy;
    }
    inline void addAccel(double dx, double dy) {
        aX += dx;
        aY += dy;
    }

    inline void setColor(uint8_t r, uint8_t g, uint8_t b) {
        color[0] = r;
        color[1] = g;
        color[2] = b;
    }

    virtual uint8_t type() = 0;
    Player* player = nullptr;
    double x = 0.0, y = 0.0, velX = 0.0, velY = 0.0, aX = 0.0, aXO = 0.0, aY = 0.0, aYO = 0.0, rotation = 0.0, rotateVel = 0.0,
    dVelX = 0.0, dVelY = 0.0, // exist for caching reasons
    radius = 0.0, mass = 0.0,
    syncX = 0.0, syncY = 0.0, syncVelX = 0.0, syncVelY = 0.0;
    bool ghost = false, ai = false, synced = false, active = true, gravitates = false;
    Entity* simRelBody = nullptr;
    unsigned char color[3]{255, 255, 255};
    uint32_t id, parent_id = std::numeric_limits<uint32_t>::max();
};

Entity* idLookup(uint32_t);

struct Quad {
    void collideAttract(Entity* e, bool, bool);
    static void put(uint32_t id, Entity* e, int reclevel);
    Quad& getChild(uint8_t at);
    static uint32_t getMakeChild(uint32_t id, double at_x, double at_y);
    uint32_t unstaircasize();
    void postBuild();

    double size, tX, tY, // the parameters of the quad as if it hadn't been stretched
    xsize, ysize, x, y, comx = 0.0, comy = 0.0, mass = 0.0;
    uint32_t children[4] = {0, 0, 0, 0};
    Entity* entity = nullptr;
    bool used = false, hasGravitators = false;
};

struct Triangle: public Entity {
    Triangle();

    void control(movement& cont) override;

    void loadCreatePacket(Packet& packet) override;
    void loadSyncPacket(Packet& packet) override;

    void onEntityDelete(Entity* d) override;

    uint8_t type() override;
    static double mass, accel, rotateSlowSpeedMult, rotateSpeed, boostCooldown, boostStrength, reload, shootPower, secondaryRegen, secondaryReload, secondaryStockpile, secondaryShootPower, maxSecondaryAngle, slowRotateSpeed;
    double boostProgress = 0.0, reloadProgress = 0.0, secondaryCharge = 0.0, secondaryProgress = 0.0,
    resBoostProgress = 0.0, resReloadProgress = 0.0, resSecondaryCharge = 0.0, resSecondaryProgress = 0.0;

    std::string name = "unnamed";

    Entity* target = nullptr;
};

struct CelestialBody: public Entity {
    CelestialBody(double radius);
    CelestialBody(double radius, double mass);
    CelestialBody(bool ghost);

    void collide(Entity* with, bool collideOther) override;

    void loadCreatePacket(Packet& packet) override;
    void loadSyncPacket(Packet& packet) override;

    uint8_t type() override;
    
    void postMassUpdate();

    bool star = false, blackhole = false;
};

struct Projectile: public Entity {
    Projectile();

    void collide(Entity* with, bool collideOther) override;

    void loadCreatePacket(Packet& packet) override;
    void loadSyncPacket(Packet& packet) override;

    uint8_t type() override;

    static double mass;
};

struct Missile: public Projectile {
    Missile();

    void update2() override;

    void loadCreatePacket(Packet& packet) override;
    void loadSyncPacket(Packet& packet) override;

    void onEntityDelete(Entity* d) override;

    uint8_t type() override;

    Entity* target = nullptr;
    Entity* owner = nullptr;

    static double mass, accel, rotateSpeed, maxThrustAngle, startingFuel, leastItimeDecrease, fullThrustThreshold;
    static int guidanceIterations;
    double fuel,
    resFuel,
    prevItime = 0.0;
    bool thrust = true;
};

struct Player {
    ~Player();

    std::string name();

    Entity* entity = nullptr;

    uint64_t connId = 0; // websocket connection ID
    std::vector<Packet> tcpQueue;
    std::string username = "unnamed", ip = "";
    std::string disconnectReason = "";
    double lastAck = 0.0, lastPingSent = 0.0, lastPingReceived = 0.0, lastSynced = 0.0, lastFullsynced = 0.0, ping = 0.0,
    viewW = 500.0, viewH = 500.0;
    int kills = 0;
    movement controls;
    unsigned short port = 0;
    bool connected = false;
};

}
