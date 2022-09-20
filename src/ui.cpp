#include "camera.hpp"
#include "globals.hpp"
#include "math.hpp"
#include "ui.hpp"

#include <SFML/Graphics.hpp>

using namespace obf;

namespace obf {

void wrapText(std::string& string, sf::Text& text, double maxWidth) {
    text.setString(string);
    for (size_t i = 0; i < string.size(); i++) {
        if (text.findCharacterPos(i).x - text.findCharacterPos(0).x > maxWidth) {
            if (i == 0) [[unlikely]] {
                throw std::runtime_error("Couldn't wrap text: width too small.");
            }
            string.insert(i - 1, 1, '\n');
            text.setString(string);
        }
    }
}

UIElement::UIElement() {
    body.setOutlineThickness(2.f);
    body.setOutlineColor(sf::Color(32, 32, 32, 192));
    body.setFillColor(sf::Color(64, 64, 64, 64));
    body.setPosition(0.f, 0.f);
    body.setSize(sf::Vector2f(0.f, 0.f));
}

void UIElement::update() {
    window->draw(body);
}

void UIElement::resized() {
    double lerpf = std::max(std::min(1.0, (g_camera.w - widestAdjustAt) / narrowestAdjustAt), 0.0);
    width = g_camera.w * (lerpf * sideUIWidthMin + (1.0 - lerpf) * sideUIWidthMax);
    lerpf = std::max(std::min(1.0, (g_camera.h - tallestAdjustAt) / shortestAdjustAt), 0.0);
    height = g_camera.h * (lerpf * sideUIHeightMin + (1.0 - lerpf) * sideUIHeightMax);
}

MiscInfoUI::MiscInfoUI() {
    text.setFont(*font);
    text.setCharacterSize(textCharacterSize);
    text.setFillColor(sf::Color::White);
    text.setPosition(padding, padding);
}

void MiscInfoUI::update() {
    std::string info = "";
    info.append("FPS: ").append(std::to_string(framerate))
    .append("\nPing: ").append(std::to_string((int)(lastPing * 1000.0))).append("ms");
    if (lastTrajectoryRef) {
        info.append("\nDistance: ").append(std::to_string((int)(dst(ownX - lastTrajectoryRef->x, ownY - lastTrajectoryRef->y))));
        if (ownEntity) [[likely]] {
            info.append("\nVelocity: ").append(std::to_string((int)(dst(ownEntity->velX - lastTrajectoryRef->velX, ownEntity->velY - lastTrajectoryRef->velY) * 60.0)));
        }
    }
    wrapText(info, text, width - padding * 2.0);
    sf::FloatRect bounds = text.getLocalBounds();
    double actualWidth = bounds.width + padding * 2.0;
    height = bounds.height + padding * 2.0;
    body.setSize(sf::Vector2f(actualWidth, height));
    UIElement::update();
    window->draw(text);
}

}
