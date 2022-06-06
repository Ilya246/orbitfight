#pragma once

#include <vector>

#include <SFML/Graphics.hpp>

namespace obf {

struct Entity;

inline sf::RenderWindow* window = nullptr;
inline std::vector<Entity*> updateGroup;
inline sf::Vector2i mousePos;
inline sf::Clock deltaClock, globalClock;
inline float delta = 1.0 / 60.0,
	globalTime = 0;
inline bool headless = false;

}
