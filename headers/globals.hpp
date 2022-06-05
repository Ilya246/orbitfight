#pragma once
#include <SFML/Graphics.hpp>
#include <vector>
#include "entities.hpp"

namespace obf{
    inline sf::RenderWindow window(sf::VideoMode(200, 200), "Test");
    inline std::vector<updateEntity*> updateGroup;
    inline sf::Vector2i mousePos = sf::Mouse::getPosition(window);
    inline sf::Clock deltaClock;
    inline sf::Clock globalClock;
    inline float delta = 1 / 60;
    inline float globalTime = 0;
}
