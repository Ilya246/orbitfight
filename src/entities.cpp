#include "entities.hpp"
#include "globals.hpp"
#include "math.hpp"

#include <cmath>

#include <SFML/Graphics.hpp>
#include <SFML/Network.hpp>

namespace obf {

bool operator ==(movement& mov1, movement& mov2) {
	return *(unsigned char*) &mov1 == *(unsigned char*) &mov2;
}

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
	id = nextID;
	nextID++;
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
			despawnPacket << (uint16_t)6 << this->id;
			p->tcpSocket.send(despawnPacket);
		}
	}
}

void Entity::control(movement& cont) {}
void Entity::update() {
	x += velX * delta;
	y += velY * delta;
}

Triangle::Triangle() : Entity() {
	if (!headless) {
		shape = new sf::CircleShape(25, 3);
		shape->setOrigin(25, 25);
		forwards = new sf::CircleShape(8, 3);
		forwards->setOrigin(8, 8);
	} else {
		for (Player* p : playerGroup) {
			sf::Packet packet;
			packet << (uint16_t)1;
			this->loadCreatePacket(packet);
			p->tcpSocket.send(packet);
		}
	}
}
Triangle::~Triangle() {
	delete shape;
}
void Triangle::loadCreatePacket(sf::Packet& packet) {
	packet << type << id << x << y << velX << velY << rotation;
}
void Triangle::unloadCreatePacket(sf::Packet& packet) {
	packet >> id >> x >> y >> velX >> velY >> rotation;
}
void Triangle::loadSyncPacket(sf::Packet& packet) {
	packet << id << x << y << velX << velY << rotation;
}
void Triangle::unloadSyncPacket(sf::Packet& packet) {
	packet >> x >> y >> velX >> velY >> rotation;
}

void Triangle::control(movement& cont) {
	float rotationRad = rotation * degToRad;
	if (cont.forward) {
		addVelocity(accel * std::cos(rotationRad) * delta, accel * std::sin(rotationRad) * delta);
	} else if (cont.backward) {
		addVelocity(-accel * std::cos(rotationRad) * delta, -accel * std::sin(rotationRad) * delta);
	}
	if (cont.turnleft) {
		rotation += rotateSpeed * delta;
	} else if (cont.turnright) {
		rotation -= rotateSpeed * delta;
	}
}

void Triangle::draw() {
	shape->setPosition(x, y);
	shape->setRotation(90.f - rotation);
	window->draw(*shape);
	float rotationRad = rotation * degToRad;
	forwards->setPosition(x + 30.0 * sin(rotationRad), y + 30.0 * cos(rotationRad));
	forwards->setRotation(90.f - rotation);
	window->draw(*forwards);
}

Attractor::Attractor() : Entity() {
	if (!headless) {
		shape = new sf::CircleShape(300, 50);
		shape->setOrigin(300, 300);
	} else {
		for (Player* p : playerGroup) {
			sf::Packet packet;
			packet << (uint16_t)1;
			this->loadCreatePacket(packet);
			p->tcpSocket.send(packet);
		}
	}
}
Attractor::~Attractor() {
	delete shape;
}

void Attractor::loadCreatePacket(sf::Packet& packet) {
	packet << type << id << x << y << velX << velY;
}
void Attractor::unloadCreatePacket(sf::Packet& packet) {
	packet >> id >> x >> y >> velX >> velY;
}
void Attractor::loadSyncPacket(sf::Packet& packet) {
	packet << id << x << y << velX << velY;
}
void Attractor::unloadSyncPacket(sf::Packet& packet) {
	packet >> x >> y >> velX >> velY;
}

void Attractor::update() {
	for (Entity* e : updateGroup) {
		if (e == this) [[unlikely]] {
			continue;
		}
		double xdiff = e->x - x, ydiff = y - e->y;
		double factor = G / pow(xdiff * xdiff + ydiff * ydiff, 1.5);
		double factorthis = factor * e->mass, factore = -factor * mass;
		addVelocity(xdiff * factorthis, ydiff * factorthis);
		e->addVelocity(xdiff * factore, ydiff * factore);
	}
}
void Attractor::draw() {
	shape->setPosition(x, y);
	window->draw(*shape);
}

}
