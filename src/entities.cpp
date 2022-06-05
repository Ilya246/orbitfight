#include "entities.hpp"
#include "globals.hpp"

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


Circle::Circle(double x, double y, double radius)
		: Entity(x, y) {
	shape = sf::CircleShape(radius);
	shape.setOrigin(radius / 2, radius / 2);
}

void Circle::update() {
	addVelocity((mousePos.x - x) * delta * 0.0001, (mousePos.y - y) * delta * 0.0001);
	Entity::update();
	shape.setPosition(x, y);
	window.draw(shape);
}

}
