#pragma once

namespace obf{
    inline int windowWidth = 200;
    inline int windowHeight = 200; // read-only
    inline sf::RenderWindow window(sf::VideoMode(windowWidth, windowHeight), "Test");
}
