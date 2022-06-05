#include "entities.hpp"
#include "globals.hpp"

#include <SFML/Graphics.hpp>

using namespace obf;

int main() {
	Circle circle(10, 10, 500);
	while (window.isOpen()) {
		mousePos = sf::Mouse::getPosition(window);
		sf::Event event;
		while (window.pollEvent(event)) {
			switch (event.type) {
			case sf::Event::Closed:
				window.close();
				break;
			case sf::Event::Resized:
				window.setView(sf::View(sf::FloatRect(0, 0, event.size.width, event.size.height)));
			default:
				break;
			}
		}

		window.clear(sf::Color(25, 5, 40));
		for (auto *entity : updateGroup){
			entity->update();
		}

		window.display();
		delta = deltaClock.restart().asSeconds() * 60;
		globalTime = globalClock.getElapsedTime().asSeconds();
	}
}
