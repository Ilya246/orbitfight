#pragma once
#include <vector>

namespace obf {

struct Entity;

struct EventListener {
    inline EventListener() {
        listeners.push_back(this);
    }
    inline ~EventListener() {
        for (size_t i = 0; i < listeners.size(); i++) {
            if (listeners[i] == this) {
                listeners[i] = listeners[listeners.size() - 1];
                listeners.pop_back();
                break;
            }
        }
    }
    inline static std::vector<EventListener*> listeners;
};

struct EntityDeleteListener : EventListener {
    virtual void onEntityDelete(Entity* d) = 0;
};

}
