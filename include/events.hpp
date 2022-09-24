#pragma once
#include <SFML/Window.hpp>

#include <vector>

namespace obf {

struct Entity;

struct KeyPressListener {
    inline KeyPressListener() {
        listeners.push_back(this);
    }
    inline ~KeyPressListener() {
        for (size_t i = 0; i < listeners.size(); i++) {
            if (listeners[i] == this) {
                listeners[i] = listeners[listeners.size() - 1];
                listeners.pop_back();
                break;
            }
        }
    }

    virtual void onKeyPress(sf::Keyboard::Key k) = 0;

    inline static std::vector<KeyPressListener*> listeners;
};

struct TextEnteredListener {
    inline TextEnteredListener() {
        listeners.push_back(this);
    }
    inline ~TextEnteredListener() {
        for (size_t i = 0; i < listeners.size(); i++) {
            if (listeners[i] == this) {
                listeners[i] = listeners[listeners.size() - 1];
                listeners.pop_back();
                break;
            }
        }
    }

    virtual void onTextEntered(uint32_t c) = 0;

    inline static std::vector<TextEnteredListener*> listeners;
};

struct EntityDeleteListener {
    inline EntityDeleteListener() {
        listeners.push_back(this);
    }
    inline ~EntityDeleteListener() {
        for (size_t i = 0; i < listeners.size(); i++) {
            if (listeners[i] == this) {
                listeners[i] = listeners[listeners.size() - 1];
                listeners.pop_back();
                break;
            }
        }
    }

    virtual void onEntityDelete(Entity* d) = 0;

    inline static std::vector<EntityDeleteListener*> listeners;
};

struct MousePressListener {
    inline MousePressListener() {
        listeners.push_back(this);
    }
    inline ~MousePressListener() {
        for (size_t i = 0; i < listeners.size(); i++) {
            if (listeners[i] == this) {
                listeners[i] = listeners[listeners.size() - 1];
                listeners.pop_back();
                break;
            }
        }
    }
    virtual void onMousePress(sf::Mouse::Button b) = 0;

    inline static std::vector<MousePressListener*> listeners;
};

}
