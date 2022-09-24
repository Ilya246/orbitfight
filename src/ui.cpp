#include "camera.hpp"
#include "globals.hpp"
#include "math.hpp"
#include "net.hpp"
#include "strings.hpp"
#include "ui.hpp"

#include <SFML/Graphics.hpp>
#include <SFML/Window.hpp>

#include <thread>

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

size_t fitText(std::string& string, sf::Text& text, float maxWidth) {
    text.setString(string);
    for (size_t i = 0; i < string.size(); i++) {
        if (text.findCharacterPos(i).x - text.findCharacterPos(0).x > maxWidth) {
            text.setString(string.substr(0, i));
            return string.size() - i;
        }
    }
    return 0;
}

UIElement::UIElement() {
    body.setOutlineThickness(2.f);
    body.setOutlineColor(sf::Color(32, 32, 32, 192));
    body.setFillColor(sf::Color(64, 64, 64, 64));
    body.setPosition(0.f, 0.f);
    body.setSize(sf::Vector2f(0.f, 0.f));
}

UIElement::~UIElement() noexcept {
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

bool UIElement::isMousedOver() {
    return mousePos.x > x && mousePos.y > y && mousePos.x < x + width && mousePos.y < y + height;
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
    textbox.padding = 2.f;
    textbox.height = textCharacterSize + 3.f + textbox.padding * 2.f;
    textbox.text.setFont(*font);
    textbox.text.setCharacterSize(textCharacterSize);
    textbox.text.setFillColor(sf::Color::White);
    textbox.maxChars = messageLimit;
    mulWidthMax = 0.5f;
    mulWidthMin = 0.25f;
    mulHeightMax = 0.5f;
    mulHeightMin = 0.24f;
}

void ChatUI::update() {
    string = "";
    int displayMessageCount = std::min((int)((height - textbox.padding * 2.f - textbox.height) / (textCharacterSize + 3)), storedMessageCount);
    messageCursorPos = std::max(0, std::min(messageCursorPos, storedMessageCount - displayMessageCount));
    for (int i = messageCursorPos; i < messageCursorPos + displayMessageCount; i++) {
        string.insert(0, storedMessages[i] + "\n");
    }
    int overshoot = wrapText(string, text, width - padding * 2.f);
    for (int i = 0; i < overshoot; i++) {
        for (size_t c = 0; c < string.size(); c++) {
            if (string[c] == '\n' && c + 1 < string.size()) {
                string = string.substr(c + 1, string.size());
                break;
            }
        }
    }
    text.setString(string);
    text.setPosition(padding, g_camera.h - (textCharacterSize + 3) * (displayMessageCount + 1) - padding - textbox.padding * 2.f);
    UIElement::update();
    window->draw(text);
    textbox.update();
}

void ChatUI::resized() {
    float lerpf = std::max(std::min(1.f, (g_camera.w - widestAdjustAt) / narrowestAdjustAt), 0.f);
    width = std::min((float)(messageLimit * (textCharacterSize + 2)), g_camera.w * (lerpf * mulWidthMin + (1.f - lerpf) * mulWidthMax));
    lerpf = std::max(std::min(1.f, (g_camera.h - tallestAdjustAt) / shortestAdjustAt), 0.f);
    height = std::min((float)(storedMessageCount + 1) * (textCharacterSize + 3), g_camera.h * (lerpf * mulHeightMin + (1.f - lerpf) * mulHeightMax));
    body.setPosition(0.f, g_camera.h - height);
    body.setSize(sf::Vector2f(width, height));
    textbox.position(0, g_camera.h - textbox.height);
    textbox.width = width;
    textbox.body.setSize(sf::Vector2f(textbox.width, textbox.height));
}

void ChatUI::onKeyPress(sf::Keyboard::Key k) {
    if (!active || (activeTextbox != nullptr && activeTextbox != &textbox)) {
        return;
    }
    if (k == sf::Keyboard::Enter) {
        activeTextbox = activeTextbox == &textbox ? nullptr : &textbox;
        handledTextBoxSelect = true;
        if (textbox.fullString.size() == 0) {
            return;
        }
        if (textbox.fullString[0] == '/') {
            parseCommand(textbox.fullString.substr(1, textbox.fullString.size() - 1));
            textbox.fullString = "";
            textbox.stringChanged();
            return;
        }
        if (serverSocket) {
            sf::Packet chatPacket;
            chatPacket << Packets::Chat << textbox.fullString;
            serverSocket->send(chatPacket);
        } else {
            printPreferred(textbox.fullString);
        }
        textbox.fullString = "";
        textbox.stringChanged();
    }
}

void TextElement::update() {
    UIElement::update();
    window->draw(text);
}

void TextElement::resized() {
    text.setString(string);
    sf::FloatRect bounds = text.getLocalBounds();
    width = bounds.width + padding * 2.f;
    height = bounds.height + padding * 2.f;
    text.setOrigin(bounds.left, bounds.top);
    body.setSize(sf::Vector2f(width, height));
}

void TextElement::resizeY(float height) {
    this->height = height;
    body.setSize(sf::Vector2f(width, height));
    sf::FloatRect bounds = text.getLocalBounds();
    text.setOrigin(bounds.left, bounds.top + (bounds.height + padding * 2.f - height) * 0.5f);
}

void TextElement::position(float x, float y) {
    this->x = x;
    this->y = y;
    body.setPosition(x, y);
    text.setPosition(x + padding, y + padding);
}

void TextBoxElement::update() {
    TextElement::update();
    if (activeTextbox == this && std::sin(globalTime * TAU * 1.5) > 0.0) {
        sf::RectangleShape rect(sf::Vector2f(1.f, textCharacterSize));
        sf::FloatRect bounds = text.getGlobalBounds();
        rect.setPosition(text.findCharacterPos(cursorPos - viewPos).x, bounds.top);
        window->draw(rect);
    }
}

void TextBoxElement::resized() {
    width = width + padding * 2.f;
    height = height + padding * 2.f;
    body.setSize(sf::Vector2f(width, height));
}

void TextBoxElement::stringChanged() {
    std::string sub;
    if (fullString.size() > 0) {
        sub = fullString.substr(viewPos);
        cursorPos = std::min((size_t)cursorPos, fullString.size());
    } else {
        sub = "";
        cursorPos = 0;
        viewPos = 0;
        text.setString(sub);
        return;
    }
    size_t chars = fullString.size() - viewPos - fitText(sub, text, width - padding * 2.f);
    if (cursorPos - viewPos > chars) {
        viewPos = cursorPos - chars;
        stringChanged();
    }
}

void TextBoxElement::onKeyPress(sf::Keyboard::Key k) {
    if (activeTextbox != this) {
        return;
    }
    switch (k) {
        case sf::Keyboard::Enter: {
            if (!handledTextBoxSelect) {
                activeTextbox = nullptr;
            }
            break;
        }
        case sf::Keyboard::BackSpace: {
            if (fullString.size() > 0 && cursorPos > 0) {
                fullString.erase(cursorPos - 1, 1);
                cursorPos = std::max((size_t)0, cursorPos - 1);
                viewPos = viewPos == 0 ? 0 : viewPos - 1;
                stringChanged();
            }
            break;
        }
        case sf::Keyboard::Left: {
            cursorPos = cursorPos == 0 ? 0 : cursorPos - 1;
            viewPos = std::min(cursorPos, viewPos);
            stringChanged();
            break;
        }
        case sf::Keyboard::Right: {
            cursorPos = std::min(cursorPos + 1, fullString.size());
            stringChanged();
            break;
        }
        default:
            break;
    }
}

void TextBoxElement::onTextEntered(uint32_t c) {
    if (activeTextbox == this && c > 31 && c < 128 && fullString.size() < maxChars) {
        fullString.insert(cursorPos, 1, (char)c);
        cursorPos++;
        stringChanged();
    }
}

void TextBoxElement::handleMousePress() {
    activeTextbox = this;
}

MenuUI::MenuUI() {
    setState(state);
}

void MenuUI::update() {
    UIElement::update();
    for (TextElement* b : buttons) {
        if (b->active) {
            b->update();
        }
    }
}

void MenuUI::resized() {
    height = padding * 2.f;
    width = 0.f;
    for (TextElement* b : buttons) {
        width = std::max(width, b->width);
        height += b->height + buttonSpacing;
    }
    width += padding * 2.f;
    x = (g_camera.w - width) * 0.5f;
    y = (g_camera.h - height) * 0.5f;
    float curY = 0.f;
    for (TextElement* b : buttons) {
        b->position(x + (width - b->width) * 0.5f, y + padding + curY);
        curY += b->height + buttonSpacing;
    }
    body.setSize(sf::Vector2f(width, height));
    body.setPosition(x, y);
}

void MenuUI::setState(uint8_t state) {
    this->state = state;
    switch (state) {
        case MenuStates::Main: {
            for (TextElement* b : buttons) {
                delete b;
            }
            buttons.resize(4);
            for (int i = 0; i < 4; i++) {
                buttons[i] = new TextElement();
            }
            buttons[0]->string = "Freeplay";
            buttons[1]->string = "Connect To Server";
            buttons[2]->string = "Settings";
            buttons[3]->string = "Disconnect";
            float maxHeight = 0.f;
            for (TextElement* b : buttons) {
                b->padding = buttonPadding;
                b->text.setFont(*font);
                b->text.setCharacterSize(textCharacterSize);
                b->text.setFillColor(sf::Color::White);
                b->resized();
                maxHeight = std::max(maxHeight, b->height);
            }
            for (TextElement* b : buttons) {
                b->resizeY(maxHeight);
            }
            resized();
            break;
        }
        case MenuStates::ConnectMenu: {
            for (TextElement* b : buttons) {
                delete b;
            }
            buttons.resize(4);
            buttons[0] = new TextElement();
            buttons[1] = new TextBoxElement();
            buttons[2] = new TextElement();
            buttons[3] = new TextElement();
            buttons[0]->active = false;
            buttons[1]->body.setFillColor(sf::Color(64, 64, 64, 192));
            buttons[1]->width = connectBoxWidth;
            buttons[1]->height = textCharacterSize + 2.f;
            buttons[2]->string = "Connect";
            buttons[3]->string = "Cancel";
            for (TextElement* b : buttons) {
                b->padding = buttonPadding;
                b->text.setFont(*font);
                b->text.setCharacterSize(textCharacterSize);
                b->text.setFillColor(sf::Color::White);
                b->resized();
            }
            buttons[0]->text.setFillColor(sf::Color::Red);
            resized();
            break;
        }
    }
}

void MenuUI::connect() {
    std::string address = ((TextBoxElement*)buttons[1])->fullString;
    std::vector<std::string> addressPort;
    splitString(address, addressPort, ':');
    if (addressPort.size() == 0) {
        buttons[0]->string = "Invalid input.";
        buttons[0]->active = true;
        buttons[0]->resized();
        resized();
        return;
    }
    address = addressPort[0];
    if (addressPort.size() == 2) {
        if (std::regex_match(addressPort[1], int_regex)) {
            port = stoi(addressPort[1]);
        } else {
            buttons[0]->string = "Port must be integer.";
            buttons[0]->active = true;
            buttons[0]->resized();
            resized();
            return;
        }
    }
    sf::TcpSocket* newSocket = new sf::TcpSocket();
    if (newSocket->connect(address, port) != sf::Socket::Done) {
        buttons[0]->string = "Could not connect to server.";
        buttons[0]->active = true;
        buttons[0]->resized();
        resized();
        delete newSocket;
        return;
    }
    delete serverSocket;
    serverSocket = newSocket;
    fullClear(true);
    onServerConnection();
    if (activeTextbox == buttons[1]) {
        activeTextbox = nullptr;
    }
    active = false;
    setState(MenuStates::Main);
}

void selfListen() {
    connectListener->accept(sparePlayer->tcpSocket);
}

void MenuUI::onMousePress(sf::Mouse::Button b) {
    if (!isMousedOver() || !active || b != sf::Mouse::Button::Left) {
        return;
    }
    for (TextElement* b : buttons) {
        if (activeTextbox == b) {
            activeTextbox = nullptr;
        }
    }
    switch (state) {
        case MenuStates::Main: {
            if (buttons[0]->isMousedOver()) {
                fullClear(true);
                generateSystem();
                ownEntity = new Triangle();
                ((Triangle*)ownEntity)->name = name;
                setupShip(ownEntity, false);
                delete serverSocket;
                serverSocket = nullptr;
                authority = true;
                active = false;
            } else if (buttons[1]->isMousedOver()) {
                setState(MenuStates::ConnectMenu);
            } else if (buttons[2]->isMousedOver()) {
                printPreferred("Not implemented yet");
            } else if (buttons[3]->isMousedOver()) {
                fullClear(true);
                delete serverSocket;
                serverSocket = nullptr;
            }
            break;
        }
        case MenuStates::ConnectMenu: {
            if (buttons[1]->isMousedOver()) {
                ((TextBoxElement*)buttons[1])->handleMousePress();
            } else if (buttons[2]->isMousedOver()) {
                connect();
            } else if (buttons[3]->isMousedOver()) {
                setState(MenuStates::Main);
            }
            break;
        }
    }
}

void MenuUI::onKeyPress(sf::Keyboard::Key k) {
    switch (k) {
        case sf::Keyboard::Escape: {
            active = !active;
            break;
        }
        case sf::Keyboard::Enter: {
            if (state == MenuStates::ConnectMenu && activeTextbox == buttons[1]) {
                connect();
            }
            break;
        }
        default:
            break;
    }
}

}
