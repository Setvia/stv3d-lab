#pragma once

#ifndef VEC_2_H
#define VEC_2_H

#include <cmath>

struct vec2
{
    float x, y;

    // Constructor
    constexpr vec2() noexcept : x{0.f}, y{0.f} {}
    constexpr vec2(float xv, float yv) noexcept : x{xv}, y{yv} {}

    // Addition
    constexpr vec2 operator+(const vec2& rhs) const noexcept
    {
        return vec2{x + rhs.x, y + rhs.y};
    }

    // Subtraction
    constexpr vec2 operator-(const vec2& rhs) const noexcept
    {
        return vec2{x - rhs.x, y - rhs.y};
    }

    // Scalar multiply
    constexpr vec2 operator*(float s) const noexcept
    {
        return vec2{x * s, y * s};
    }

    // Scalar divide
    constexpr vec2 operator/(float s) const noexcept
    {
        return vec2{x / s, y / s};
    }

    // In-place addition
    vec2& operator+=(const vec2& rhs) noexcept
    {
        x += rhs.x;
        y += rhs.y;
        return *this;
    }

    // In-place substraction
    vec2& operator-=(const vec2& rhs) noexcept
    {
        x -= rhs.x;
        y -= rhs.y;
        return *this;
    }

    // In-place scalar multiply
    vec2& operator*=(float s) noexcept
    {
        x *= s;
        y *= s;
        return *this;
    }

    // In-place scalar divide
    vec2& operator/=(float s) noexcept
    {
        x /= s;
        y /= s;
        return *this;
    }

    // Dot product
    [[nodiscard]] constexpr float dot(const vec2& rhs) const noexcept
    {
        return x * rhs.x + y * rhs.y;
    }

    // Get vector length
    [[nodiscard]] float length() const noexcept
    {
        return std::sqrt(dot(*this));
    }

    // Return normalized vector
    [[nodiscard]] vec2 normalized() const noexcept
    {
        float len = length();
        if (len < 1e-6f) return vec2{};
        return *this / len;
    }
};

#endif // VEC_2_H