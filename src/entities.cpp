#include "camera.hpp"
#include "entities.hpp"
#include "globals.hpp"
#include "math.hpp"
#include "net.hpp"
#include "types.hpp"

#include <chrono>
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

void setupShip(Entity* ship, bool sync) {
	CelestialBody* planet;
	std::vector<CelestialBody*> gravitators;
	for (Entity* e : updateGroup) {
		if (e->type() == Entities::CelestialBody) {
			gravitators.push_back((CelestialBody*)e);
		}
	}
	if (gravitators.size() == 0) {
		printf("no planets, couldn't spawn ship\n");
	}
	size_t at = (size_t)rand_f(0, gravitators.size());
	planet = gravitators[at];
	double spawnDst = planet->radius * rand_f(shipSpawnDistanceMin, shipSpawnDistanceMax);
	float spawnAngle = rand_f(-PI, PI);
	ship->setPosition(planet->x + spawnDst * std::cos(spawnAngle), planet->y + spawnDst * std::sin(spawnAngle));
	double vel = sqrt(G * planet->mass / spawnDst);
	ship->setVelocity(planet->velX + vel * std::cos(spawnAngle + PI / 2.0), planet->velY + vel * std::sin(spawnAngle + PI / 2.0));
	if (sync) {
		sf::Packet packet;
		packet << Packets::SyncEntity;
		ship->loadSyncPacket(packet);
		for (Player* p : playerGroup) {
			p->tcpSocket.send(packet);
		}
	}
}

int generateOrbitingPlanets(std::vector<CelestialBody*>& planets, int amount, double x, double y, double velx, double vely, double parentmass, double minradius, double maxradius, double spawnDst) {
	int totalMoons = 0;
	double maxFactor = sqrt(pow(gen_minNextRadius * gen_maxNextRadius, amount * 0.5) * spawnDst);

	for (int i = 0; i < amount; i++) {
		spawnDst *= rand_f(gen_minNextRadius, gen_maxNextRadius);
		double factor = sqrt(spawnDst) / maxFactor; // makes planets further outward generate larger
		float spawnAngle = rand_f(-PI, PI);
		float radius = rand_f(minradius, maxradius * factor);
		double mass = gen_baseDensity * pow(radius, gen_densityFactor);
		bool star = mass > gen_starMass * gen_starMassReq;

		CelestialBody* planetp = new CelestialBody(star ? gen_starRadius * pow(mass / gen_starMass, 1.0 / gen_starDensityFactor) : radius, mass);
		planets.push_back(planetp);
		CelestialBody& planet = *planetp;
		planet.postMassUpdate();

		planet.setPosition(x + spawnDst * std::cos(spawnAngle), y + spawnDst * std::sin(spawnAngle));
		double vel = sqrt(G * parentmass / spawnDst);
		planet.addVelocity(velx + vel * std::cos(spawnAngle + PI / 2.0), vely + vel * std::sin(spawnAngle + PI / 2.0));

		if (!star) {
			planet.setColor((int)rand_f(64.f, 255.f), (int)rand_f(64.f, 255.f), (int)rand_f(64.f, 255.f));
		}

		int moons = (int)(rand_f(0.f, 1.f) * pow(radius / gen_moonFactor, gen_moonPower));
		double moonsDistance = planet.radius * (1.0 + rand_f(gen_minMoonDistance, gen_minMoonDistance + pow(gen_maxMoonDistance, std::min(1.0, 0.5 / (planet.radius / gen_maxPlanetRadius)))));
		totalMoons += moons + generateOrbitingPlanets(planets, moons, planet.x, planet.y, planet.velX, planet.velY, planet.mass, gen_minMoonRadius, planet.radius * gen_maxMoonRadiusFrac, moonsDistance);
	}

	return totalMoons;
}

