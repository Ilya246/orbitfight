#pragma once
#include <vector>

namespace obf {

struct Entity;

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

}
