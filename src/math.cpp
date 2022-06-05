#include <random>
#include "globals.hpp"
#include "math.hpp"

using namespace obf;

std::random_device rand_d;
std::mt19937 rand_g(rand_d());

float rand_f(float from, float to){
    return std::uniform_real_distribution<>(from, to)(rand_g);
}
