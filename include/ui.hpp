#pragma once
#include "globals.hpp"

#include <SFML/Graphics.hpp>
#include <SFML/Window.hpp>

namespace obf {

int wrapText(std::string& string, sf::Text& text, double maxWidth);

struct UIElement {
    UIElement();
    virtual ~UIElement() noexcept;

    virtual void update();
    virtual void resized();

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

// NOTE: the x, y, text and string properties of this struct should be managed by the owning struct
struct TextElement : UIElement {
    void update() override;
    void resized() override; // sets width, height of text and the UI body according to desired padding
    void position(float x, float y); // positions text and UI body
    void resizeY(float height); // force resize to desired size, position text accordingly, assumes string is unchanged

    std::string string;
    sf::Text text;
};

// width and height should be set by owning struct
struct TextBoxElement : TextElement, KeyPressListener, TextEnteredListener {
    void update() override;
    void resized() override;

    void stringChanged();

    void onKeyPress(sf::Keyboard::Key k) override;
    void onTextEntered(uint32_t c) override;
    void handleMousePress();

    std::string fullString;
    int viewPos = 0, cursorPos = 0;
};

struct MiscInfoUI : UIElement {
    MiscInfoUI();

    void update() override;

    sf::Text text;
};

struct ChatUI : TextElement, KeyPressListener {
    ChatUI();

    void update() override;
    void resized() override;

    void onKeyPress(sf::Keyboard::Key k) override;

    TextBoxElement textbox;
};

namespace MenuStates {

constexpr uint8_t Main = 0,
	ConnectMenu = 1,
	SettingsMenu = 2;
};

struct MenuUI : UIElement, MousePressListener, KeyPressListener {
    MenuUI();

    void update() override;
    void resized() override;

    void setState(uint8_t state);

    void connect(); // for the ConnectMenu state to prevent code duplication

    void onMousePress(sf::Mouse::Button b) override;
    void onKeyPress(sf::Keyboard::Key k) override;

    std::vector<TextElement*> buttons;

    uint8_t state = MenuStates::Main;

    float buttonSpacing = 5.f, buttonPadding = 2.f, connectBoxWidth = 250.f;
};

}
