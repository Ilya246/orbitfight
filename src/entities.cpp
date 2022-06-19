#include "entities.hpp"
#include "globals.hpp"
#include "math.hpp"

#include <cmath>

#include <SFML/Graphics.hpp>
#include <SFML/Network.hpp>

namespace obf {

Player::~Player() {
	delete this->entity;
}

std::string Player::name() {
	if (username.size()) return username;

	std::string ret;
	ret.append(ip);
	if (port != 0) {
		ret.append(":").append(std::to_string(port));
	}

	return ret;
}

Entity::Entity() {
	updateGroup.push_back(this);
}
Entity::Entity(double x, double y)
		: x(x), y(y) {
	updateGroup.push_back(this);
}

Entity::~Entity() noexcept {
	// swap remove this from updateGroup
	// O(n) complexity since iterates whole thing at worst
	for (size_t i = 0; i < updateGroup.size(); i++) {
		if (updateGroup[i] == this) [[unlikely]]{
			updateGroup[i] = updateGroup[updateGroup.size() - 1];
			updateGroup.pop_back();
			break;
		}
	}

	if (headless) {
		for (Player* p : playerGroup) {
			if (!p->entity) continue;
			sf::Packet despawnPacket;
			despawnPacket << (uint16_t)6 << reinterpret_cast<long long>(p->entity);
			p->tcpSocket.send(despawnPacket);
		}
	}
}

void Entity::update() {
	x += velX * delta;
	y += velY * delta;
}

Triangle::Triangle() : Entity() {
	shape = sf::CircleShape(25, 3);
	shape.setOrigin(25, 25);
}
void Triangle::loadCreatePacket(sf::Packet& packet) {
	packet << type << reinterpret_cast<long long>(this) << x << y << velX << velY << rotation;
}
void Triangle::unloadCreatePacket(sf::Packet& packet) {
	packet >> id >> x >> y >> velX >> velY >> rotation;
}
void Triangle::loadSyncPacket(sf::Packet& packet) {
	packet << reinterpret_cast<long long>(this) << x << y << velX << velY << rotation;
}
void Triangle::unloadSyncPacket(sf::Packet& packet) {
	packet >> x >> y >> velX >> velY >> rotation;
}

void Triangle::update() {
	if (player) {
		addVelocity(std::max(-500, (int)std::min((player->mouseX - x), 500.0)) * delta * 0.00001, std::max(-500, (int)std::min((player->mouseY - y), 500.0)) * delta * 0.00001);
		rotation = lerpRotation(rotation, (float)atan2(player->mouseY - y, player->mouseX - x) * radToDeg, delta * 0.1f);
	}

	Entity::update();
	shape.setPosition(x, y);
	shape.setRotation(90.f - rotation);
}
void Triangle::draw() {
	window->draw(shape);
}

}
