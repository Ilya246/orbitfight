#pragma once
#include "events.hpp"
#include "math.hpp"

#include <limits>
#include <memory>
#include <vector>

#include <SFML/Graphics/CircleShape.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Network.hpp>

namespace obf {

struct Player;

struct Entity;

void setupShip(Entity* ship, bool sync);

void generateSystem();

void fullClear(bool clearTriangles);

void updateEntities();

void reallocateQuadtree();
void buildQuadtree();

struct movement {
	int forward: 1 = 0;
	int backward: 1 = 0;
	int turnright: 1 = 0;
	int turnleft: 1 = 0;
	int boost: 1 = 0;
	int hyperboost: 1 = 0;
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
	virtual void draw();

	virtual void collide(Entity* with, bool collideOther);

	std::vector<uint32_t> collided;

	void syncCreation();

	virtual void loadCreatePacket(sf::Packet& packet) = 0;
	virtual void unloadCreatePacket(sf::Packet& packet) = 0;
	virtual void loadSyncPacket(sf::Packet& packet) = 0;
	virtual void unloadSyncPacket(sf::Packet& packet) = 0;

	virtual void simSetup();
	virtual void simReset();

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

	inline void setColor(uint8_t r, uint8_t g, uint8_t b) {
		color[0] = r;
		color[1] = g;
		color[2] = b;
	}

	std::unique_ptr<sf::CircleShape> icon;

	std::vector<Point> trajectory;

	virtual uint8_t type() = 0;
	Player* player = nullptr;
	double x = 0.0, y = 0.0, velX = 0.0, velY = 0.0, rotation = 0.0, rotateVel = 0.0,
	dVelX = 0.0, dVelY = 0.0, // exist for caching reasons
	radius = 0.0,
	mass = 0.0,
	resX = 0.0, resY = 0.0, resVelX = 0.0, resVelY = 0.0, resRotation = 0.0, resRotateVel = 0.0, resMass = 0.0, resRadius = 0.0,
	syncX = 0.0, syncY = 0.0, syncVelX = 0.0, syncVelY = 0.0;
	bool ghost = false, ai = false, synced = false, active = true;
	Entity* simRelBody = nullptr;
	unsigned char color[3]{255, 255, 255};
	uint32_t id, parent_id = std::numeric_limits<uint32_t>::max();
};

Entity* idLookup(uint32_t);

struct Quad {
	void collideAttract(Entity* e, bool, bool);
	void put(Entity* e);
	Quad& getChild(uint8_t at);
	uint32_t unstaircasize();
	void postBuild();

	void draw();

	uint32_t children[4] = {0, 0, 0, 0};
	Entity* entity = nullptr;
	double size, invsize, x, y, comx = 0.0, comy = 0.0, mass = 0.0;
	bool used = false;
};

struct Triangle: public Entity {
	Triangle();

	void control(movement& cont) override;
	void draw() override;

	void loadCreatePacket(sf::Packet& packet) override;
	void unloadCreatePacket(sf::Packet& packet) override;
	void loadSyncPacket(sf::Packet& packet) override;
	void unloadSyncPacket(sf::Packet& packet) override;

	void simSetup() override;
	void simReset() override;

	void onEntityDelete(Entity* d) override;

	uint8_t type() override;
	static double mass, accel, rotateSlowSpeedMult, rotateSpeed, boostCooldown, boostStrength, reload, shootPower, hyperboostStrength, hyperboostTime, hyperboostRotateSpeed, afterburnStrength, minAfterburn;
	double boostProgress = 0.0, reloadProgress = 0.0, hyperboostCharge = 0.0,
	resBoostProgress = 0.0, resReloadProgress = 0.0, resHyperboostCharge = 0.0;

	bool burning = false, resBurning;
	std::string name = "unnamed";

	Entity* target = nullptr;

	std::unique_ptr<sf::CircleShape> shape, forwards;
};

struct CelestialBody: public Entity {
	CelestialBody(double radius);
	CelestialBody(double radius, double mass);
	CelestialBody(bool ghost);

	void draw() override;

	void collide(Entity* with, bool collideOther) override;

	void loadCreatePacket(sf::Packet& packet) override;
	void unloadCreatePacket(sf::Packet& packet) override;
	void loadSyncPacket(sf::Packet& packet) override;
	void unloadSyncPacket(sf::Packet& packet) override;

	uint8_t type() override;
	
	void postMassUpdate();

	bool star = false, blackhole = false;

	std::unique_ptr<sf::CircleShape> shape, warning;
};

struct Projectile: public Entity {
	Projectile();

	void update2() override;

	void draw() override;

	void collide(Entity* with, bool collideOther) override;

	void loadCreatePacket(sf::Packet& packet) override;
	void unloadCreatePacket(sf::Packet& packet) override;
	void loadSyncPacket(sf::Packet& packet) override;
	void unloadSyncPacket(sf::Packet& packet) override;
	
	void simSetup() override;
	void simReset() override;

	void onEntityDelete(Entity* d) override;

	uint8_t type() override;

	Entity* target = nullptr;
	Entity* owner = nullptr;

	static double mass, accel, rotateSpeed, maxThrustAngle, easeInFactor, startingFuel;
	double fuel,
	resFuel;

	std::unique_ptr<sf::CircleShape> shape, warning;
};

struct Player {
	~Player();

	std::string name();

	Entity* entity = nullptr;

	sf::TcpSocket tcpSocket;
	std::vector<sf::Packet> tcpQueue;
	std::string username = "unnamed", ip = "";
	double lastAck = 0.0, lastPingSent = 0.0, lastSynced = 0.0, lastFullsynced = 0.0, ping = 0.0,
	viewW = 500.0, viewH = 500.0;
	int kills = 0;
	movement controls;
	unsigned short port = 0;
};

}
