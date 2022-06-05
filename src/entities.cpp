#include <SFML/Graphics.hpp>
#include "globals.hpp"
#include "entities.hpp"

using namespace obf;

Circle::Circle(double radius){
    shape = sf::CircleShape(radius);
}
void Circle::update(){
    window.draw(shape);
}
