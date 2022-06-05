#include "math.hpp"

#include <random>

namespace obf {

static std::random_device rand_d;
static std::mt19937 rand_g(rand_d());

float rand_f(float from, float to) {
	return std::uniform_real_distribution(from, to)(rand_g);
}

}
