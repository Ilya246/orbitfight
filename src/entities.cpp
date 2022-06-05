#include <cmath>
#include <iostream>
#include <SFML/Graphics.hpp>
#include "entities.hpp"
#include "globals.hpp"

using namespace obf;

void positionEntity::setPosition(double x, double y){
    this->x = x;
    this->y = y;
}

Circle::Circle(double x, double y, double radius){
    shape = sf::CircleShape(radius);
    this->setPosition(x, y);
    updateGroup.push_back(this);
}
void Circle::setPosition(double x, double y){
    this->positionEntity::setPosition(x, y);
    this->shape.setPosition(x, y);
}
void Circle::update(){
    this->setPosition(std::lerp(this->x, mousePos.x, std::min(0.0001f / delta, 1.f)), std::lerp(this->y, mousePos.y, std::min(0.0001f / delta, 1.f)));
    std::cout << this->x << this->y << mousePos.x << mousePos.y << std::endl;
    window.draw(shape);
}
