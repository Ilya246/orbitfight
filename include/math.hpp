#pragma once

#include <cmath>

namespace obf {

constexpr double PI = atan(1) * 4;
constexpr double radToDeg = 180.0 / PI;

float rand_f(float, float);
float deltaAngle(float, float);
float lerpRotation(float, float, float);

}
