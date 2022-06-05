#include <SFML/Graphics.hpp>
#include "globals.hpp"

using namespace obf;

// entity type definitions

class positionEntity{
    public:
        double x;
        double y;
};
class updateEntity{
    public:
        virtual void update();
};

// actual entities

class Circle: public positionEntity, public updateEntity{
    public:
        sf::CircleShape shape;
        Circle(float radius){
            shape = sf::CircleShape(radius);
        }
        void update(){
            window.draw(shape);
        }
};
