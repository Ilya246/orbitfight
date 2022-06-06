#include "entities.hpp"
#include "globals.hpp"
#include "math.hpp"

#include <cmath>

#include <SFML/Graphics.hpp>

namespace obf {

Entity::Entity(double x, double y)
		: x(x), y(y) {
	updateGroup.push_back(this);
}

Entity::~Entity() noexcept {
	// swap remove this from updateGroup
	// O(n) complexity since iterates whole thing at worst
	for (size_t i = 0; i < updateGroup.size(); i++) {
		if (updateGroup[i] == this) [[unlikely]] {
			updateGroup[i] = updateGroup[updateGroup.size() - 1];
			updateGroup.pop_back();
			break;
		}
	}
}

void Entity::update() {
	x += velX * delta;
	y += velY * delta;
}


Triangle::Triangle(double x, double y, double radius)
		: Entity(x, y) {
	shape = sf::CircleShape(radius, 3);
	shape.setOrigin(radius, radius);
}

void Triangle::update() {
	addVelocity((mousePos.x - x) * delta * 0.0001, (mousePos.y - y) * delta * 0.0001);
	rotation = lerpRotation(rotation, std::atan2(mousePos.y - y, mousePos.x - x) * radToDeg, delta * 0.1f);
	Entity::update();
	shape.setPosition(x, y);
	shape.setRotation(rotation + 90.f);
}
void Triangle::draw() {
	window->draw(shape);
}

}
