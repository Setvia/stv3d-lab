#pragma once

#ifndef CORE_MATH_QUAT_H
#define CORE_MATH_QUAT_H

#include <cmath>

#include "mat3.h"
#include "vec3.h"

// Unit quaternion used for every rotation in the engine (no Euler angles anywhere).
//
// Storage order is scalar-first: { w, x, y, z }.
//   * a unit quaternion rotates a vector as  v' = q * v * q^-1
//   * composition follows the matrix convention: (a * b) applies b first
//   * the identity (no rotation) is { 1, 0, 0, 0 }
//
// See core/math/conventions.h for the project-wide conventions.
struct quat
{
    float w, x, y, z;

    // ---- constructors -----------------------------------------------------
    constexpr quat() noexcept : w{1.f}, x{0.f}, y{0.f}, z{0.f} {}
    constexpr quat(float wv, float xv, float yv, float zv) noexcept : w{wv}, x{xv}, y{yv}, z{zv} {}

    // Rotation of `radians` around `axis` (axis is normalized internally)
    [[nodiscard]] static quat fromAxisAngle(const vec3& axis, float radians) noexcept
    {
        const vec3 n = axis.normalized();
        const float half = radians * 0.5f;
        const float s = std::sin(half);
        return quat{std::cos(half), n.x * s, n.y * s, n.z * s};
    }

    // Shortest rotation that maps `from` onto `to` (both normalized internally)
    [[nodiscard]] static quat fromTo(const vec3& from, const vec3& to) noexcept
    {
        const vec3 a = from.normalized();
        const vec3 b = to.normalized();
        const float d = a.dot(b);

        if (d >= 1.0f - 1e-6f)
        {
            return quat{};  // already aligned
        }
        if (d <= -1.0f + 1e-6f)
        {
            // Opposite directions: any perpendicular axis gives a valid 180 degree turn
            vec3 axis = a.cross(vec3{1.f, 0.f, 0.f});
            if (axis.length() < 1e-6f)
            {
                axis = a.cross(vec3{0.f, 0.f, 1.f});
            }
            return fromAxisAngle(axis, 3.14159265358979323846f);
        }

        const vec3 axis = a.cross(b);
        return quat{1.0f + d, axis.x, axis.y, axis.z}.normalized();
    }

    // ---- basic algebra ----------------------------------------------------
    [[nodiscard]] constexpr quat operator*(const quat& rhs) const noexcept
    {
        // Hamilton product; using it as (a * b) means "apply b, then a"
        return quat{
            w * rhs.w - x * rhs.x - y * rhs.y - z * rhs.z,
            w * rhs.x + x * rhs.w + y * rhs.z - z * rhs.y,
            w * rhs.y - x * rhs.z + y * rhs.w + z * rhs.x,
            w * rhs.z + x * rhs.y - y * rhs.x + z * rhs.w
        };
    }

    quat& operator*=(const quat& rhs) noexcept
    {
        *this = *this * rhs;
        return *this;
    }

    [[nodiscard]] constexpr quat operator*(float s) const noexcept
    {
        return quat{w * s, x * s, y * s, z * s};
    }

    [[nodiscard]] constexpr quat operator+(const quat& rhs) const noexcept
    {
        return quat{w + rhs.w, x + rhs.x, y + rhs.y, z + rhs.z};
    }

    [[nodiscard]] constexpr float dot(const quat& rhs) const noexcept
    {
        return w * rhs.w + x * rhs.x + y * rhs.y + z * rhs.z;
    }

    [[nodiscard]] float length() const noexcept
    {
        return std::sqrt(dot(*this));
    }

    // Normalized copy; returns identity for a degenerate (zero-length) input
    [[nodiscard]] quat normalized() const noexcept
    {
        const float len = length();
        if (len < 1e-6f)
        {
            return quat{};
        }
        const float inv = 1.0f / len;
        return quat{w * inv, x * inv, y * inv, z * inv};
    }

    // Inverse rotation. For a unit quaternion this equals the conjugate.
    [[nodiscard]] constexpr quat conjugate() const noexcept
    {
        return quat{w, -x, -y, -z};
    }

