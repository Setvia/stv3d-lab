#pragma once

#ifndef VEC_3_H
#define VEC_3_H

#include <cmath>

struct vec3
{
    float x, y, z;

    // Constructor
    constexpr vec3() noexcept : x{0.f}, y{0.f}, z{0.f} {}
    constexpr vec3(float xv, float yv, float zv) noexcept : x{xv}, y{yv}, z{zv} {}

    // Addition
    constexpr vec3 operator+(const vec3& rhs) const noexcept
    {
        return vec3{x + rhs.x, y + rhs.y, z + rhs.z};
    }

    // Subtraction
    constexpr vec3 operator-(const vec3& rhs) const noexcept
    {
        return vec3{x - rhs.x, y - rhs.y, z - rhs.z};
    }

    // Unary negation (mirror of the vector); needed for things like -forward()
    constexpr vec3 operator-() const noexcept
    {
        return vec3{-x, -y, -z};
    }

    // Scalar multiply
    constexpr vec3 operator*(float s) const noexcept
    {
        return vec3{x * s, y * s, z * s};
    }

    // Scalar divide
    constexpr vec3 operator/(float s) const noexcept
    {
        return vec3{x / s, y / s, z / s};
    }

    // In-place addition
    vec3& operator+=(const vec3& rhs) noexcept
    {
        x += rhs.x;
        y += rhs.y;
        z += rhs.z;
        return *this;
    }

    // In-place substraction
    vec3& operator-=(const vec3& rhs) noexcept
    {
        x -= rhs.x;
        y -= rhs.y;
        z -= rhs.z;
        return *this;
    }

    // In-place scalar multiply
    vec3& operator*=(float s) noexcept
    {
        x *= s;
        y *= s;
        z *= s;
        return *this;
    }

    // In-place scalar divide
    vec3& operator/=(float s) noexcept
    {
        x /= s;
        y /= s;
        z /= s;
        return *this;
    }

    // Dot product
    [[nodiscard]] constexpr float dot(const vec3& rhs) const noexcept
    {
        return x * rhs.x + y * rhs.y + z * rhs.z;
    }

    // Cross product, only valid for vec3
    [[nodiscard]] constexpr vec3 cross(const vec3& rhs) const noexcept
    {
        return vec3{
            y * rhs.z - z * rhs.y,
            z * rhs.x - x * rhs.z,
            x * rhs.y - y * rhs.x
        };
    }

    // Get vector length
    [[nodiscard]] float length() const noexcept
    {
        return std::sqrt(dot(*this));
    }

    // Return normalized vector
    [[nodiscard]] vec3 normalized() const noexcept
    {
        float len = length();
        if (len < 1e-6f) return vec3{};
        return *this / len;
    }
};

#endif // VEC_3_H