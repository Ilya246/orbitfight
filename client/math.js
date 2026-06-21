// Ported from include/math.hpp and src/math.cpp

export const PI = Math.atan(1.0) * 4.0;
export const TAU = Math.atan(1.0) * 8.0;
export const radToDeg = 180.0 / PI;
export const degToRad = PI / 180.0;
export const sqrt2 = Math.sqrt(2);

// physics
export const C = 3.0e8;
export const CC = C * C;

export function dst2(x, y) {
  return x * x + y * y;
}

export function dst(x, y) {
  return Math.sqrt(dst2(x, y));
}

export function rand_f(from, to) {
  return from + Math.random() * (to - from);
}

export function chance(number) {
  return Math.random() < number;
}

export function deltaAngle(a, b) {
  let diff = (b - a) % 360;
  return diff + 360 * ((diff < -180 ? 1 : 0) - (diff > 180 ? 1 : 0));
}

export function deltaAngleRad(a, b) {
  let diff = (b - a) % TAU;
  return diff + TAU * ((diff < -PI ? 1 : 0) - (diff > PI ? 1 : 0));
}

export function absMax(a, b) {
  return Math.abs(a) > Math.abs(b) ? a : b;
}

export function lerpRotation(a, b, c) {
  return a + c * deltaAngle(a, b);
}
