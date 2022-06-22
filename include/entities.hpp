#pragma once

#include <vector>

#include <SFML/Graphics/CircleShape.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Network.hpp>

namespace obf {

struct Player;

struct movement {
	int forward: 1 = 0;
	int backward: 1 = 0;
	int turnright: 1 = 0;
	int turnleft: 1 = 0;
	int boost: 1 = 0;
	int unknown: 1 = 0;
	int primaryfire: 1 = 0;
	int secondaryfire: 1 = 0;
};

bool operator ==(movement& mov1, movement& mov2);

struct Entity {
	Entity();
	virtual ~Entity() noexcept;

	virtual void control(movement& cont);
	virtual void update();
	virtual void draw() = 0;

	virtual void collide(Entity* with, bool collideOther);

	std::vector<Entity*> near;

	virtual void syncCreation();

	virtual void loadCreatePacket(sf::Packet& packet) = 0;
	virtual void unloadCreatePacket(sf::Packet& packet) = 0;
	virtual void loadSyncPacket(sf::Packet& packet) = 0;
	virtual void unloadSyncPacket(sf::Packet& packet) = 0;

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
		velY -= dy;
	}

	inline void setColor(uint8_t r, uint8_t g, uint8_t b) {
		color[0] = r;
		color[1] = g;
		color[2] = b;
	}


	virtual uint8_t type() = 0;
	Player* player = nullptr;
	double x = 0.0, y = 0.0, velX = 0.0, velY = 0.0, radius = 0.0,
	mass = 0.0,
	lastCollideCheck = 0.0, lastCollideScan = 0.0;
	unsigned char color[3]{255, 255, 255};
	uint32_t id;
};

struct Triangle: public Entity {
	Triangle();

	void control(movement& cont) override;
	void draw() override;

	void loadCreatePacket(sf::Packet& packet) override;
	void unloadCreatePacket(sf::Packet& packet) override;
	void loadSyncPacket(sf::Packet& packet) override;
	void unloadSyncPacket(sf::Packet& packet) override;

	uint8_t type() override;
	double accel = 6, rotateSpeed = 2.0, boostCooldown = 15.0, boostStrength = 2.0, reload = 6.0, shootPower = 2.0,
	lastBoosted = -boostCooldown, lastShot = -reload;

	std::string name = "";

	std::unique_ptr<sf::CircleShape> shape, forwards;
	float rotation = 0;
};

struct Attractor: public Entity {
	Attractor(double radius);
	Attractor(double radius, double mass);

	void update() override;
	void draw() override;

	void loadCreatePacket(sf::Packet& packet) override;
	void unloadCreatePacket(sf::Packet& packet) override;
	void loadSyncPacket(sf::Packet& packet) override;
	void unloadSyncPacket(sf::Packet& packet) override;

	uint8_t type() override;

	std::unique_ptr<sf::CircleShape> shape;
};

struct Projectile: public Entity {
	Projectile();

	void draw() override;

	void collide(Entity* with, bool collideOther) override;

	void loadCreatePacket(sf::Packet& packet) override;
	void unloadCreatePacket(sf::Packet& packet) override;
	void loadSyncPacket(sf::Packet& packet) override;
	void unloadSyncPacket(sf::Packet& packet) override;

	uint8_t type() override;

	std::unique_ptr<sf::CircleShape> shape, icon;
};

struct Player {
	~Player();

	std::string name();

	Entity* entity = nullptr;

	sf::TcpSocket tcpSocket;
	std::vector<sf::Packet> tcpQueue;
	std::string username = "", ip;
	double lastAck = 0.0, lastPingSent = 0.0, lastSynced = 0.0, ping = 0.0,
	viewW = 500.0, viewH = 500.0;
	movement controls;
	unsigned short port;
};

}
