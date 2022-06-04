#include <SFML/Graphics.hpp>
#include <globals.hpp>

using namespace obf;

int main(){
    sf::CircleShape shape(50.f);
    shape.setFillColor(sf::Color::Green);
    sf::Clock deltaClock;
    sf::Clock globalClock;
    float delta = 1 / 60;
    float globalTime = 0;
    while(window.isOpen()){
        sf::Vector2i mousePos = sf::Mouse::getPosition(window);
        sf::Event event;
        while(window.pollEvent(event)){
            switch(event.type){
                case sf::Event::Closed:
                    window.close();
                    break;
                case sf::Event::Resized:
                    windowWidth = event.size.width;
                    windowHeight = event.size.height;
                    window.setView(sf::View(sf::FloatRect(0, 0, event.size.width, event.size.height)));
                    break;
                default:
                    break;
            }
        }
        // shape.setRadius(50 + sin(globalTime));
        // sf::Vector2f pos = shape.getPosition();
        // shape.setPosition(lerp(shape.getX(), mousePos.x, 0.5 * delta), lerp(shape.getY(), mousePos.y, 0.5 * delta));
        window.clear(sf::Color(25,5,40));
        window.draw(shape);
        window.display();
        delta = deltaClock.restart().asSeconds() * 60;
        globalTime = globalClock.getElapsedTime().asSeconds();
    }
    return 0;
}
