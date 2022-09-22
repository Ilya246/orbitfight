#pragma once

#include <cmath>

namespace obf {

constexpr double PI = atan(1.0) * 4.0;
constexpr double TAU = atan(1.0) * 8.0;
constexpr double radToDeg = 180.0 / PI;
constexpr double degToRad = PI / 180.0;

// physics
constexpr double C = 3.0e8;
constexpr double CC = C * C;

double dst2(double, double);
double dst(double, double);
float rand_f(float, float);
bool chance(float);
template <typename T>
T deltaAngle(T a, T b) {
	T diff = fmod(b - a, 360.0);
	return diff + 360.0 * ((diff < -180.0) - (diff > 180.0));
}
template <typename T>
T deltaAngleRad(T a, T b) {
	T diff = fmod(b - a, TAU);
	return diff + TAU * ((diff < -PI) - (diff > PI));
}
template <typename T>
T absMax(T a, T b) {
	return std::abs(a) > std::abs(b) ? a : b;
}
float lerpRotation(float, float, float);

}
