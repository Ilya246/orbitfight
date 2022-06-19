#include "math.hpp"

#include <cmath>
#include <random>

namespace obf{

static std::random_device rand_d;
static std::mt19937 rand_g(rand_d());

float rand_f(float from, float to){
	return std::uniform_real_distribution(from, to)(rand_g);
}
float deltaAngle(float a, float b){
	float diff = std::fmod(b - a, 360.f);
	return diff + 360.f * (diff < -180.f) - 360.f * (diff > 180.f);
}
float lerpRotation(float a, float b, float c){
	return a + c * deltaAngle(a, b);
}

}
