#pragma once

#include <cmath>

namespace obf {

constexpr double PI = atan(1.0) * 4.0;
constexpr double radToDeg = 180.0 / PI;
constexpr double degToRad = PI / 180.0;

double dst(double, double);
float rand_f(float, float);
float deltaAngle(float, float);
float lerpRotation(float, float, float);

}
