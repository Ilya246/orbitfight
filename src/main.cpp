#include "entities.hpp"
#include "globals.hpp"

#include <SFML/Graphics.hpp>

#include <cstring>

using namespace obf;

int main(int argc, char** argv) {
	for (int i = 1; i < argc; i++) {
    	headless |= !std::strcmp(argv[i], "--headless");
	}
	if(!headless){
		window = new sf::RenderWindow(sf::VideoMode(500, 500), "Test");
	}
	Triangle triangle(50, 50, 75);
	while (window->isOpen()) {
		if(!headless){
			mousePos = sf::Mouse::getPosition(*window);
		}
		sf::Event event;
		while (window->pollEvent(event)) {
			switch (event.type) {
			case sf::Event::Closed:
				window->close();
				break;
			case sf::Event::Resized:
				window->setView(sf::View(sf::FloatRect(0, 0, event.size.width, event.size.height)));
			default:
				break;
			}
		}
		for (auto *entity : updateGroup){
			entity->update();
		}
		if(!headless){
			window->clear(sf::Color(25, 5, 40));
			for (auto *entity : updateGroup){
				entity->draw();
			}
			window->display();
		}
		delta = deltaClock.restart().asSeconds() * 60;
		globalTime = globalClock.getElapsedTime().asSeconds();
	}
}
