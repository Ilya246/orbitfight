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

bool AABB_collides(double x1, double y1, double sx1, double sy1, double x2, double y2, double sx2, double sy2) {
	return x1 + sx1 > x2 && y1 + sy1 > y2 && x1 < x2 + sx2 && y1 < y2 + sy2;
}
double linesCollideX(double x11, double y11, double x21, double y21, double x12, double y12, double x22, double y22) {
	double collx = x12 - x11 + (y12 - y11) / ((y21 - y11) / (x21 - x11) - (y22 - y12) / (x22 - x12));
	return (collx > x11 && collx < x21 && collx > x12 && collx < x22) ? collx : INFINITY;
}
bool pointInAABB(double x, double y, double x1, double y1, double sx, double sy) {
	return x < x1 + sx && y < y1 + sy && x > x1 && y > y1;
}
bool AABB_collideLine(double xr, double yr, double sx, double sy, double x1, double y1, double x2, double y2) {
	if (pointInAABB(x1, y1, xr, yr, sx, sy) || pointInAABB(x2, y2, xr, yr, sx, sy)) {
		return true;
	}
	double slope = (y2 - y1) / (x2 - x1);
	double xi1 = x1 + (yr - y1) / slope, xi2 = x1 + (yr + sy - y1) / slope;
	double yi1 = y1 + (xr - x1) * slope, yi2 = y1 + (xr + sx - x1) * slope;
	return (xi1 > xr && xi1 < xr + sx) || (xi2 > xr && xi2 < xr + sx) || (yi1 > yr && yi1 < yr + sy) || (yi2 > yr && yi2 < yr + sy);
}

}
