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

void Entity::update() {
	x += velX * delta;
	y += velY * delta;
}

Triangle::Triangle() : Entity() {
	if(!headless){
		shape = new sf::CircleShape(25, 3);
		shape->setOrigin(25, 25);
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

void Triangle::update(){};
void Triangle::control(movement& cont){
	float rotationRad = rotation * degToRad;
	if(cont.forward){
		addVelocity(accel * std::cos(rotationRad) * delta, accel * std::sin(rotationRad) * delta);
	}else if(cont.backward){
		addVelocity(-accel * std::cos(rotationRad) * delta, -accel * std::sin(rotationRad) * delta);
	}
	if(cont.turnleft){
		rotation -= rotateSpeed * delta;
	}else if(cont.turnright){
		rotation += rotateSpeed * delta;
	}
}

void Triangle::draw() {
	shape->setPosition(x, y);
	shape->setRotation(90.f - rotation);
	window->draw(*shape);
}

}
