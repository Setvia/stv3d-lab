#pragma once

#ifndef VEC_4_H
#define VEC_4_H

#include <cmath>

struct vec4
{
    float x, y, z, w;

    // Constructor
    constexpr vec4() noexcept : x{0.f}, y{0.f}, z{0.f}, w{0.f} {}
    constexpr vec4(float xv, float yv, float zv, float wv) noexcept : x{xv}, y{yv}, z{zv}, w{wv} {}

    // Addition
    constexpr vec4 operator+(const vec4& rhs) const noexcept
    {
        return vec4{x + rhs.x, y + rhs.y, z + rhs.z, w + rhs.w};
    }

    // Subtraction
    constexpr vec4 operator-(const vec4& rhs) const noexcept
    {
        return vec4{x - rhs.x, y - rhs.y, z - rhs.z, w - rhs.w};
    }

    // Scalar multiply
    constexpr vec4 operator*(float s) const noexcept
    {
        return vec4{x * s, y * s, z * s, w *s};
    }

    // Scalar divide
    constexpr vec4 operator/(float s) const noexcept
    {
        return vec4{x / s, y / s, z / s, w / s};
    }

    // In-place addition
    vec4& operator+=(const vec4& rhs) noexcept
    {
        x += rhs.x;
        y += rhs.y;
        z += rhs.z;
        w += rhs.w;
        return *this;
    }

    // In-place substraction
    vec4& operator-=(const vec4& rhs) noexcept
    {
        x -= rhs.x;
        y -= rhs.y;
        z -= rhs.z;
        w -= rhs.w;
        return *this;
    }

    // In-place scalar multiply
    vec4& operator*=(float s) noexcept
    {
        x *= s;
        y *= s;
        z *= s;
        w *= s;
        return *this;
    }

    // In-place scalar divide
    vec4& operator/=(float s) noexcept
    {
        x /= s;
        y /= s;
        z /= s;
        w /= s;
        return *this;
    }

    // Dot product
    [[nodiscard]] constexpr float dot(const vec4& rhs) const noexcept
    {
        return x * rhs.x + y * rhs.y + z * rhs.z + w * rhs.w;
    }

    // Squared length
    [[nodiscard]] constexpr float lengthSquared() const noexcept
    {
        return dot(*this);
    }

    // Get vector length
    [[nodiscard]] float length() const noexcept
    {
        return std::sqrt(lengthSquared());
    }

    // Return normalized vector
    [[nodiscard]] vec4 normalized() const noexcept
    {
        float len = length();
        if (len < 1e-6f) return vec4{};
        return *this / len;
    }
};

#endif // VEC_4_H