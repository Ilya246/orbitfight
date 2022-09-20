#pragma once

#include <SFML/Graphics.hpp>

namespace obf {

void wrapText(std::string& string, sf::Text& text, double maxWidth);

struct UIElement {
    UIElement();

    virtual void update();
    virtual void resized();

    double x = 0.0, y = 0.0, width = 0.0, height = 0.0,
    padding = 5.0,
    sideUIWidthMin = 0.2, sideUIWidthMax = 0.4,
    widestAdjustAt = 200.0, narrowestAdjustAt = 800.0,
    sideUIHeightMin = 0.15, sideUIHeightMax = 0.3,
    tallestAdjustAt = 200.0, shortestAdjustAt = 800.0;

    sf::RectangleShape body;
};

struct MiscInfoUI : UIElement {
    MiscInfoUI();

    void update() override;

    sf::Text text;
};

}
