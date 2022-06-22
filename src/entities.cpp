#include "camera.hpp"
#include "entities.hpp"
#include "globals.hpp"
#include "math.hpp"
#include "types.hpp"

#include <cmath>
#include <sstream>

#include <SFML/Graphics.hpp>
#include <SFML/Network.hpp>

namespace obf {

bool operator ==(movement& mov1, movement& mov2) {
	return *(unsigned char*) &mov1 == *(unsigned char*) &mov2;
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
Player::~Player() {
	delete this->entity;
}

Entity::Entity() {
	id = nextID;
	nextID++;
	updateGroup.push_back(this);
}

Entity::~Entity() noexcept {
	// swap remove this from updateGroup
	// O(n) complexity since iterates whole thing at worst
	if (debug) {
		printf("Deleting entity id %u\n", this->id);
	}
	for (size_t i = 0; i < updateGroup.size(); i++) {
		if (updateGroup[i] == this) [[unlikely]] {
			updateGroup[i] = updateGroup[updateGroup.size() - 1];
			updateGroup.pop_back();
			break;
		}
	}
	for (Entity* en : near) {
		for (size_t i = 0; i < en->near.size(); i++){
			if (en->near[i] == this) [[unlikely]] {
				en->near[i] = en->near[en->near.size() - 1];
				en->near.pop_back();
				break;
			}
		}
	}

	if (headless) {
		for (Player* p : playerGroup) {
			sf::Packet despawnPacket;
			despawnPacket << Packets::DeleteEntity << this->id;
			p->tcpSocket.send(despawnPacket);
		}
	}
}

void Entity::syncCreation() {
	for (Player* p : playerGroup) {
		sf::Packet packet;
		packet << Packets::CreateEntity;
		this->loadCreatePacket(packet);
		p->tcpSocket.send(packet);
	}
}

void Entity::control(movement& cont) {}
void Entity::update() {
	x += velX * delta;
	y += velY * delta;
	if(globalTime - lastCollideScan > collideScanSpacing) [[unlikely]] {
		size_t i = 0;
		for (Entity* e : updateGroup) {
			if (e == this) [[unlikely]] {
				continue;
			}
			if (dst2((abs(x - e->x) - radius - e->radius), (abs(y - e->y) - radius - e->radius)) / dst2(e->velX - velX, e->velY - velY) < collideScanDistance2) {
				if(i == near.size()) {
					near.push_back(e);
				} else {
					near[i] = e;
				}
				i++;
			}
		}
		if(i < near.size()){
			near.erase(near.begin() + i + 1, near.begin() + near.size());
		}
		lastCollideScan = globalTime;
	}
	for (Entity* e : near) {
		if (dst2(x - e->x, y - e->y) <= (radius + e->radius) * (radius + e->radius)) [[unlikely]] {
			collide(e, true);
		}
	}
}

void Entity::collide(Entity* with, bool collideOther) {
	double dVx = velX - with->velX, dVy = with->velY - velY;
	double inHeading = std::atan2(y - with->y, with->x - x);
	double velHeading = std::atan2(dVy, dVx);
	double massFactor = std::min(with->mass / mass, 1.0);
	double factor = massFactor * std::cos(std::abs(deltaAngleRad(inHeading, velHeading))) * collideRestitution;
	if (factor < 0.0) {
		return;
	}
	if (collideOther) {
		with->collide(this, false);
	}
	double vel = dst(dVx, dVy);
	double inX = std::cos(inHeading), inY = std::sin(inHeading);
	velX -= vel * inX * factor + friction * dVx;
	velY += vel * inY * factor + friction * dVy;
	x = (x + (with->x - (radius + with->radius) * inX) * massFactor) / (1.0 + massFactor);
	y = (y + (with->y + (radius + with->radius) * inY) * massFactor) / (1.0 + massFactor);
}

Triangle::Triangle() : Entity() {
	mass = 1.0;
	radius = 8.0;
	if (!headless) {
		shape = std::make_unique<sf::CircleShape>(radius, 3);
		shape->setOrigin(radius, radius);
		forwards = std::make_unique<sf::CircleShape>(4.f, 3);
		forwards->setOrigin(4.f, 4.f);
	}
}

void Triangle::loadCreatePacket(sf::Packet& packet) {
	packet << type << id << x << y << velX << velY << rotation;
	if(debug){
		printf("Sent id %d: %g %g %g %g\n", id, x, y, velX, velY);
	}
}
void Triangle::unloadCreatePacket(sf::Packet& packet) {
	packet >> id >> x >> y >> velX >> velY >> rotation;
	if(debug){
		printf("Received id %d: %g %g %g %g\n", id, x, y, velX, velY);
	}
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
	if (cont.boost && lastBoosted + boostCooldown < globalTime) {
		addVelocity(boostStrength * std::cos(rotationRad) * delta, boostStrength * std::sin(rotationRad) * delta);
		lastBoosted = globalTime;
	}
}

void Triangle::draw() {
	shape->setPosition(x, y);
	shape->setRotation(90.f - rotation);
	shape->setFillColor(sf::Color(color[0], color[1], color[2]));
	window->draw(*shape);
	g_camera.bindUI();
	float rotationRad = rotation * degToRad;
	if(ownEntity) [[likely]] {
		forwards->setPosition(g_camera.w * 0.5 + (ownEntity->x - x) / g_camera.scale + 14.0 * cos(rotationRad), g_camera.h * 0.5 + (ownEntity->y - y) / g_camera.scale - 14.0 * sin(rotationRad));
	}
	forwards->setRotation(90.f - rotation);
	window->draw(*forwards);
	g_camera.bindWorld();
}

Attractor::Attractor(double radius) : Entity() {
	this->radius = radius;
	this->mass = 100.0;
	if (!headless) {
		shape = std::make_unique<sf::CircleShape>(radius, 50);
		shape->setOrigin(radius, radius);
	}
}
Attractor::Attractor(double radius, double mass) : Entity() {
	this->radius = radius;
	this->mass = mass;
	if (!headless) {
		shape = std::make_unique<sf::CircleShape>(radius, 50);
		shape->setOrigin(radius, radius);
	}
}

void Attractor::loadCreatePacket(sf::Packet& packet) {
	packet << type << radius << id << x << y << velX << velY << mass << color[0] << color[1] << color[2];
	if(debug){
		printf("Sent id %d: %g %g %g %g\n", id, x, y, velX, velY);
	}
}
void Attractor::unloadCreatePacket(sf::Packet& packet) {
	packet >> id >> x >> y >> velX >> velY >> mass >> color[0] >> color[1] >> color[2];
	if(debug){
		printf(", id %d: %g %g %g %g\n", id, x, y, velX, velY);
	}
}
void Attractor::loadSyncPacket(sf::Packet& packet) {
	packet << id << x << y << velX << velY;
}
void Attractor::unloadSyncPacket(sf::Packet& packet) {
	packet >> x >> y >> velX >> velY;
}

void Attractor::update() {
	Entity::update();
	for (Entity* e : updateGroup) {
		if (e == this) [[unlikely]] {
			continue;
		}

		double xdiff = e->x - x, ydiff = y - e->y;
		double factor = -mass * delta * G / pow(xdiff * xdiff + ydiff * ydiff, 1.5);
		e->addVelocity(xdiff * factor, ydiff * factor);
	}
}
void Attractor::draw() {
	shape->setPosition(x, y);
	shape->setFillColor(sf::Color(color[0], color[1], color[2]));
	window->draw(*shape);
}

}
