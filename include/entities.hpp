#pragma once

#include <vector>

#include <SFML/Graphics/CircleShape.hpp>
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

	virtual void control(movement& cont) = 0;
	virtual void update() = 0;
	virtual void draw() = 0;

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
		velY += dy;
	}

	Player* player = nullptr;
	double x, y, velX = 0, velY = 0;
	int id;
};

struct Triangle: public Entity {
	Triangle();
	~Triangle();

	void control(movement& cont) override;
	void update() override;
	void draw() override;

	void loadCreatePacket(sf::Packet& packet) override;
	void unloadCreatePacket(sf::Packet& packet) override;
	void loadSyncPacket(sf::Packet& packet) override;
	void unloadSyncPacket(sf::Packet& packet) override;

	static const unsigned char type = 0;
	const double accel = 0.0001;
	const double rotateSpeed = 0.6;

	sf::CircleShape* shape = nullptr;
	float rotation = 0;
};

struct Player {
	~Player();

	std::string name();

	Entity* entity = nullptr;

	sf::TcpSocket tcpSocket;
	std::vector<sf::Packet> tcpQueue;
	std::string username = "", ip;
	double lastAck, lastPingSent, lastSynced;
	movement controls;
	unsigned short port;
};

}
