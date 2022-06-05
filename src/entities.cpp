#include <SFML/Graphics.hpp>
#include "entities.hpp"
#include "globals.hpp"

using namespace obf;

Circle::Circle(double radius){
    shape = sf::CircleShape(radius);
    updateGroup.push_back(this);
}
void Circle::update(){
    window.draw(shape);
}
