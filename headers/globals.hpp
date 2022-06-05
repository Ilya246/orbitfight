#pragma once
#include <vector>

namespace obf{
    inline int windowWidth = 200;
    inline int windowHeight = 200; // read-only
    inline sf::Clock deltaClock;
    inline sf::Clock globalClock;
    inline float delta = 1 / 60;
    inline float globalTime = 0;
    inline sf::RenderWindow window(sf::VideoMode(windowWidth, windowHeight), "Test");
    inline std::vector<updateEntity*> updateGroup;
}
