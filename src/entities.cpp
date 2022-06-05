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
void posVelEntity::setVelocity(double x, double y){
    this->velX = x;
    this->velY = y;
}
void posVelEntity::addVelocity(double x, double y){
    this->velX += x;
    this->velY += y;
}

Circle::Circle(double x, double y, double radius){
    shape = sf::CircleShape(radius);
    this->setPosition(x, y);
    updateGroup.push_back(this);
}
void Circle::setPosition(double x, double y){
    this->posVelEntity::positionEntity::setPosition(x, y);
    this->shape.setPosition(x, y);
}
void Circle::update(){
    this->addVelocity((mousePos.x - this->x) * delta * 0.0001, (mousePos.y - this->y) * delta * 0.0001);
    this->setPosition(this->x + this->posVelEntity::velX, this->y + + this->posVelEntity::velY);
    window.draw(shape);
}
