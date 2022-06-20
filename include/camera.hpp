#pragma once

#include <SFML/Graphics/View.hpp>

namespace obf {

using Pos = sf::Vector2f;

struct Camera {
	void resize();
	// multiply scale and reload view
	void zoom(float by);

	void bindWorld() const;
	void bindUI() const;

	// mouse position on the window
	Pos windowMouse() const;
	// mouse position in the world
	Pos worldMouse() const;

	float scale = 0;
	int w, h;
	Pos pos;

private:
	sf::View worldView, uiView;
};

inline Camera g_camera;

}