void generateSystem() {
	std::vector<CelestialBody*> planets;
	std::vector<CelestialBody*> stars;
	int starsN = 1;
	while (rand_f(0.f, 1.f) < gen_extraStarChance) {
		starsN += 1;
	}
	double angleSpacing = TAU / starsN, angle = 0.0;
	double starsMass = gen_starMass * starsN, dist = (starsN - 1) * gen_starRadius * 2.0;
	for (int i = 0; i < starsN; i++) {
		bool blackhole = rand_f(0.f, 1.f) < gen_blackholeChance;
		CelestialBody* starp = new CelestialBody(blackhole ? 2.0 * G * gen_starMass / (CC) : gen_starRadius, blackhole ? gen_starMass * 1.0001 : gen_starMass);
		stars.push_back(starp);
		CelestialBody& star = *starp;
		star.star = true;

		star.blackhole = blackhole;
		if (blackhole) {
			star.setColor(0, 0, 0);
		} else {
			star.setColor(255, 229, 97);
		}

		double posX = std::cos(angle) * dist;
		double posY = std::sin(angle) * dist;
		star.setPosition(posX, posY);

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
	double spawnDst = gen_firstPlanetDistance * rand_f(gen_minNextRadius, gen_maxNextRadius) + dist + gen_starRadius;
	int planetsN = (int)(rand_f(gen_baseMinPlanets, gen_baseMaxPlanets) * sqrt(starsN));
	printf("Generated system: %u stars, %u planets, %u moons\n", starsN, planetsN, generateOrbitingPlanets(planets, planetsN, 0.0, 0.0, 0.0, 0.0, starsMass, gen_minPlanetRadius, gen_maxPlanetRadius, spawnDst));
	stars.clear();
	planets.clear();
}

void fullClear(bool clearTriangles) {
	if (isServer) {
		sf::Packet clearPacket;
		clearPacket << Packets::FullClear;
		for (Player* p : playerGroup) {
			p->tcpSocket.send(clearPacket);
		}
	}
	std::vector<Entity*> triangles;
	for (Entity* e : updateGroup) {
		if (clearTriangles || e->type() != Entities::Triangle) {
			delete e;
		} else {
			triangles.push_back(e);
		}
	}
	if (clearTriangles) {
		ownEntity = nullptr;
		updateGroup.clear();
	} else {
		updateGroup = triangles;
	}
	trajectoryRef = nullptr;
	lastTrajectoryRef = nullptr;
}

void updateEntities() {
	for (Entity* e : updateGroup) {
		e->update2();
	}
	return;
}

void drawTrajectory(sf::Color& color, std::vector<Point>& traj) {
	size_t to = traj.size();
	size_t by = std::max((size_t)1, (size_t)(g_camera.scale / predictBaseScale));
	if (by > to / 2) {
		return;
	}
	if (lastTrajectoryRef && to > 0) [[likely]] {
		sf::VertexArray lines(sf::Lines, to / by + (size_t)(to % by != 0));
		float lastAlpha = 255.f;
		float decBy = (255.f - 64.f) / to;
		size_t i = 0;
		for (size_t j = 0; j < to; j += by) {
			Point point = traj[j];
			lines[i].position = sf::Vector2f(lastTrajectoryRef->x + point.x + drawShiftX, lastTrajectoryRef->y + point.y + drawShiftY);
			lines[i].color = color;
			lines[i].color.a = (uint8_t)lastAlpha;
			lastAlpha -= decBy;
			++i;
		}
		window->draw(lines);
	}
}

Entity* idLookup(uint32_t id) {
	size_t searchBy = 0;
	for (size_t i = 1; i > 0; i = i << 1) {
		searchBy = std::max(searchBy, updateGroup.size() & i);
	}
	size_t at = 0;
	for (size_t i = searchBy; i > 0; i = i >> 1) {
		if (at + i < updateGroup.size() && updateGroup[at + i]->id <= id) {
			at += i;
		}
	}
	return updateGroup[at]->id == id ? updateGroup[at] : nullptr;
}

double Projectile::mass = 5.0e3;

double Missile::mass = 1.0e3,
Missile::accel = 196.0,
Missile::rotateSpeed = 240.0,
Missile::maxThrustAngle = 45.0 * degToRad,
Missile::startingFuel = 80.0,
Missile::leastItimeDecrease = 0.4,
Missile::fullThrustThreshold = 0.95;
int Missile::guidanceIterations = 3;

double Triangle::mass = 1.0e7,
Triangle::accel = 96.0,
Triangle::rotateSlowSpeedMult = 2.0 / 3.0,
Triangle::rotateSpeed = 180.0,
Triangle::boostCooldown = 12.0,
Triangle::boostStrength = 320.0,
Triangle::reload = 8.0,
Triangle::shootPower = 120.0,
Triangle::secondaryRegen = 0.3,
Triangle::secondaryReload = 1.0,
Triangle::secondaryStockpile = 6.0,
Triangle::secondaryShootPower = 25000.0,
Triangle::maxSecondaryAngle = 9.0,
Triangle::slowRotateSpeed = 0.02;

std::string Player::name() {
	return username;
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
	ghost = simulating;
}

Entity::~Entity() noexcept {
	if (debug) {
		printf("Deleting entity id %u\n", this->id);
	}
	if (this == ownEntity) {
		ownEntity = nullptr;
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

void Entity::control(movement&) {
	return;
}
void Entity::update1() {
	dVelX = velX * delta;
	dVelY = velY * delta;
	x += dVelX;
	y += dVelY;
	rotation += rotateVel * delta;
	collided.clear();
}
void Entity::update2() {
	quadtree[0].collideAttract(this, true, true);
}

void Entity::draw() {
	sf::Color trajColor(color[0], color[1], color[2]);
	drawTrajectory(trajColor, trajectory);
}

void Entity::collide(Entity* with, bool specialOnly) {
	if (specialOnly) {
		return;
	}
	if (debug && !simulating && dst2(with->velX - velX, with->velY - velY) > 0.1) [[unlikely]] {
		printf("collision: %u-%u\n", id, with->id);
	}
	double massFactorThis = 1.0 / (1.0 + mass / with->mass);
	double massFactorOther = 1.0 / (1.0 + with->mass / mass); // for conservation of momentum
	double inHeading = std::atan2(y - with->y, x - with->x); // heading of vector from other to this
	double inX = std::cos(inHeading);
	double inY = std::sin(inHeading);
	double newX = x + (with->x - x + (radius + with->radius) * inX) * massFactorThis;
	double newY = y + (with->y - y + (radius + with->radius) * inY) * massFactorThis;
	with->x -= (with->x - x + (radius + with->radius) * inX) * massFactorOther;
	with->y -= (with->y - y + (radius + with->radius) * inY) * massFactorOther;
	x = newX;
	y = newY;
	double dVx = with->velX - velX, dVy = with->velY - velY;
	double velHeading = std::atan2(dVy, dVx); // heading of own relative velocity
	double factor = std::cos(std::abs(deltaAngleRad(inHeading, velHeading)));
	if (factor < 0.0) {
		return;
	}
	factor *= dst(dVx, dVy) * collideRestitution; // normal component of velocity multiplied by restitution
	addVelocity(massFactorThis * (inX * factor + friction * delta * dVx), massFactorThis * (inY * factor + friction * delta * dVy));
	with->addVelocity(-massFactorOther * (inX * factor + friction * delta * dVx), -massFactorOther * (inY * factor + friction * delta * dVy));
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

void Entity::onEntityDelete(Entity* d) {
	if (simRelBody == d) {
		simRelBody = nullptr;
	}
}

uint32_t Quad::getMakeChild(uint32_t id, double at_x, double at_y) {
	Quad parent = quadtree[id];
	uint8_t at = (at_x > parent.tX + parent.size * 0.5) + 2 * (at_y > parent.tY + parent.size * 0.5);
	if (parent.children[at] == 0) {
		uint32_t next_id = (uint32_t)quadtree.size();
		quadtree.emplace_back();
		Quad& child = quadtree.back();
		double halfsize = parent.size * 0.5;
		child.tX = at == 1 || at == 3 ? parent.tX + halfsize : parent.tX;
		child.x = child.tX;
		child.tY = at > 1 ? parent.tY + halfsize : parent.tY;
		child.y = child.tY;
		child.size = halfsize;
		child.xsize = halfsize;
		child.ysize = halfsize;
		quadtree[id].children[at] = next_id;
		return next_id;
	}
	return parent.children[at];
}
Quad& Quad::getChild(uint8_t at) {
	return quadtree[children[at]];
}
void Quad::put(uint32_t id, Entity* e, int reclevel) {
	Quad& cur = quadtree[id];
	cur.mass += e->mass;
	cur.comx += e->mass * e->x;
	cur.comy += e->mass * e->y;
	cur.hasGravitators = cur.hasGravitators || e->gravitates;
	if (reclevel > 512) {
		printf("body {ptr %lli, id %i, type %i, x %f, y %f, vx %f, vy %f, radius %f} exceeded quadtree recursion limit.\n", (size_t)e, e->id, e->type(), e->x, e->y, e->velX, e->velY, e->radius);
		e->active = false;
		e = nullptr;
		return;
	}
	double nEntX = e->x + e->dVelX;
	double nEntY = e->y + e->dVelY;
	cur.x     = std::min(cur.x,     nEntX - e->radius        );
	cur.xsize = std::max(cur.xsize, nEntX + e->radius - cur.x);
	cur.y     = std::min(cur.y,     nEntY - e->radius        );
	cur.ysize = std::max(cur.ysize, nEntY + e->radius - cur.y); // stretch the quad to fit the entity during this and next frames
	if (cur.used) {
		Entity* ent = cur.entity;
		put(getMakeChild(id, e->x, e->y), e, reclevel + 1); // may have invalidated `cur`
		if (ent) {
			if (ent->ghost && ent->parent_id == e->id) [[unlikely]] {
				quadtree[id].entity = nullptr;
				return;
			}
			put(getMakeChild(id, ent->x, ent->y), ent, reclevel + 1);
			quadtree[id].entity = nullptr;
		}
	} else {
		cur.entity = e;
		cur.used = true;
	}
}
uint32_t Quad::unstaircasize() { // makes children which are "staircases" of identical quads just point to the lowest non-identical children instead
	for (uint32_t& c : children) {
		if (c != 0) {
			Quad& child = quadtree[c];
			uint32_t retcode = child.unstaircasize();
			if (child.comx == comx && child.comy == comy) {
				return retcode == 0 ? c : retcode;
			} else if (retcode != 0) {
				c = retcode;
			}
		}
	}
	return 0;
}
void Quad::postBuild() {
	comx /= mass;
	comy /= mass;
	for (uint32_t c : children) {
		if (c != 0) {
			Quad& child = quadtree[c];
			child.postBuild();
			x = std::min(x, child.x);
			xsize = std::max(xsize, child.x - x + child.xsize);
			y = std::min(y, child.y);
			ysize = std::max(ysize, child.y - y + child.ysize);
		}
	}
}
void Quad::collideAttract(Entity* e, bool doGravity, bool checkCollide) {
	// will the entity during its movement be at least partially within the stretched quad?
	checkCollide = checkCollide && e->x - e->radius - std::max(e->dVelX, 0.0) < (x + xsize) && e->y - e->radius - std::max(e->dVelY, 0.0) < (y + ysize) && e->x + e->radius + std::max(e->dVelX, 0.0) > x && e->y + e->radius + std::max(e->dVelX, 0.0) > y;
	if (entity && entity != e) {
		if (e->parent_id == entity->id || entity->parent_id == e->id) [[unlikely]] {
			return;
		}
		if (checkCollide && std::find(e->collided.begin(), e->collided.end(), entity->id) == e->collided.end()) {
			double dVx = entity->dVelX - e->dVelX, dVy = entity->dVelY - e->dVelY,
			dx = e->x - entity->x, dy = e->y - entity->y;
			double radiusSum = e->radius + entity->radius;
			if (dst2(dx, dy) <= radiusSum * radiusSum) {
				e->collide(entity, false);
				entity->collide(e, true);
				entity->collided.push_back(e->id);
			} else if (std::abs(dx) - radiusSum < std::abs(dVx) * 2.0 && std::abs(dy) - radiusSum < std::abs(dVy) * 2.0) { // possibly colliding before next frame?
				double vel = dst(dVx, dVy);
				double ivel = 1.0 / vel,
				// calculate closest approach and at what x it will happen to check whether velocity is big enough to reach said closest approach
				/*         |   . = r
				 *         |  /|
				 *         | / | = c = ?
				 *         |/  |              dot_p = r * c * cos(r ^ c)
				 * --------|--->----          c = r * cos(r ^ c) = r * dot_p / r = dot_p / c
				 *         |   = v            c⃗_dir = (v_y, -v_x)
				 *         |                  dot_p = r⃗ * c⃗ = r_x * v_y - v_x * r_y
				 *         |                  c = (r_x * v_y - v_x * r_y) / c_dir = (r_x * v_y - v_x * r_y) / vel
				*/
				cApproach = (dx * dVy - dy * dVx) * ivel,
				cApproachAt = sqrt(dst2(dx, dy) - cApproach * cApproach); // distance the body will pass before closest approach
				// collideAt = cApproachAt - sqrt(radiusSum * radiusSum - cApproach * cApproach); // distance the body will pass before colliding if abs(radiusSum) > abs(cApproach)
				// cApproachAtX = dx - cApproach * dVy * ivel;
				if (std::abs(cApproach) <= radiusSum && cApproachAt <= vel) {
					if (debug) {
						printf("Collision: dX %f, dY %f, dVx %f, dVy %f, vel %f, cApproach %f, cApproachAt %f\n", dx, dy, dVx, dVy, vel, cApproach, cApproachAt);
					}
					e->collide(entity, false);
					entity->collide(e, true);
					entity->collided.push_back(e->id);
				}
			}
		}
		if (doGravity) {
			double xdiff = entity->x - e->x, ydiff = entity->y - e->y;
			double dist = dst(xdiff, ydiff);
			double factor = entity->mass * delta * G / (dist * dist * dist);
			e->addVelocity(xdiff * factor, ydiff * factor); // here and below: `F = GM/R^3 * R_vec`, for each coordinate
		}
		return;
	}
	if (doGravity) {
		double midx = x + xsize * 0.5, midy = y + ysize * 0.5;
		if (!hasGravitators || (std::abs(e->x - midx) + std::abs(e->y - midy)) > gravityAccuracy * size) {
			double xdiff = comx - e->x, ydiff = comy - e->y;
			double dist = dst(xdiff, ydiff);
			double factor = delta * mass * G / (dist * dist * dist);
			e->addVelocity(xdiff * factor, ydiff * factor); //          ^ "below" being here
			doGravity = false;
		}
	} else if (!checkCollide) {
		return;
	}
	for (uint32_t c : children) {
		if (c != 0) {
			quadtree[c].collideAttract(e, doGravity, checkCollide);
		}
	}
}
void Quad::draw() {
	sf::RectangleShape quad(sf::Vector2f(xsize / g_camera.scale, ysize / g_camera.scale));
	quad.setPosition(g_camera.w * 0.5 + (x - ownX) / g_camera.scale, g_camera.h * 0.5 + (y - ownY) / g_camera.scale);
	quad.setFillColor(sf::Color(0, 0, 0, 0));
	quad.setOutlineColor(sf::Color(0, 0, 255, 255));
	quad.setOutlineThickness(1);
	window->draw(quad);
	for (uint32_t c : children) {
		if (c != 0) {
			quadtree[c].draw();
		}
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
	quadtree.clear();
	quadtree.emplace_back();
	Quad& root = quadtree.front();
	root.x = x1;
	root.tX = x1;
	root.y = y1;
	root.tY = y1;
	root.size = std::max(x2 - x1, y2 - y1);
	root.xsize = x2 - x1;
	root.ysize = y2 - y1;
	for (size_t i = 0; i < updateGroup.size(); i++) {
		Quad::put(0, updateGroup[i], 0);
	}
	quadtree[0].unstaircasize();
	quadtree[0].postBuild();
}

Triangle::Triangle() : Entity() {
	this->Entity::mass = Triangle::mass;
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
	resBoostProgress = boostProgress;
	resReloadProgress = reloadProgress;
	resSecondaryCharge = secondaryCharge;
	resSecondaryProgress = secondaryProgress;
}
void Triangle::simReset() {
	Entity::simReset();
 	boostProgress = resBoostProgress;
	reloadProgress = resReloadProgress;
	secondaryCharge = resSecondaryCharge;
	secondaryProgress = resSecondaryProgress;
}

void Triangle::control(movement& cont) {
	float rotationRad = rotation * degToRad;
	double xMul = std::cos(rotationRad), yMul = std::sin(rotationRad);
	double rotateSpeed = this->rotateSpeed;
	bool setDraw = headless;
	boostProgress += delta;
	if (reloadProgress < reload) {
		reloadProgress += delta;
	}
	if (secondaryProgress < secondaryReload) {
		secondaryProgress += delta;
	}
	secondaryCharge = std::min(secondaryStockpile, secondaryCharge + secondaryRegen * delta);
	if (cont.slowrotate) {
		rotateSpeed *= slowRotateSpeed;
		if (!setDraw) {
			forwards->setFillColor(sf::Color(255, 255, 0));
			forwards->setRotation(90.f + rotation);
			setDraw = true;
		}
	}
	if (cont.forward) {
		addVelocity(accel * xMul * delta, accel * yMul * delta);
		if (!setDraw) {
			forwards->setFillColor(sf::Color(255, 196, 0));
			forwards->setRotation(90.f + rotation);
			setDraw = true;
		}
	} else if (cont.backward) {
		addVelocity(-accel * xMul * delta, -accel * yMul * delta);
		if (!setDraw) {
			forwards->setFillColor(sf::Color(255, 64, 64));
			forwards->setRotation(270.f + rotation);
			setDraw = true;
		}
	}
	if (!setDraw) {
		forwards->setFillColor(sf::Color::White);
		forwards->setRotation(90.f + rotation);
		setDraw = true;
	}
	if (cont.turnleft) {
		rotateVel -= rotateSpeed * delta;
	} else if (cont.turnright) {
		rotateVel += rotateSpeed * delta;
	}
	if (rotateVel > 0.0) {
		rotateVel = std::max(0.0, rotateVel - rotateSpeed * delta * rotateSlowSpeedMult);
	} else {
		rotateVel = std::min(0.0, rotateVel + rotateSpeed * delta * rotateSlowSpeedMult);
	}
	if (cont.boost && boostProgress > boostCooldown) {
		addVelocity(boostStrength * xMul, boostStrength * yMul);
		boostProgress = 0.0;
		if (!headless) {
			forwards->setFillColor(sf::Color(64, 255, 64));
			forwards->setRotation(90.f + rotation);
		}
	}
	if (cont.primaryfire && reloadProgress >= reload) {
		if (authority) {
			Missile* proj = new Missile();
			if (simulating) {
				simCleanupBuffer.push_back(proj);
			}
			proj->setPosition(x + (radius + proj->radius * 3.0) * xMul, y + (radius + proj->radius * 3.0) * yMul);
			addVelocity(-shootPower * xMul * proj->mass / mass, -shootPower * yMul * proj->mass / mass);
			proj->setVelocity(velX + shootPower * xMul, velY + shootPower * yMul);
			proj->rotation = rotation;
			proj->rotateVel = rotateVel;
			proj->owner = this;
			proj->target = target;
			if (isServer) {
				proj->syncCreation();
			}
		}
		reloadProgress -= reload;
	}
	if (cont.secondaryfire && secondaryCharge >= 1.0 && secondaryProgress >= secondaryReload) {
		if (authority) {
			Projectile* proj = new Projectile();
			proj->setPosition(x + (radius + proj->radius * 3.0) * xMul, y + (radius + proj->radius * 3.0) * yMul);
			addVelocity(-secondaryShootPower * xMul * proj->mass / mass, -secondaryShootPower * yMul * proj->mass / mass);
			double shootX = xMul;
			double shootY = yMul;
			if (target) {
				double dX            = target->x - x;
				double dY            = target->y - y;
				double headIn        = std::atan2(dY, dX);
				double dVx           = target->velX - velX;
				double dVy           = target->velY - velY;
				double dVtg          = dVy * cos(headIn) - dVx * sin(headIn);
				double Vin           = dVtg >= secondaryShootPower ? 0.0 : std::sqrt(secondaryShootPower * secondaryShootPower - dVtg * dVtg);
				double targetHeading = Vin == 0.0 ? headIn : std::atan2(dVtg, Vin) + headIn;
				double angdiff       = deltaAngleRad((double)rotationRad, targetHeading);
				double ang           = rotationRad + std::copysign(std::min(std::abs(angdiff), maxSecondaryAngle * degToRad), angdiff);
				shootX               = std::cos(ang);
				shootY               = std::sin(ang);
			}
			proj->setVelocity(velX + secondaryShootPower * shootX, velY + secondaryShootPower * shootY);
			proj->rotation = rotation;
			proj->rotateVel = rotateVel;
			if (simulating) {
				simCleanupBuffer.push_back(proj);
			}
			if (isServer) {
				proj->syncCreation();
			}
		}
		secondaryCharge -= 1.0;
		secondaryProgress -= secondaryReload;
		if (simulating && secondaryCharge < 1.0) {
			cont.secondaryfire = false;
		}
	}
}

void Triangle::draw() {
	Entity::draw();
	shape->setPosition(x + drawShiftX, y + drawShiftY);
	shape->setRotation(90.f + rotation);
	shape->setFillColor(sf::Color(color[0], color[1], color[2]));
	window->draw(*shape);
	g_camera.bindUI();
	float rotationRad = rotation * degToRad;
	double uiX = g_camera.w * 0.5 + (x - ownX) / g_camera.scale, uiY = g_camera.h * 0.5 + (y - ownY) / g_camera.scale;
	forwards->setPosition(uiX + 14.0 * cos(rotationRad), uiY + 14.0 * sin(rotationRad));
	if (ownEntity == this) {
		float reloadProgress = (-this->reloadProgress / reload + 1.0) * 40.f,
		secondaryChargeProgress = (-this->secondaryCharge / secondaryStockpile + 1.0) * 40.f,
		boostProgress = (-this->boostProgress / boostCooldown + 1.0) * 40.f;
		if (reloadProgress > 0.0) {
			sf::RectangleShape reloadBar(sf::Vector2f(reloadProgress, 4.f));
			reloadBar.setFillColor(sf::Color(255, 64, 64));
			reloadBar.setPosition(g_camera.w * 0.5f - reloadProgress / 2.f, g_camera.h * 0.5f + 40.f);
			window->draw(reloadBar);
		}
		if (secondaryChargeProgress > 0.0) {
			sf::RectangleShape reloadBar(sf::Vector2f(secondaryChargeProgress, 4.f));
			reloadBar.setFillColor(sf::Color(255, 64, 255));
			reloadBar.setPosition(g_camera.w * 0.5f - secondaryChargeProgress / 2.f, g_camera.h * 0.5f + 46.f);
			window->draw(reloadBar);
		}
		if (boostProgress > 0.0) {
			sf::RectangleShape boostReloadBar(sf::Vector2f(boostProgress, 4.f));
			boostReloadBar.setFillColor(sf::Color(64, 255, 64));
			boostReloadBar.setPosition(g_camera.w * 0.5f - boostProgress / 2.f, g_camera.h * 0.5f - 40.f);
			window->draw(boostReloadBar);
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

void Triangle::onEntityDelete(Entity* d) {
	Entity::onEntityDelete(d);
	if (target == d) {
		target = nullptr;
	}
}

uint8_t Triangle::type() {
	return Entities::Triangle;
}

CelestialBody::CelestialBody(double radius) : Entity() {
	this->radius = radius;
	this->mass = 1.0e18;
	this->gravitates = true;
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
	this->gravitates = true;
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
CelestialBody::CelestialBody(bool) {
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

void CelestialBody::postMassUpdate() {
	if (mass > gen_chandrasekharLimit) {
		setColor(0, 0, 0);
		blackhole = true;
		/*if (!star) { // prevent bouncy black holes
			star = true;
		}*/
		radius = 2.0 * G * mass / (CC);
	} else if (mass > gen_starMass * gen_starMassReq) {
		if (!simulating) {
			double colorFactor = pow(gen_starMass / mass, gen_starColorFactor);
			setColor((int)(255.0 * std::max(0.0, std::min(1.0, 2.0 - colorFactor))), (int)(255.0 * std::max(0.0, std::min(1.0, 1.9 - colorFactor))), (int)(255.0 * std::max(0.0, std::min(1.0, colorFactor - 0.72))));
			if (debug) {
				printf("New color: %u, %u, %u\n", color[0], color[1], color[2]);
			}
			star = true;
		}
		radius = gen_starRadius * pow(mass / gen_starMass, 1.0 / gen_starDensityFactor);
	} else {
		radius = pow(mass / gen_baseDensity, 1.0 / gen_densityFactor);
	}
	if (!headless && !simulating) {
		shape->setRadius(radius);
		shape->setOrigin(radius, radius);
	}
}

void CelestialBody::collide(Entity* with, bool specialOnly) {
	Entity::collide(with, specialOnly);
	if (!with->active) [[unlikely]] {
		return;
	}
	if (authority && star && with->type() == Entities::Triangle) {
		if (with->player || !isServer || with == ownEntity) {
			if (isServer) {
				std::string sendMessage;
				sendMessage.append("<").append(((Triangle*)with)->name).append("> has been incinerated.");
				relayMessage(sendMessage);
			}
			setupShip((Triangle*)with, isServer);
		} else {
			with->active = false;
		}
	} else if (authority && with->type() == Entities::CelestialBody) [[unlikely]] {
		if (mass >= with->mass) {
			if (!simulating && printPlanetMerges) {
				printf("Planetary collision: %u absorbed %u\n", id, with->id);
			}
			mass += with->mass;
			postMassUpdate();
			if (isServer) {
				sf::Packet collisionPacket;
				collisionPacket << Packets::PlanetCollision << id << mass;
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
	radius = 4.0;
	this->Entity::mass = Projectile::mass;
	this->color[0] = 180;
	this->color[1] = 60;
	this->color[2] = 60;
	if (!headless && !simulating) {
		shape = std::make_unique<sf::CircleShape>(radius, 6);
		shape->setOrigin(radius, radius);
		icon = std::make_unique<sf::CircleShape>(2.f, 6);
		icon->setOrigin(2.f, 2.f);
		icon->setFillColor(sf::Color(255, 60, 60));
	}
}

void Projectile::collide(Entity* with, bool specialOnly) {
	if (debug) {
		printf("bullet collision: %u-%u ", id, with->id);
	}
	if (with->type() == Entities::Triangle) {
		if (debug) {
			printf("of type triangle\n");
		}
		if (authority) {
			if (!simulating && (with->player || !isServer || with == ownEntity)) {
				if (isServer) {
					std::string sendMessage;
					sendMessage.append("<").append(((Triangle*)with)->name).append("> has been killed.");
					relayMessage(sendMessage);
				}
				setupShip((Triangle*)with, isServer);
			} else {
				with->active = false;
			}
			active = false;
		}
	} else if (with->type() == Entities::CelestialBody) {
		if (debug) {
			printf("of type CelestialBody\n");
		}
		if (authority) {
			active = false;
		}
	} else if (with->type() == Entities::Projectile || with->type() == Entities::Missile) {
		if (debug) {
			printf("of type Missile\n");
		}
		if (authority) {
			active = false;
			with->active = false;
		}
	} else {
		if (debug) {
			printf("of unaccounted type\n");
		}
		Entity::collide(with, specialOnly);
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

void Projectile::draw() {
	Entity::draw();
	shape->setPosition(x + drawShiftX, y + drawShiftY);
	shape->setRotation(90.f + rotation);
	shape->setFillColor(sf::Color(color[0], color[1], color[2]));
	window->draw(*shape);
	if (g_camera.scale > radius) {
		g_camera.bindUI();
		icon->setPosition(g_camera.w * 0.5 + (x - ownX) / g_camera.scale, g_camera.h * 0.5 + (y - ownY) / g_camera.scale);
		icon->setRotation(90.f + rotation);
		window->draw(*icon);
		g_camera.bindWorld();
	}
}

uint8_t Projectile::type() {
	return Entities::Projectile;
}

Missile::Missile() : Projectile() {
	radius = 4.0;
	fuel = startingFuel;
	this->Entity::mass = Missile::mass;
	this->color[0] = 180;
	this->color[1] = 0;
	this->color[2] = 0;
	if (!headless && !simulating) {
		shape = std::make_unique<sf::CircleShape>(radius, 3);
		shape->setOrigin(radius, radius);
		icon = std::make_unique<sf::CircleShape>(2.f, 3);
		icon->setOrigin(2.f, 2.f);
		icon->setFillColor(sf::Color(255, 0, 0));
		warning = std::make_unique<sf::CircleShape>(4.f, 4);
		warning->setOrigin(4.f, 4.f);
		warning->setFillColor(sf::Color(0, 0, 0, 0));
		warning->setOutlineColor(sf::Color(255, 0, 0, 255));
		warning->setOutlineThickness(1.f);
		warning->setRotation(45.f);
	}
}

// iteratively guesses time of intercept for an accelerating and linearly moving target
// reference frame should be rotated so that the linearly moving target is moving x-wards
// the solution for the actual equation would take more time to compute
double guessInterceptTime(double prev, double x0, double vel, double y0, double accel) {
	double x  = x0 + vel * prev;
	double d  = dst(x, y0);
	double dd = vel * x / d;
	return (dd + std::sqrt(dd * dd + 2.0 * accel * (d - dd * prev))) / (accel);
}
double accelAt(double time, double fuel, double thrust) {
	double fuel1 = fuel * std::exp(-time / fuel);
	double dV = thrust * (fuel - fuel1);
	return dV / time;
}
void Missile::update2() {
	if (target) {
		double dVx       = target->velX - velX;
		double dVy       = target->velY - velY;
		double dX        = target->x - x;
		double dY        = target->y - y;
		double refRot    = std::atan2(dVy, dVx);
		double vel       = dVx / std::cos(refRot);
		double projX     = dX * std::cos(refRot) + dY * std::sin(refRot);
		double projY     = dY * std::cos(refRot) - dX * std::sin(refRot);
		double accel     = this->accel * fuel / startingFuel;
		double itimef    = guessInterceptTime(0.0, -projX, -vel, projY, accel);
		double itime     = guessInterceptTime(itimef, -projX, -vel, projY, accelAt(itimef, fuel, accel));
		for (int i = 0; i < guidanceIterations; ++i) {
			itime  = guessInterceptTime(itime, -projX, -vel, projY, accelAt(itime, fuel, accel));
			itimef = guessInterceptTime(itimef, -projX, -vel, projY, accel);
		}
		bool fullthrust  = itimef < fuel * fullThrustThreshold;
		bool thrust      = ((prevItime - itime) < leastItimeDecrease * delta || fullthrust) && fuel > 0.0;
		double targetRot = std::atan2(dY + dVy * itime, dX + dVx * itime);
		double finangle  = degToRad * (rotation + std::abs(rotateVel) * rotateVel / (2.0 * rotateSpeed));
		rotateVel       += delta * (deltaAngleRad(finangle, targetRot) > 0.0 ? rotateSpeed : -rotateSpeed);
		if (std::abs(deltaAngleRad(targetRot, rotation * degToRad)) < maxThrustAngle && thrust) {
			double actaccel = fullthrust ? this->accel : accel;
			addVelocity(actaccel * delta * std::cos(targetRot), actaccel * delta * std::sin(targetRot));
			fuel -= delta * (fullthrust ? 1.0 : fuel / startingFuel);
		}
		/* if (!simulating) {
			printf("pX %f pY %f vel %f halfaccel %f g1 %f g2 %f it %f\n", projX, projY, vel, halfaccel, guess1, guess2, itime);
			printf("dX %f, dY %f, ang %f, target %f\n", dX, dY, radToDeg * std::atan2(dY, dX), radToDeg * targetRot);
		} */
		prevItime = itime;
	}
	Entity::update2();
}

void Missile::loadCreatePacket(sf::Packet& packet) {
	packet << type() << id << x << y << velX << velY << rotation << (target == nullptr ? std::numeric_limits<uint32_t>::max() : target->id) << (owner == nullptr ? std::numeric_limits<uint32_t>::max() : owner->id);
	if (debug) {
		printf("Sent id %d: %g %g %g %g\n", id, x, y, velX, velY);
	}
}
void Missile::unloadCreatePacket(sf::Packet& packet) {
	packet >> id >> x >> y >> velX >> velY >> rotation;
	uint32_t entityID, ownerID;
	packet >> entityID >> ownerID;
	target = entityID == std::numeric_limits<uint32_t>::max() ? nullptr : idLookup(entityID);
	owner = ownerID == std::numeric_limits<uint32_t>::max() ? nullptr : idLookup(ownerID);
	if (debug) {
		printf(", id %d: %g %g %g %g\n", id, x, y, velX, velY);
	}
}
void Missile::loadSyncPacket(sf::Packet& packet) {
	packet << id << x << y << velX << velY << rotation << fuel;
}
void Missile::unloadSyncPacket(sf::Packet& packet) {
	packet >> syncX >> syncY >> syncVelX >> syncVelY >> rotation >> fuel;
}

void Missile::simSetup() {
	Entity::simSetup();
	resFuel = fuel;
}
void Missile::simReset() {
	Entity::simReset();
	fuel = resFuel;
}

void Missile::draw() {
	Entity::draw();
	shape->setPosition(x + drawShiftX, y + drawShiftY);
	shape->setRotation(90.f + rotation);
	shape->setFillColor(sf::Color(color[0], color[1], color[2]));
	window->draw(*shape);
	if (g_camera.scale > radius) {
		g_camera.bindUI();
		icon->setPosition(g_camera.w * 0.5 + (x - ownX) / g_camera.scale, g_camera.h * 0.5 + (y - ownY) / g_camera.scale);
		icon->setRotation(90.f + rotation);
		window->draw(*icon);
		if (target && ownEntity && target == ownEntity) {
			warning->setPosition(g_camera.w * 0.5 + (x - ownX) / g_camera.scale, g_camera.h * 0.5 + (y - ownY) / g_camera.scale);
			window->draw(*warning);
		}
		g_camera.bindWorld();
	}
}

void Missile::onEntityDelete(Entity* d) {
	Entity::onEntityDelete(d);
	if (owner == d) {
		owner = nullptr;
	}
	if (target == d) {
		target = d->type() == Entities::Missile && ((Missile*)d)->owner != owner ? ((Missile*)d)->owner : nullptr;
	}
}

uint8_t Missile::type() {
	return Entities::Missile;
}

}
