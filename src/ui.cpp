#include "camera.hpp"
#include "globals.hpp"
#include "math.hpp"
#include "ui.hpp"

#include <SFML/Graphics.hpp>

using namespace obf;

namespace obf {

int wrapText(std::string& string, sf::Text& text, float maxWidth) {
    int newlines = 0;
    text.setString(string);
    for (size_t i = 0; i < string.size(); i++) {
        if (text.findCharacterPos(i).x - text.findCharacterPos(0).x > maxWidth) {
            if (i == 0) [[unlikely]] {
                throw std::runtime_error("Couldn't wrap text: width too small.");
            }
            string.insert(i - 1, 1, '\n');
            text.setString(string);
            newlines++;
        }
    }
    return newlines;
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
    float lerpf = std::max(std::min(1.f, (g_camera.w - widestAdjustAt) / narrowestAdjustAt), 0.f);
    width = g_camera.w * (lerpf * mulWidthMin + (1.f - lerpf) * mulWidthMax);
    lerpf = std::max(std::min(1.f, (g_camera.h - tallestAdjustAt) / shortestAdjustAt), 0.f);
    height = g_camera.h * (lerpf * mulHeightMin + (1.f - lerpf) * mulHeightMax);
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
    wrapText(info, text, width - padding * 2.f);
    sf::FloatRect bounds = text.getLocalBounds();
    float actualWidth = bounds.width + padding * 2.f;
    height = bounds.height + padding * 2.f;
    body.setSize(sf::Vector2f(actualWidth, height));
    UIElement::update();
    window->draw(text);
}

ChatUI::ChatUI() {
    text.setFont(*font);
    text.setCharacterSize(textCharacterSize);
    text.setFillColor(sf::Color::White);
    mulWidthMax = 0.4f;
    mulWidthMin = 0.35f;
    mulHeightMax = 0.4f;
    mulHeightMin = 0.3f;
}

void ChatUI::update() {
    std::string chatString = "";
    int displayMessageCount = std::min((int)(height / (textCharacterSize + 3)) - 1, storedMessageCount);
    messageCursorPos = std::max(0, std::min(messageCursorPos, storedMessageCount - displayMessageCount));
    int countOffset = 0;
    chatString.append("> " + chatBuffer);
    if (chatting && std::sin(globalTime * TAU * 1.5) > 0.0) {
        chatString.append("|");
    }
    for (int i = messageCursorPos; i < messageCursorPos + displayMessageCount - countOffset; i++) {
        chatString.insert(0, storedMessages[i] + "\n");
        text.setString(chatString);
        if (text.getLocalBounds().width + padding * 2.f > width) {
            countOffset += wrapText(chatString, text, width - padding * 2.f);
        }
    }
    text.setString(chatString);
    displayMessageCount++;
    text.setPosition(padding, g_camera.h - (textCharacterSize + 3) * displayMessageCount - padding);
    UIElement::update();
    window->draw(text);
}

void ChatUI::resized() {
    float lerpf = std::max(std::min(1.f, (g_camera.w - widestAdjustAt) / narrowestAdjustAt), 0.f);
    width = g_camera.w * (lerpf * mulWidthMin + (1.f - lerpf) * mulWidthMax);
    lerpf = std::max(std::min(1.f, (g_camera.h - tallestAdjustAt) / shortestAdjustAt), 0.f);
    height = std::min((float)(storedMessageCount + 1) * (textCharacterSize + 3), g_camera.h * (lerpf * mulHeightMin + (1.f - lerpf) * mulHeightMax));
    body.setPosition(0.f, g_camera.h - height);
    body.setSize(sf::Vector2f(width, height));
}

}
