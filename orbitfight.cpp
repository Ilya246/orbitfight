#include <SFML/Graphics.hpp>

int main(){
    int windowWidth = 200;
    int windowHeight = 200; // read-only
    sf::RenderWindow window(sf::VideoMode(windowWidth, windowHeight), "Test");
    sf::CircleShape shape(50.f);
    shape.setFillColor(sf::Color::Green);
    sf::Clock deltaClock;
    float delta = 1 / 60;
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
                    break;
                default:
                    break;
            }
        }
        window.clear();
        window.draw(shape);
        window.display();
        delta = deltaClock.restart().asSeconds();
    }
    return 0;
}
