#include "math.hpp"

#include <cmath>
#include <random>

namespace obf{

static std::random_device rand_d;
static std::mt19937 rand_g(rand_d());

double dst2(double x, double y) {
	return x * x + y * y;
}
double dst(double x, double y) {
	return sqrt(dst2(x, y));
}
float rand_f(float from, float to) {
	return std::uniform_real_distribution(from, to)(rand_g);
}
bool chance(float number) {
	return std::uniform_real_distribution(0.f, 1.f)(rand_g) < number;
}
float lerpRotation(float a, float b, float c) {
	return a + c * deltaAngle(a, b);
}

}
