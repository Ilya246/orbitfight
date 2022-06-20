#include "camera.hpp"
#include "globals.hpp"

namespace obf {

void Camera::resize() {
	auto size = window->getSize();
	w = size.x;
	h = size.y;

	uiView.setSize(w, h);
	uiView.setCenter(w / 2, h / 2);
	worldView.setSize(w * scale, h * scale);
}

void Camera::zoom(float by) {
	scale *= by;
	resize();
}

void Camera::bindWorld() const {
	auto wv = worldView;
	wv.setCenter(pos.x, pos.y);
	window->setView(wv);
}

void Camera::bindUI() const {
	window->setView(uiView);
}

Pos Camera::windowMouse() const {
	auto screen = sf::Mouse::getPosition(*window);
	return window->mapPixelToCoords(screen, uiView);
}

Pos Camera::worldMouse() const {
	auto screen = sf::Mouse::getPosition(*window);
	auto wv = worldView;
	wv.setCenter(pos.x, pos.y);
	return window->mapPixelToCoords(screen, wv);
}

}
