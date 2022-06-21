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
	int straferight: 1 = 0;
	int strafeleft: 1 = 0;
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

	Player* player = nullptr;
	double x = 0, y = 0, mass = 0, velX = 0, velY = 0;
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
	double accel = 0.01, rotateSpeed = 2, mass = 1.0;

	std::unique_ptr<sf::CircleShape> shape, forwards;
	float rotation = 0;
};

struct Attractor: public Entity {
	Attractor(float radius);
	Attractor(float radius, double mass);

	void update() override;
	void draw() override;

	void loadCreatePacket(sf::Packet& packet) override;
	void unloadCreatePacket(sf::Packet& packet) override;
	void loadSyncPacket(sf::Packet& packet) override;
	void unloadSyncPacket(sf::Packet& packet) override;

	static const uint8_t type = 1;
	double mass = 100.0;
	float radius;

	std::unique_ptr<sf::CircleShape> shape;
};

struct Player {
	std::string name();

	std::unique_ptr<Entity> entity;

	sf::TcpSocket tcpSocket;
	std::vector<sf::Packet> tcpQueue;
	std::string username = "", ip;
	double lastAck = 0, lastPingSent = 0, lastSynced = 0;
	movement controls;
	unsigned short port;
};

}
