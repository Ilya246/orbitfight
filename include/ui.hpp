#pragma once
#include "globals.hpp"

#include <SFML/Graphics.hpp>

namespace obf {

int wrapText(std::string& string, sf::Text& text, double maxWidth);

struct UIElement {
    UIElement();

    virtual void update();
    virtual void resized();
    virtual void pressed(sf::Mouse::Button button);

    bool isMousedOver();

    float x = 0.0, y = 0.0, width = 0.0, height = 0.0,
    padding = 5.0,
    mulWidthMin = 0.2, mulWidthMax = 0.4,
    widestAdjustAt = 200.0, narrowestAdjustAt = 800.0,
    mulHeightMin = 0.15, mulHeightMax = 0.3,
    tallestAdjustAt = 200.0, shortestAdjustAt = 800.0;
    bool active = true;

    sf::RectangleShape body;
};

/* struct Button : UIElement {
    Button();

    void update() override;

    std::string text;
}; */

struct MiscInfoUI : UIElement {
    MiscInfoUI();

    void update() override;

    sf::Text text;
};

struct ChatUI : UIElement {
    ChatUI();

    void update() override;
    void resized() override;

    sf::Text text;
};

/* namespace MenuStates {

constexpr uint8_t Main = 0,
	ConnectMenu = 1,
	SettingsMenu = 2;
};

struct MenuUI : UIelement {
    MenuUI();

    void update() override;
    void resized() override;

    Button buttons[4];

    uint8_t state = MenuStates::Main;
}; */

}
