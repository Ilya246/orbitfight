#pragma once

#include <vector>

#include <SFML/Graphics/CircleShape.hpp>
#include <SFML/Network.hpp>

namespace obf {

struct Player;

struct Entity {
	Entity();
	Entity(double x, double y);
	virtual ~Entity() noexcept;

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
	long long id;
};

struct Triangle: public Entity {
	Triangle();

	void update() override;
	void draw() override;

	void loadCreatePacket(sf::Packet& packet) override;
	void unloadCreatePacket(sf::Packet& packet) override;
	void loadSyncPacket(sf::Packet& packet) override;
	void unloadSyncPacket(sf::Packet& packet) override;

	static const unsigned char type = 0;

	sf::CircleShape shape;
	float rotation = 0;
};

struct Player {
	~Player();

	std::string name();

	Entity* entity = nullptr;

	sf::TcpSocket tcpSocket;
	std::vector<sf::Packet> tcpQueue;
	std::string username = "", ip;
	double lastAck, lastPingSent, lastSynced, mouseX, mouseY;
	unsigned short port;
};

}
