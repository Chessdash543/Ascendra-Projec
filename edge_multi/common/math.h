#pragma once
#include <cmath>
#include <cstdlib>

struct Vec2 {
    float x, y;

    Vec2() : x(0), y(0) {}
    Vec2(float x, float y) : x(x), y(y) {}

    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2 operator/(float s) const { return {x / s, y / s}; }
    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }

    float len() const { return std::sqrt(x * x + y * y); }
    Vec2 normalized() const { float l = len(); return l < 0.0001f ? Vec2{0,0} : Vec2{x/l, y/l}; }
};

inline float dist(const Vec2& a, const Vec2& b) { return (a - b).len(); }

inline float clamp(float v, float mn, float mx) { return v < mn ? mn : v > mx ? mx : v; }

inline float randf() { return (float)std::rand() / (float)RAND_MAX; }

constexpr float M_PI_F = 3.14159265358979323846f;
