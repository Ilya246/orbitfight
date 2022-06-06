#pragma once

#include <cmath>

namespace obf {

inline const double PI = std::atan(1) * 4;
inline const double radToDeg = 180.d / PI;

float rand_f(float, float);
float deltaAngle(float, float);
float lerpRotation(float, float, float);

}
