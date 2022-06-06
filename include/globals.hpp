#pragma once

#include <vector>

#include <SFML/Graphics.hpp>

namespace obf {

struct Entity;

inline sf::RenderWindow window(sf::VideoMode(500, 500), "Test");
inline std::vector<Entity*> updateGroup;
inline sf::Vector2i mousePos = sf::Mouse::getPosition(window);
inline sf::Clock deltaClock, globalClock;
inline float delta = 1.0 / 60.0,
	globalTime = 0;

}