    [[nodiscard]] quat inverse() const noexcept
    {
        const float len_sq = dot(*this);
        if (len_sq < 1e-12f)
        {
            return quat{};
        }
        const float inv = 1.0f / len_sq;
        return quat{w * inv, -x * inv, -y * inv, -z * inv};
    }

    // ---- application ------------------------------------------------------
    // Rotate a direction/offset vector: v' = q * v * q^-1 (expanded, no temporaries)
    [[nodiscard]] vec3 rotate(const vec3& v) const noexcept
    {
        // t = 2 * (q_vec x v);  v' = v + w * t + q_vec x t
        const vec3 qv{x, y, z};
        const vec3 t = qv.cross(v) * 2.0f;
        return v + t * w + qv.cross(t);
    }

    // ---- conversions ------------------------------------------------------
    [[nodiscard]] mat3 toMat3() const noexcept
    {
        // Standard quaternion -> rotation matrix, written in column-major order
        const float xx = x * x, yy = y * y, zz = z * z;
        const float xy = x * y, xz = x * z, yz = y * z;
        const float wx = w * x, wy = w * y, wz = w * z;

        return mat3{
            // column 0
            1.f - 2.f * (yy + zz), 2.f * (xy + wz),        2.f * (xz - wy),
            // column 1
            2.f * (xy - wz),       1.f - 2.f * (xx + zz),  2.f * (yz + wx),
            // column 2
            2.f * (xz + wy),       2.f * (yz - wx),        1.f - 2.f * (xx + yy)
        };
    }

    // ---- interpolation ----------------------------------------------------
    // Quaternion represented by an orthonormal, column-major rotation matrix.
    // (Shoemake's method; reads through mat3::at() so the storage layout stays private.)
    [[nodiscard]] static quat fromMat3(const mat3& m) noexcept
    {
        const float m00 = m.at(0, 0);
        const float m11 = m.at(1, 1);
        const float m22 = m.at(2, 2);
        const float trace = m00 + m11 + m22;

        if (trace > 0.0f)
        {
            const float s = std::sqrt(trace + 1.0f) * 2.0f;  // s = 4w
            return quat{0.25f * s,
                        (m.at(2, 1) - m.at(1, 2)) / s,
                        (m.at(0, 2) - m.at(2, 0)) / s,
                        (m.at(1, 0) - m.at(0, 1)) / s};
        }
        if (m00 > m11 && m00 > m22)
        {
            const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;  // s = 4x
            return quat{(m.at(2, 1) - m.at(1, 2)) / s,
                        0.25f * s,
                        (m.at(0, 1) + m.at(1, 0)) / s,
                        (m.at(0, 2) + m.at(2, 0)) / s};
        }
        if (m11 > m22)
        {
            const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;  // s = 4y
            return quat{(m.at(0, 2) - m.at(2, 0)) / s,
                        (m.at(0, 1) + m.at(1, 0)) / s,
                        0.25f * s,
                        (m.at(1, 2) + m.at(2, 1)) / s};
        }
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;      // s = 4z
        return quat{(m.at(1, 0) - m.at(0, 1)) / s,
                    (m.at(0, 2) + m.at(2, 0)) / s,
                    (m.at(1, 2) + m.at(2, 1)) / s,
                    0.25f * s};
    }

    // Spherical linear interpolation; `t` is clamped to [0, 1]
    [[nodiscard]] static quat slerp(const quat& a, const quat& b, float t) noexcept
    {
        if (t <= 0.0f) return a.normalized();
        if (t >= 1.0f) return b.normalized();

        quat q0 = a.normalized();
        quat q1 = b.normalized();

        float cos_theta = q0.dot(q1);
        if (cos_theta < 0.0f)
        {
            // Take the short way around
            q1 = q1 * -1.0f;
            cos_theta = -cos_theta;
        }

        if (cos_theta > 0.9995f)
        {
            // Nearly identical: linear interpolation is numerically safer
            return (q0 + (q1 + q0 * -1.0f) * t).normalized();
        }

        const float theta = std::acos(cos_theta);
        const float sin_theta = std::sin(theta);
        const float wa = std::sin((1.0f - t) * theta) / sin_theta;
        const float wb = std::sin(t * theta) / sin_theta;
        return (q0 * wa + q1 * wb).normalized();
    }
};

#endif  // CORE_MATH_QUAT_H
