#include "camera.hpp"
#include "entities.hpp"
#include "globals.hpp"
#include "math.hpp"
#include "types.hpp"

#include <cmath>
#include <iostream>
#include <sstream>
#include <vector>

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
	for (size_t i = 0; i < playerGroup.size(); i++) {
		if (playerGroup[i] == this) [[unlikely]] {
			playerGroup[i] = playerGroup[playerGroup.size() - 1];
			playerGroup.pop_back();
			break;
		}
	}
	sf::Packet chatPacket;
	std::string sendMessage = "";
	sendMessage.append("<").append(name()).append("> has disconnected.");
	std::cout << sendMessage << std::endl;
	chatPacket << Packets::Chat << sendMessage;
	for (Player* p : playerGroup) {
		p->tcpSocket.send(chatPacket);
	}
	entityDeleteBuffer.push_back(entity);
}

Entity::Entity() {
	id = nextID;
	nextID++;
	updateGroup.push_back(this);
	if (!headless) {
		trajectory = std::make_unique<std::vector<Point>>();
	}
}

Entity::~Entity() noexcept {
	// swap remove this from updateGroup
	// O(n) complexity since iterates whole thing at worst
	if (debug) {
		printf("Deleting entity id %u\n", this->id);
	}
	for (size_t i = 0; i < updateGroup.size(); i++) {
		Entity* e = updateGroup[i];
		if (e == this) [[unlikely]] {
			updateGroup[i] = updateGroup[updateGroup.size() - 1];
			updateGroup.pop_back();
		} else {
			for (size_t i = 0; i < e->near.size(); i++){
				if (e->near[i] == this) [[unlikely]] {
					e->near[i] = e->near[e->near.size() - 1];
					e->near.pop_back();
					break;
				}
			}
			if (e->simRelBody == this) [[unlikely]] {
				e->simRelBody = nullptr;
			}
		}
	}

	if (headless) {
		for (Player* p : playerGroup) {
			sf::Packet despawnPacket;
			despawnPacket << Packets::DeleteEntity << this->id;
			p->tcpSocket.send(despawnPacket);
		}
		for (size_t i = 0; i < planets.size(); i++) {
			Entity* e = planets[i];
			if (e == this) [[unlikely]] {
				planets[i] = planets[planets.size() - 1];
				planets.pop_back();
				break;
			}
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
	if(globalTime - lastCollideScan > collideScanSpacing || (simulating && predictingFor - lastCollideScan > collideScanSpacing * 60.0)) [[unlikely]] {
		size_t i = 0;
		for (Entity* e : updateGroup) {
			if (e == this || ((e->ghost || ghost) && type() == Entities::Triangle && e->type() == Entities::Triangle)) [[unlikely]] {
				continue;
			}
			if ((dst2(abs(x - e->x), abs(y - e->y)) - (radius + e->radius) * (radius + e->radius)) / std::max(0.5, dst2(e->velX - velX, e->velY - velY)) < collideScanDistance2) {
				if(i == near.size()) {
					near.push_back(e);
				} else {
					near[i] = e;
				}
				i++;
			}
		}
		if (i < near.size()) {
			near.erase(near.begin() + i + 1, near.begin() + near.size());
		}
		lastCollideScan = simulating ? predictingFor : globalTime;
	}
	for (Entity* e : near) {
		if (dst2(x - e->x, y - e->y) <= (radius + e->radius) * (radius + e->radius)) [[unlikely]] {
			collide(e, true);
			if (type() == Entities::Attractor) {
				if (this == star && e->type() == Entities::Triangle) [[unlikely]] {
					bool found = false;
					for (Player* p : playerGroup) {
						if (p->entity == e) [[unlikely]] {
							sf::Packet chatPacket;
							std::string sendMessage;
							sendMessage.append("<").append(p->name()).append("> has been incinerated by the star.");
							std::cout << sendMessage << std::endl;
							chatPacket << Packets::Chat << sendMessage;
							p->tcpSocket.disconnect();
							delete p;
							for (Player* p : playerGroup) {
								p->tcpSocket.send(chatPacket);
							}
							found = true;
						}
					}
					if (!found && (headless || simulating)) {
						entityDeleteBuffer.push_back(e);
					}
				} else if (e->type() == Entities::Attractor) [[unlikely]] {
					if (debug) {
						printf("Planetary collision: %u, %u\n", id, e->id);
					}
					if (mass > e->mass) {
						double radiusMul = sqrt((mass + e->mass) / mass);
						mass += e->mass;
						radius *= radiusMul;
						if (!headless) {
							if (!simulating) {
								((Attractor*)this)->shape->setRadius(radius);
							}
						} else {
							sf::Packet collisionPacket;
							collisionPacket << Packets::PlanetCollision << id << mass << radius;
							for (Player* p : playerGroup) {
								p->tcpSocket.send(collisionPacket);
							}
						}
						if (headless || simulating) {
							entityDeleteBuffer.push_back(e);
						}
					}
				}
			}
		}
	}
}

void Entity::draw() {
	sf::Color trajColor(color[0], color[1], color[2]);
	if (trajectory && simRelBody && trajectory->size() > 0) [[likely]] {
		std::vector<Point> traj = *trajectory;
		sf::VertexArray lines(sf::LineStrip, traj.size());
		for (size_t i = 0; i < traj.size(); i++){
			lines[i].position = sf::Vector2f(simRelBody->x + traj[i].x, simRelBody->y + traj[i].y);
			lines[i].color = trajColor;
		}
		window->draw(lines);
	}
}

void Entity::collide(Entity* with, bool collideOther) {
	if (debug && dst2(with->velX - velX, with->velY - velY) > 0.1) [[unlikely]] {
		printf("collision: %u-%u\n", id, with->id);
	}
	if (with->type() == Entities::Projectile) {
		return;
	}
	double dVx = velX - with->velX, dVy = with->velY - velY;
	double inHeading = std::atan2(y - with->y, with->x - x);
	double velHeading = std::atan2(dVy, dVx);
	double massFactor = std::min(with->mass / mass, 1.0);
	double factor = massFactor * std::cos(std::abs(deltaAngleRad(inHeading, velHeading))) * collideRestitution;
	if (factor < 0.0) {
		return;
	}
	double vel = dst(dVx, dVy);
	double inX = std::cos(inHeading), inY = std::sin(inHeading);
	velX -= vel * inX * factor + friction * dVx * delta;
	velY += vel * inY * factor + friction * dVy * delta;
	x = (x + (with->x - (radius + with->radius) * inX) * massFactor) / (1.0 + massFactor);
	y = (y + (with->y + (radius + with->radius) * inY) * massFactor) / (1.0 + massFactor);
	if (collideOther) {
		with->collide(this, false);
	}
}

void Entity::simSetup() {
	resX = x;
	resY = y;
	resVelX = velX;
	resVelY = velY;
	resMass = mass;
	resRadius = radius;
	resNear = near;
	resCollideScan = lastCollideScan;
	lastCollideScan = 0.0;
	double maxFactor = 0.0;
	for (Entity* e : updateGroup) {
		if (e == this || e->type() != Entities::Attractor) {
			continue;
		}
		double factor = G * e->mass / dst2(e->x - x, e->y - y);
		if (factor > maxFactor) {
			simRelBody = e;
			maxFactor = factor;
		}
	}
}
void Entity::simReset() {
	x = resX;
	y = resY;
	velX = resVelX;
	velY = resVelY;
	mass = resMass;
	radius = resRadius;
	near = resNear;
	lastCollideScan = resCollideScan;
}

Triangle::Triangle() : Entity() {
	mass = 0.1;
	radius = 8.0;
	if (!headless) {
		shape = std::make_unique<sf::CircleShape>(radius, 3);
		shape->setOrigin(radius, radius);
		forwards = std::make_unique<sf::CircleShape>(2.f, 6);
		forwards->setOrigin(4.f, 4.f);
		icon = std::make_unique<sf::CircleShape>(3.f, 3);
		icon->setOrigin(2.f, 2.f);
		icon->setFillColor(sf::Color(255, 255, 255));
		inertTrajectory = std::make_unique<std::vector<Point>>();
	}
}
Triangle::Triangle(bool ghost) : Entity() {
	mass = 0.1;
	radius = 8.0;
	this->ghost = true;
}

void Triangle::loadCreatePacket(sf::Packet& packet) {
	packet << type() << id << x << y << velX << velY << rotation;
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

void Triangle::simSetup() {
	Entity::simSetup();
	resLastBoosted = lastBoosted;
	resLastShot = lastShot;
	resHyperboostCharge = hyperboostCharge;
	resBurning = burning;
	resRotation = rotation;
}
void Triangle::simReset() {
	Entity::simReset();
	lastBoosted = resLastBoosted;
	lastShot = resLastShot;
	hyperboostCharge = resHyperboostCharge;
	burning = resBurning;
	rotation = resRotation;
}

void Triangle::control(movement& cont) {
	float rotationRad = rotation * degToRad;
	double xMul = std::cos(rotationRad), yMul = std::sin(rotationRad);
	if (cont.hyperboost || burning) {
		hyperboostCharge += delta * (burning ? -2 : 1);
		hyperboostCharge = std::min(hyperboostCharge, 2.0 * hyperboostTime);
		burning = hyperboostCharge > hyperboostTime && (burning || (cont.boost && hyperboostCharge > minAfterburn));
		if (burning) {
			addVelocity(afterburnStrength * xMul * delta, afterburnStrength * yMul * delta);
			if (!headless) {
				forwards->setFillColor(sf::Color(196, 32, 255));
				forwards->setRotation(90.f - rotation);
			}
			return;
		}
		if (cont.turnleft) {
			rotation += rotateSpeed * delta * hyperboostTurnMult;
		} else if (cont.turnright) {
			rotation -= rotateSpeed * delta * hyperboostTurnMult;
		}
		if (hyperboostCharge > hyperboostTime) {
			addVelocity(hyperboostStrength * xMul * delta, hyperboostStrength * yMul * delta);
			if (!headless) {
				forwards->setFillColor(sf::Color(64, 64, 255));
				forwards->setRotation(90.f - rotation);
			}
		} else if (!headless) {
			forwards->setFillColor(sf::Color(255, 255, 0));
			forwards->setRotation(90.f - rotation);
		}
		return;
	} else {
		hyperboostCharge = 0.0;
	}
	if (cont.forward) {
		addVelocity(accel * xMul * delta, accel * yMul * delta);
		if (!headless) {
			forwards->setFillColor(sf::Color(255, 196, 0));
			forwards->setRotation(90.f - rotation);
		}
	} else if (cont.backward) {
		addVelocity(-accel * xMul * delta, -accel * yMul * delta);
		if (!headless) {
			forwards->setFillColor(sf::Color(255, 64, 64));
			forwards->setRotation(270.f - rotation);
		}
	} else if (!headless) {
		forwards->setFillColor(sf::Color::White);
		forwards->setRotation(90.f - rotation);
	}
	if (cont.turnleft) {
		rotation += rotateSpeed * delta;
	} else if (cont.turnright) {
		rotation -= rotateSpeed * delta;
	}
	if (cont.boost && lastBoosted + boostCooldown < globalTime) {
		addVelocity(boostStrength * xMul, boostStrength * yMul);
		lastBoosted = globalTime;
		if (!headless) {
			forwards->setFillColor(sf::Color(64, 255, 64));
			forwards->setRotation(90.f - rotation);
		}
	}
	if (cont.primaryfire && lastShot + reload < globalTime) {
		if (headless) {
			Projectile* proj = new Projectile();
			proj->setPosition(x + (radius + proj->radius * 2.0) * xMul, y - (radius + proj->radius * 2.0) * yMul);
			proj->setVelocity(velX + shootPower * xMul, velY - shootPower * yMul);
			addVelocity(-shootPower * xMul * proj->mass / mass, -shootPower * yMul * proj->mass / mass);
			proj->syncCreation();
		}
		lastShot = globalTime;
	}
}

void Triangle::draw() {
	Entity::draw();
	if (inertTrajectory && simRelBody && inertTrajectory->size() > 0) [[likely]] {
		sf::Color trajColor((unsigned char)(color[0] * 0.7), (unsigned char)(color[1] * 0.7), (unsigned char)(color[2] * 0.7));
		std::vector<Point> traj = *inertTrajectory;
		sf::VertexArray lines(sf::LineStrip, traj.size());
		for (size_t i = 0; i < traj.size(); i++){
			lines[i].position = sf::Vector2f(simRelBody->x + traj[i].x, simRelBody->y + traj[i].y);
			lines[i].color = trajColor;
		}
		window->draw(lines);
	}
	shape->setPosition(x, y);
	shape->setRotation(90.f - rotation);
	shape->setFillColor(sf::Color(color[0], color[1], color[2]));
	window->draw(*shape);
	if(ownEntity) [[likely]] {
		g_camera.bindUI();
		float rotationRad = rotation * degToRad;
		double uiX = g_camera.w * 0.5 + (x - ownEntity->x) / g_camera.scale, uiY = g_camera.h * 0.5 + (y - ownEntity->y) / g_camera.scale;
		forwards->setPosition(uiX + 14.0 * cos(rotationRad), uiY - 14.0 * sin(rotationRad));
		if (ownEntity == this) {
			float reloadProgress = ((lastShot - globalTime) / reload + 1.0) * 40.f,
			boostProgress = ((lastBoosted - globalTime) / boostCooldown + 1.0) * 40.f;
			if (reloadProgress > 0.0) {
				sf::RectangleShape reloadBar(sf::Vector2f(reloadProgress, 4.f));
				reloadBar.setFillColor(sf::Color(255, 64, 64));
				reloadBar.setPosition(g_camera.w * 0.5f - reloadProgress / 2.f, g_camera.h * 0.5f + 40.f);
				window->draw(reloadBar);
			}
			if (boostProgress > 0.0) {
				sf::RectangleShape boostReloadBar(sf::Vector2f(boostProgress, 4.f));
				boostReloadBar.setFillColor(sf::Color(64, 255, 64));
				boostReloadBar.setPosition(g_camera.w * 0.5f - boostProgress / 2.f, g_camera.h * 0.5f - 40.f);
				window->draw(boostReloadBar);
			}
			if (hyperboostCharge > 0.0) {
				float hyperboostProgress = (1.0 - hyperboostCharge / hyperboostTime) * 40.f;
				if (hyperboostProgress > 0.0) {
					sf::RectangleShape hyperboostBar(sf::Vector2f(hyperboostProgress, 4.f));
					hyperboostBar.setFillColor(sf::Color(64, 64, 255));
					hyperboostBar.setPosition(g_camera.w * 0.5f - hyperboostProgress / 2.f, g_camera.h * 0.5f + 36.f);
					window->draw(hyperboostBar);
				}
				if (hyperboostProgress < 0.0) {
					sf::RectangleShape hyperboostBar(sf::Vector2f(-hyperboostProgress, 4.f));
					hyperboostBar.setFillColor(sf::Color(255, 255, 64));
					hyperboostBar.setPosition(g_camera.w * 0.5f + hyperboostProgress / 2.f, g_camera.h * 0.5f + 36.f);
					window->draw(hyperboostBar);
				}
			}
		}
		window->draw(*forwards);
		if (!name.empty()) {
			sf::Text nameText;
			nameText.setFont(*font);
			nameText.setString(name);
			nameText.setCharacterSize(8);
			nameText.setFillColor(sf::Color::White);
			nameText.setPosition(uiX - nameText.getLocalBounds().width / 2.0, uiY - 28.0);
			window->draw(nameText);
		}
		if (g_camera.scale * 2.0 > radius) {
			icon->setPosition(uiX, uiY);
			window->draw(*icon);
		}
		g_camera.bindWorld();
	}
}

uint8_t Triangle::type() {
	return Entities::Triangle;
}

Attractor::Attractor(double radius) : Entity() {
	this->radius = radius;
	this->mass = 100.0;
	if (!headless) {
		shape = std::make_unique<sf::CircleShape>(radius, 50);
		shape->setOrigin(radius, radius);
		icon = std::make_unique<sf::CircleShape>(2.f, 6);
		icon->setOrigin(2.f, 2.f);
	}
}
Attractor::Attractor(double radius, double mass) : Entity() {
	this->radius = radius;
	this->mass = mass;
	if (!headless) {
		shape = std::make_unique<sf::CircleShape>(radius, 50);
		shape->setOrigin(radius, radius);
		icon = std::make_unique<sf::CircleShape>(2.f, 6);
		icon->setOrigin(2.f, 2.f);
	}
}

void Attractor::loadCreatePacket(sf::Packet& packet) {
	packet << type() << radius << id << x << y << velX << velY << mass << color[0] << color[1] << color[2];
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
		double factor = delta * G / pow(xdiff * xdiff + ydiff * ydiff, 1.5);
		double factorm = -factor * mass;
		e->addVelocity(xdiff * factorm, ydiff * factorm);
		if (e->type() != Entities::Attractor) {
			double factortm = factor * e->mass;
			addVelocity(xdiff * factortm, ydiff * factortm);
		}
	}
}
void Attractor::draw() {
	Entity::draw();
	shape->setPosition(x, y);
	shape->setFillColor(sf::Color(color[0], color[1], color[2]));
	window->draw(*shape);
	if (g_camera.scale > radius && ownEntity) {
		g_camera.bindUI();
		icon->setPosition(g_camera.w * 0.5 + (x - ownEntity->x) / g_camera.scale, g_camera.h * 0.5 + (y - ownEntity->y) / g_camera.scale);
		icon->setFillColor(sf::Color(color[0], color[1], color[2]));
		window->draw(*icon);
		g_camera.bindWorld();
	}
}

uint8_t Attractor::type() {
	return Entities::Attractor;
}

Projectile::Projectile() : Entity() {
	this->radius = 4;
	this->mass = 0.02;
	this->color[0] = 180;
	this->color[1] = 0;
	this->color[2] = 0;
	if (!headless) {
		shape = std::make_unique<sf::CircleShape>(radius, 10);
		shape->setOrigin(radius, radius);
		icon = std::make_unique<sf::CircleShape>(2.f, 4);
		icon->setOrigin(2.f, 2.f);
		icon->setFillColor(sf::Color(255, 0, 0));
		icon->setRotation(45.f);
	}
}

void Projectile::loadCreatePacket(sf::Packet& packet) {
	packet << type() << id << x << y << velX << velY;
	if(debug){
		printf("Sent id %d: %g %g %g %g\n", id, x, y, velX, velY);
	}
}
void Projectile::unloadCreatePacket(sf::Packet& packet) {
	packet >> id >> x >> y >> velX >> velY;
	if(debug){
		printf(", id %d: %g %g %g %g\n", id, x, y, velX, velY);
	}
}
void Projectile::loadSyncPacket(sf::Packet& packet) {
	packet << id << x << y << velX << velY;
}
void Projectile::unloadSyncPacket(sf::Packet& packet) {
	packet >> x >> y >> velX >> velY;
}

void Projectile::collide(Entity* with, bool collideOther) {
	if (debug) {
		printf("bullet collision: %u-%u ", id, with->id);
	}
	if (with->type() == Entities::Triangle) {
		if (debug) {
			printf("of type triangle\n");
		}
		if (headless) {
			for (Player* p : playerGroup) {
				if (p->entity == with) [[unlikely]] {
					sf::Packet chatPacket;
					std::string sendMessage;
					sendMessage.append("<").append(p->name()).append("> has been killed.");
					std::cout << sendMessage << std::endl;
					chatPacket << Packets::Chat << sendMessage;
					p->tcpSocket.disconnect();
					delete p;
					for (Player* p : playerGroup) {
						p->tcpSocket.send(chatPacket);
					}
				}
			}
		}
		if (headless || simulating) {
			entityDeleteBuffer.push_back(this);
		}
	} else if (with->type() == Entities::Attractor) {
		if (debug) {
			printf("of type attractor\n");
		}
		if (headless || simulating) {
			entityDeleteBuffer.push_back(this);
		}
	} else {
		if (debug) {
			printf("of unaccounted type\n");
		}
		Entity::collide(with, collideOther);
	}
}

void Projectile::draw() {
	Entity::draw();
	shape->setPosition(x, y);
	shape->setFillColor(sf::Color(color[0], color[1], color[2]));
	window->draw(*shape);
	if (g_camera.scale > radius && ownEntity) {
		g_camera.bindUI();
		icon->setPosition(g_camera.w * 0.5 + (x - ownEntity->x) / g_camera.scale, g_camera.h * 0.5 + (y - ownEntity->y) / g_camera.scale);
		window->draw(*icon);
		g_camera.bindWorld();
	}
}

uint8_t Projectile::type() {
	return Entities::Projectile;
}

}
