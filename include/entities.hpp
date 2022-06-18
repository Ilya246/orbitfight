#pragma once

#include <vector>

#include <SFML/Graphics/CircleShape.hpp>
#include <SFML/Network.hpp>

namespace obf{

struct Player{
	std::string name();

	sf::TcpSocket tcpSocket;
	std::vector<sf::Packet> tcpQueue;
	std::string ip;
	double lastAck, lastPingSent;
	unsigned short port;
};

struct Entity{
	Entity(double x, double y);
	virtual ~Entity() noexcept;

	virtual void update();
	virtual void draw() = 0;

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

	double x, y, velX = 0, velY = 0;
};

struct Triangle: public Entity{
	Triangle(double x, double y, double radius);

	void update() override;
	void draw() override;

	sf::CircleShape shape;
	float rotation = 0;
};

}
