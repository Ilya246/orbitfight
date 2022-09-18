#include "camera.hpp"
#include "entities.hpp"
#include "globals.hpp"
#include "math.hpp"
#include "net.hpp"
#include "types.hpp"

#include <cmath>
#include <cstring>
#include <exception>
#include <iostream>
#include <sstream>
#include <vector>

#include <SFML/Graphics.hpp>
#include <SFML/Network.hpp>

namespace obf {

bool operator ==(movement& mov1, movement& mov2) {
	return *(unsigned char*) &mov1 == *(unsigned char*) &mov2;
}

void setupShip(Entity* ship) {
	CelestialBody* planet = planets[(int)rand_f(0, planets.size())];
	double spawnDst = planet->radius + rand_f(2000.f, 6000.f);
	float spawnAngle = rand_f(-PI, PI);
	ship->setPosition(planet->x + spawnDst * std::cos(spawnAngle), planet->y + spawnDst * std::sin(spawnAngle));
	double vel = sqrt(G * planet->mass / spawnDst);
	ship->setVelocity(planet->velX + vel * std::cos(spawnAngle + PI / 2.0), planet->velY + vel * std::sin(spawnAngle + PI / 2.0));
}

int generateOrbitingPlanets(int amount, double x, double y, double velx, double vely, double parentmass, double minradius, double maxradius, double spawnDst) {
	int totalMoons = 0;
	double maxFactor = sqrt(pow(gen_minNextRadius * gen_maxNextRadius, amount * 0.5) * spawnDst);
	for (int i = 0; i < amount; i++) {
		spawnDst *= rand_f(gen_minNextRadius, gen_maxNextRadius);
		double factor = sqrt(spawnDst) / maxFactor; // makes planets further outward generate larger
		float spawnAngle = rand_f(-PI, PI);
		float radius = rand_f(minradius, maxradius * factor);
		double density = gen_baseDensity / pow(radius, 1.0 / 3.0); // makes smaller planets denser
		CelestialBody* planet = new CelestialBody(radius, radius * radius * density);
		planet->setPosition(x + spawnDst * std::cos(spawnAngle), y + spawnDst * std::sin(spawnAngle));
		double vel = sqrt(G * parentmass / spawnDst);
		planet->addVelocity(velx + vel * std::cos(spawnAngle + PI / 2.0), vely + vel * std::sin(spawnAngle + PI / 2.0));
		planet->setColor((int)rand_f(64.f, 255.f), (int)rand_f(64.f, 255.f), (int)rand_f(64.f, 255.f));
		int moons = (int)(rand_f(0.f, 1.f) * radius * radius / (gen_moonFactor * gen_moonFactor));
		obf::planets.push_back(planet);
		totalMoons += moons + generateOrbitingPlanets(moons, planet->x, planet->y, planet->velX, planet->velY, planet->mass, gen_minMoonRadius, planet->radius * gen_maxMoonRadiusFrac, planet->radius * (1.0 + rand_f(gen_minMoonDistance, gen_minMoonDistance + pow(gen_maxMoonDistance, std::min(1.0, 0.5 / (planet->radius / gen_maxPlanetRadius))))));
	}
	return totalMoons;
}

void generateSystem() {
	int starsN = 1;
	while (rand_f(0.f, 1.f) < gen_extraStarChance) {
		starsN += 1;
	}
	double angleSpacing = TAU / starsN, angle = 0.0;
	double starsMass = gen_starMass * starsN, dist = (starsN - 1) * gen_starRadius * 2.0;
	for (int i = 0; i < starsN; i++) {
		CelestialBody* star = nullptr;
		double posX = std::cos(angle) * dist, posY = std::sin(angle) * dist;
		if (rand_f(0.f, 1.f) < gen_blackholeChance) {
			star = new CelestialBody(2.0 * G * gen_starMass / (CC), gen_starMass * 1.0001);
			star->setColor(0, 0, 0);
			star->blackhole = true;
		} else {
			star = new CelestialBody(gen_starRadius, gen_starMass);
			star->setColor(255, 229, 97);
		}
		star->setPosition(posX, posY);
		star->star = true;
		stars.push_back(star);
		angle += angleSpacing;
	}
	if (starsN > 1) {
		double aX = 0.0, aY = 0.0;
		for (int i = 1; i < starsN; i++) {
			double xdiff = stars[i]->x - stars[0]->x, ydiff = stars[i]->y - stars[0]->y,
			factor = stars[i]->mass * G / pow(xdiff * xdiff + ydiff * ydiff, 1.5);
			aX += factor * xdiff;
			aY += factor * ydiff;
		}
		double vel = sqrt(dst(aX, aY) * dist);
		angle = 0.0;
		for (int i = 0; i < starsN; i++) {
			stars[i]->addVelocity(vel * std::cos(angle + PI / 2.0), vel * std::sin(angle + PI / 2.0));
			angle += angleSpacing;
		}
	}
	double spawnDst = 120000.0 + starsN * gen_starRadius * 2.0 * rand_f(1.f, 1.5f);
	int planets = (int)(rand_f(gen_baseMinPlanets, gen_baseMaxPlanets) * sqrt(starsN));
	printf("Generated system: %u stars, %u planets, %u moons\n", starsN, planets, generateOrbitingPlanets(planets, 0.0, 0.0, 0.0, 0.0, starsMass, gen_minPlanetRadius, gen_maxPlanetRadius, spawnDst));
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
	entity->active = false;
}

Entity::Entity() {
	id = nextID;
	nextID++;
	updateGroup.push_back(this);
	if (!headless) {
		ghost = simulating;
	}
}

Entity::~Entity() noexcept {
	if (debug) {
		printf("Deleting entity id %u\n", this->id);
	}
	if (!fullclearing) {
		for (size_t i = 0; i < updateGroup.size(); i++) {
			Entity* e = updateGroup[i];
			if (e == this) [[unlikely]] {
				updateGroup[i] = updateGroup[updateGroup.size() - 1];
				updateGroup.pop_back();
			} else {
				if (e->simRelBody == this) [[unlikely]] {
					e->simRelBody = nullptr;
				}
				if (e->type() == Entities::Projectile && ((Projectile*)e)->owner == this) {
					((Projectile*)e)->owner = nullptr;
				}
			}
		}
	}
	if (!fullclearing) {
		for (size_t i = 0; i < stars.size(); i++) {
			Entity* e = stars[i];
			if (e == this) [[unlikely]] {
				stars[i] = stars[stars.size() - 1];
				stars.pop_back();
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
		if (!fullclearing) {
			for (size_t i = 0; i < planets.size(); i++) {
				Entity* e = planets[i];
				if (e == this) [[unlikely]] {
					planets[i] = planets[planets.size() - 1];
					planets.pop_back();
					break;
				}
			}
		}
	} else {
		if (this == lastTrajectoryRef) {
			lastTrajectoryRef = nullptr;
		}
		if (this == trajectoryRef) {
			trajectoryRef = nullptr;
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
void Entity::update1() {
	x += velX * delta;
	y += velY * delta;
	rotation += rotateVel * delta;
	collided.clear();
	attracted.clear();
}
void Entity::update2() {
	quadtree[0].collideAttract(this, true, true);
}

void Entity::draw() {
	sf::Color trajColor(color[0], color[1], color[2]);
	if (lastTrajectoryRef && trajectory.size() > trajectoryOffset) [[likely]] {
		size_t to = trajectory.size() - trajectoryOffset;
		sf::VertexArray lines(sf::LineStrip, to);
		float lastAlpha = 255;
		float decBy = (255.f - 64.f) / (to);
		for (size_t i = 0; i < to; i++){
			Point point = trajectory[i + trajectoryOffset];
			lines[i].position = sf::Vector2f(lastTrajectoryRef->x + point.x + drawShiftX, lastTrajectoryRef->y + point.y + drawShiftY);
			lines[i].color = trajColor;
			lines[i].color.a = (uint8_t)lastAlpha;
			lastAlpha -= decBy;
		}
		window->draw(lines);
	}
}

void Entity::collide(Entity* with, bool collideOther) {
	if (debug && !simulating && dst2(with->velX - velX, with->velY - velY) > 0.1) [[unlikely]] {
		printf("collision: %u-%u\n", id, with->id);
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
	velX -= vel * inX * factor + massFactor * friction * delta * dVx;
	velY += vel * inY * factor + massFactor * friction * delta * dVy;
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
	resRotation = rotation;
	resRotateVel = rotateVel;
	resMass = mass;
	resRadius = radius;
}
void Entity::simReset() {
	x = resX;
	y = resY;
	velX = resVelX;
	velY = resVelY;
	rotation = resRotation;
	rotateVel = resRotateVel;
	mass = resMass;
	radius = resRadius;
}

Quad& Quad::getChild(uint8_t at) {
	if (children[at] == 0) {
		if (quadsConstructed == quadsAllocated) [[unlikely]] {
			throw std::bad_alloc();
		}
		Quad& child = quadtree[quadsConstructed];
		child = Quad();
		double halfsize = size * 0.5;
		child.x = at == 1 || at == 3 ? x + halfsize : x;
		child.y = at > 1 ? y + halfsize : y;
		child.size = halfsize;
		child.invsize = invsize * 2;
		children[at] = quadsConstructed;
		quadsConstructed++;
		return child;
	}
	return quadtree[children[at]];
}
void Quad::put(Entity* e) {
	mass += e->mass;
	comx += e->mass * e->x;
	comy += e->mass * e->y;
	if (used) {
		getChild((e->x > x + size * 0.5) + 2 * (e->y > y + size * 0.5)).put(e);
		if (entity) {
			if (entity->ghost && entity->parent_id == e->id) [[unlikely]] {
				entity = nullptr;
				return;
			}
			getChild((entity->x > x + size * 0.5) + 2 * (entity->y > y + size * 0.5)).put(entity);
			entity = nullptr;
		}
	} else {
		entity = e;
		used = true;
	}
}
void Quad::postBuild() {
	comx /= mass;
	comy /= mass;
	for (uint16_t c : children) {
		if (c != 0) {
			quadtree[c].postBuild();
		}
	}
}
void Quad::collideAttract(Entity* e, bool doGravity, bool checkCollide) {
	checkCollide = checkCollide && e->x + e->radius > x && e->y + e->radius > y && e->x - e->radius < x + size && e->y - e->radius < y + size;
	if (entity && entity != e) {
		if (e->parent_id == entity->id || entity->parent_id == e->id) [[unlikely]] {
			return;
		}
		if (checkCollide) {
			bool alreadyCollided = false;
			for (uint32_t id : e->collided) {
				if (id == entity->id) {
					alreadyCollided = true;
					break;
				}
			}
			if (!alreadyCollided && dst2(e->x - entity->x, e->y - entity->y) <= (e->radius + entity->radius) * (e->radius + entity->radius)) {
				e->collide(entity, true);
				entity->collided.push_back(e->id);
			}
		}
		if (doGravity) {
			for (uint32_t id : e->attracted) {
				if (id == entity->id) {
					return;
				}
			}
			double xdiff = entity->x - e->x, ydiff = entity->y - e->y;
			double dist = dst(xdiff, ydiff);
			double factor = delta * G / (dist * dist * dist);
			double factorthis = factor * entity->mass;
			e->addVelocity(xdiff * factorthis, ydiff * factorthis);
			double factorother = -factor * e->mass;
			entity->addVelocity(xdiff * factorother, ydiff * factorother);
			entity->attracted.push_back(e->id);
		}
		return;
	}
	if (doGravity) {
		double halfsize = size * 0.5, midx = x + halfsize, midy = y + halfsize;
		if (invsize * (abs(e->x - midx) + abs(e->y - midy)) > gravityAccuracy) {
			double xdiff = comx - e->x, ydiff = comy - e->y;
			double dist = dst(xdiff, ydiff);
			double factor = delta * mass * G / (dist * dist * dist);
			e->addVelocity(xdiff * factor, ydiff * factor);
			doGravity = false;
		}
	} else if (!checkCollide) {
		return;
	}
	for (uint16_t c : children) {
		if (c != 0) {
			quadtree[c].collideAttract(e, doGravity, checkCollide);
		}
	}
}
void Quad::draw() {
	sf::RectangleShape quad(sf::Vector2f(size / g_camera.scale, size / g_camera.scale));
	quad.setPosition(g_camera.w * 0.5 + (x - ownX) / g_camera.scale, g_camera.h * 0.5 + (y - ownY) / g_camera.scale);
	quad.setFillColor(sf::Color(0, 0, 0, 0));
	quad.setOutlineColor(sf::Color(0, 0, 255, 255));
	quad.setOutlineThickness(1);
	window->draw(quad);
	for (uint16_t c : children) {
		if (c != 0) {
			quadtree[c].draw();
		}
	}
}

void reallocateQuadtree() {
	quadsAllocated = std::max(minQuadtreeSize, (int)(quadsConstructed * extraQuadAllocation));
	Quad* newQuadtree = (Quad*)malloc(quadsAllocated * sizeof(Quad));
	memcpy(newQuadtree, quadtree, quadsConstructed * sizeof(Quad));
	delete quadtree;
	quadtree = newQuadtree;
	if (debug) [[unlikely]] {
		printf("Reallocated quadtree, new size: %u\n", quadsAllocated);
	}
}
void buildQuadtree() {
	double x1 = +INFINITY, y1 = +INFINITY, x2 = -INFINITY, y2 = -INFINITY;
	for (Entity* e : updateGroup) {
		x1 = std::min(e->x, x1);
		y1 = std::min(e->y, y1);
		x2 = std::max(e->x, x2);
		y2 = std::max(e->y, y2);
	}
	quadtree[0] = Quad();
	quadtree[0].x = x1;
	quadtree[0].y = y1;
	quadtree[0].size = std::max(x2 - x1, y2 - y1);
	quadtree[0].invsize = 1.0 / quadtree[0].size;
	quadsConstructed = 1;
	for (size_t i = 0; i < updateGroup.size(); i++) {
		try {
			quadtree[0].put(updateGroup[i]);
		} catch (const std::bad_alloc& except) {
			delete quadtree;
			quadsAllocated = (int)(quadsAllocated * extraQuadAllocation);
			quadtree = (Quad*)malloc(quadsAllocated * sizeof(Quad));
			quadtree[0] = Quad();
			quadsConstructed = 1;
			i = 0;
			if (debug) [[unlikely]] {
				printf("Ran out of memory for quadtree, new size: %u\nPerforming investigation...", quadsAllocated);
				for (Entity* e1 : updateGroup) {
					for (Entity* e2 : updateGroup) {
						if (e1->x == e2->x || e1->y == e2->y) [[unlikely]] {
							printf("Found entities with equal coordinates: %g, %g and %g, %g\n", e1->x, e1->y, e2->x, e2->y);
						}
					}
				}
			}
		}
		if (quadsConstructed > quadsAllocated * quadReallocateThreshold) [[unlikely]] {
			if (debug) [[unlikely]] {
				printf("Expanding quadtree... ");
			}
			reallocateQuadtree();
		}
	}
	quadtree[0].postBuild();
	if (std::max((double)quadsConstructed, minQuadtreeSize / quadtreeShrinkThreshold) < quadsAllocated * quadtreeShrinkThreshold) [[unlikely]] {
		if (debug) [[unlikely]] {
			printf("Shrinking quadtree... ");
		}
		reallocateQuadtree();
	}
}

Triangle::Triangle() : Entity() {
	mass = 20000.0;
	radius = 16.0;
	if (!headless && !simulating) {
		shape = std::make_unique<sf::CircleShape>(radius, 3);
		shape->setOrigin(radius, radius);
		forwards = std::make_unique<sf::CircleShape>(2.f, 6);
		forwards->setOrigin(2.f, 2.f);
		icon = std::make_unique<sf::CircleShape>(3.f, 3);
		icon->setOrigin(3.f, 3.f);
		icon->setFillColor(sf::Color(255, 255, 255));
	}
}

void Triangle::loadCreatePacket(sf::Packet& packet) {
	packet << type() << id << x << y << velX << velY << rotation << name;
	if (debug) {
		printf("Sent id %d: %g %g %g %g\n", id, x, y, velX, velY);
	}
}
void Triangle::unloadCreatePacket(sf::Packet& packet) {
	packet >> id >> x >> y >> velX >> velY >> rotation >> name;
	if (debug) {
		printf("Received id %d: %g %g %g %g\n", id, x, y, velX, velY);
	}
}
void Triangle::loadSyncPacket(sf::Packet& packet) {
	packet << id << x << y << velX << velY << rotation;
}
void Triangle::unloadSyncPacket(sf::Packet& packet) {
	packet >> syncX >> syncY >> syncVelX >> syncVelY >> rotation;
}

void Triangle::simSetup() {
	Entity::simSetup();
	resLastBoosted = lastBoosted;
	resLastShot = lastShot;
	resHyperboostCharge = hyperboostCharge;
	resBurning = burning;
}
void Triangle::simReset() {
	Entity::simReset();
	lastBoosted = resLastBoosted;
	lastShot = resLastShot;
	hyperboostCharge = resHyperboostCharge;
	burning = resBurning;
}

void Triangle::control(movement& cont) {
	float rotationRad = rotation * degToRad;
	double xMul = std::cos(rotationRad), yMul = -std::sin(rotationRad);
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
			rotateVel += hyperboostRotateSpeed * delta;
		} else if (cont.turnright) {
			rotateVel -= hyperboostRotateSpeed * delta;
		}
		if (rotateVel > 0.0) {
			rotateVel = std::max(0.0, rotateVel - hyperboostRotateSpeed * delta * rotateSlowSpeedMult);
		}
		if (rotateVel < 0.0) {
			rotateVel = std::min(0.0, rotateVel + hyperboostRotateSpeed * delta * rotateSlowSpeedMult);
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
		rotateVel += rotateSpeed * delta;
	} else if (cont.turnright) {
		rotateVel -= rotateSpeed * delta;
	}
	if (rotateVel > 0.0) {
		rotateVel = std::max(0.0, rotateVel - rotateSpeed * delta * rotateSlowSpeedMult);
	}
	if (rotateVel < 0.0) {
		rotateVel = std::min(0.0, rotateVel + rotateSpeed * delta * rotateSlowSpeedMult);
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
		if (headless || simulating) {
			Projectile* proj = new Projectile();
			if (simulating) {
				simCleanupBuffer.push_back(proj);
			}
			proj->setPosition(x + (radius + proj->radius * 2.0) * xMul, y - (radius + proj->radius * 2.0) * yMul);
			proj->setVelocity(velX + shootPower * xMul, velY + shootPower * yMul);
			proj->owner = this;
			addVelocity(-shootPower * xMul * proj->mass / mass, -shootPower * yMul * proj->mass / mass);
			if (headless) {
				proj->syncCreation();
			}
		}
		lastShot = globalTime;
	}
}

void Triangle::draw() {
	Entity::draw();
	shape->setPosition(x + drawShiftX, y + drawShiftY);
	shape->setRotation(90.f - rotation);
	shape->setFillColor(sf::Color(color[0], color[1], color[2]));
	window->draw(*shape);
	g_camera.bindUI();
	float rotationRad = rotation * degToRad;
	double uiX = g_camera.w * 0.5 + (x - ownX) / g_camera.scale, uiY = g_camera.h * 0.5 + (y - ownY) / g_camera.scale;
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

uint8_t Triangle::type() {
	return Entities::Triangle;
}

CelestialBody::CelestialBody(double radius) : Entity() {
	this->radius = radius;
	this->mass = 1.0e18;
	if (!headless) {
		shape = std::make_unique<sf::CircleShape>(radius, std::max(4, (int)(sqrt(radius))));
		shape->setOrigin(radius, radius);
		icon = std::make_unique<sf::CircleShape>(2.f, 6);
		icon->setOrigin(2.f, 2.f);
		warning = std::make_unique<sf::CircleShape>(5.f, 4);
		warning->setOrigin(5.f, 5.f);
		warning->setFillColor(sf::Color(0, 0, 0, 0));
		warning->setOutlineColor(sf::Color(255, 0, 0));
		warning->setOutlineThickness(1.f);
	}
}
CelestialBody::CelestialBody(double radius, double mass) : Entity() {
	this->radius = radius;
	this->mass = mass;
	if (!headless) {
		shape = std::make_unique<sf::CircleShape>(radius, std::max(4, (int)(sqrt(radius))));
		shape->setOrigin(radius, radius);
		icon = std::make_unique<sf::CircleShape>(2.f, 6);
		icon->setOrigin(2.f, 2.f);
		warning = std::make_unique<sf::CircleShape>(5.f, 4);
		warning->setOrigin(5.f, 5.f);
		warning->setFillColor(sf::Color(0, 0, 0, 0));
		warning->setOutlineColor(sf::Color(255, 0, 0));
		warning->setOutlineThickness(1.f);
	}
}
CelestialBody::CelestialBody(bool ghost) {
	for (size_t i = 0; i < updateGroup.size(); i++) {
		Entity* e = updateGroup[i];
		if (e == this) [[unlikely]] {
			updateGroup[i] = updateGroup[updateGroup.size() - 1];
			updateGroup.pop_back();
			break;
		}
	}
}

void CelestialBody::loadCreatePacket(sf::Packet& packet) {
	packet << type() << radius << id << x << y << velX << velY << mass << star << blackhole << color[0] << color[1] << color[2];
	if (debug) {
		printf("Sent id %d: %g %g %g %g\n", id, x, y, velX, velY);
	}
}
void CelestialBody::unloadCreatePacket(sf::Packet& packet) {
	packet >> id >> x >> y >> velX >> velY >> mass >> star >> blackhole >> color[0] >> color[1] >> color[2];
	if (debug) {
		printf(", id %d: %g %g %g %g\n", id, x, y, velX, velY);
	}
}
void CelestialBody::loadSyncPacket(sf::Packet& packet) {
	packet << id << x << y << velX << velY;
}
void CelestialBody::unloadSyncPacket(sf::Packet& packet) {
	packet >> syncX >> syncY >> syncVelX >> syncVelY;
}

void CelestialBody::collide(Entity* with, bool collideOther) {
	Entity::collide(with, collideOther);
	if (!with->active) [[unlikely]] {
		return;
	}
	if (headless && star && with->type() == Entities::Triangle) [[unlikely]] {
		bool found = false;
		for (Player* p : playerGroup) {
			if (p->entity == with) [[unlikely]] {
				sf::Packet chatPacket;
				std::string sendMessage;
				sendMessage.append("<").append(p->name()).append("> has been incinerated.");
				relayMessage(sendMessage);
				setupShip(p->entity);
				found = true;
				break;
			}
		}
		if (!found && (headless || simulating)) {
			with->active = false;
		}
	} else if (with->type() == Entities::CelestialBody) [[unlikely]] {
		if (mass >= with->mass && (headless || simulating)) {
			if (!simulating) {
				printf("Planetary collision: %u absorbed %u\n", id, with->id);
			}
			double radiusMul = sqrt((mass + with->mass) / mass);
			mass += with->mass;
			radius *= radiusMul;
			if (headless) {
				sf::Packet collisionPacket;
				collisionPacket << Packets::PlanetCollision << id << mass << radius;
				for (Player* p : playerGroup) {
					p->tcpSocket.send(collisionPacket);
				}
			}
			with->active = false;
		}
	}
}

void CelestialBody::draw() {
	Entity::draw();
	shape->setPosition(x + drawShiftX, y + drawShiftY);
	shape->setFillColor(sf::Color(color[0], color[1], color[2]));
	window->draw(*shape);
	if (ownEntity) {
		g_camera.bindUI();
		double uiX = g_camera.w * 0.5 + (x - ownX) / g_camera.scale, uiY = g_camera.h * 0.5 + (y - ownY) / g_camera.scale;
		if (g_camera.scale > radius) {
			icon->setPosition(uiX, uiY);
			icon->setFillColor(sf::Color(color[0], color[1], color[2]));
			window->draw(*icon);
		}
		if (blackhole && this != lastTrajectoryRef) {
			warning->setPosition(uiX, uiY);
			window->draw(*warning);
		}
		g_camera.bindWorld();
	}
}

uint8_t CelestialBody::type() {
	return Entities::CelestialBody;
}

Projectile::Projectile() : Entity() {
	this->radius = 6;
	this->mass = 2000.0;
	this->color[0] = 180;
	this->color[1] = 0;
	this->color[2] = 0;
	if (!headless && !simulating) {
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
	if (debug) {
		printf("Sent id %d: %g %g %g %g\n", id, x, y, velX, velY);
	}
}
void Projectile::unloadCreatePacket(sf::Packet& packet) {
	packet >> id >> x >> y >> velX >> velY;
	if (debug) {
		printf(", id %d: %g %g %g %g\n", id, x, y, velX, velY);
	}
}
void Projectile::loadSyncPacket(sf::Packet& packet) {
	packet << id << x << y << velX << velY;
}
void Projectile::unloadSyncPacket(sf::Packet& packet) {
	packet >> syncX >> syncY >> syncVelX >> syncVelY;
}

void Projectile::collide(Entity* with, bool collideOther) {
	if (debug) {
		printf("bullet collision: %u-%u ", id, with->id);
	}
	if (with->type() == Entities::Triangle) {
		if (owner) {
			owner->kills++;
		}
		if (debug) {
			printf("of type triangle\n");
		}
		if (headless) {
			for (Player* p : playerGroup) {
				if (p->entity == with) [[unlikely]] {
					sf::Packet chatPacket;
					std::string sendMessage;
					sendMessage.append("<").append(p->name()).append("> has been killed.");
					relayMessage(sendMessage);
					setupShip(p->entity);
				}
			}
			if (owner) {
				owner->kills++;
			}
		}
		if (headless || simulating) {
			active = false;
			with->active = false;
		}
	} else if (with->type() == Entities::CelestialBody) {
		if (debug) {
			printf("of type CelestialBody\n");
		}
		if (headless || simulating) {
			active = false;
		}
	} else {
		if (debug) {
			printf("of unaccounted type\n");
			Entity::collide(with, collideOther);
		}
	}
}

void Projectile::draw() {
	Entity::draw();
	shape->setPosition(x + drawShiftX, y + drawShiftY);
	shape->setFillColor(sf::Color(color[0], color[1], color[2]));
	window->draw(*shape);
	if (g_camera.scale > radius) {
		g_camera.bindUI();
		icon->setPosition(g_camera.w * 0.5 + (x - ownX) / g_camera.scale, g_camera.h * 0.5 + (y - ownY) / g_camera.scale);
		window->draw(*icon);
		g_camera.bindWorld();
	}
}

uint8_t Projectile::type() {
	return Entities::Projectile;
}

}
