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

	static const uint8_t type = 0;
	double accel = 0.01, rotateSpeed = 2.0, boostCooldown = 600.0, boostStrength = 0.6,
	lastBoosted = 0.0;

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

	static const uint8_t type = 1;

	std::unique_ptr<sf::CircleShape> shape;
};

struct Player {
	~Player();

	std::string name();

	Entity* entity = nullptr;

	sf::TcpSocket tcpSocket;
	std::vector<sf::Packet> tcpQueue;
	std::string username = "", ip;
	double lastAck = 0, lastPingSent = 0, lastSynced = 0, ping;
	movement controls;
	unsigned short port;
};

}
