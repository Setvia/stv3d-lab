#pragma once

#ifndef CORE_MATH_MAT3_H
#define CORE_MATH_MAT3_H

#include <cmath>

#include "vec3.h"

// 3x3 matrix with column-major storage: element (row, col) lives at m[col * 3 + row].
// This matches what OpenGL/Vulkan/GLSL expect, so a mat3 can be uploaded as-is.
// See core/math/conventions.h for the project-wide rules.
struct mat3
{
    float m[9];

    // Default constructor: identity
    constexpr mat3() noexcept
        : m{
            1, 0, 0,
            0, 1, 0,
            0, 0, 1
        }
    {}

    // Construct from 9 floats given in COLUMN-major order:
    // (col0 row0, col0 row1, col0 row2, col1 row0, ...)
    constexpr mat3(
        float c0r0, float c0r1, float c0r2,
        float c1r0, float c1r1, float c1r2,
        float c2r0, float c2r1, float c2r2
    ) noexcept
        : m{
            c0r0, c0r1, c0r2,
            c1r0, c1r1, c1r2,
            c2r0, c2r1, c2r2
        }
    {}

    [[nodiscard]] static constexpr mat3 identity() noexcept { return mat3{}; }

    // ---- element access ---------------------------------------------------
    // Row/column access is provided so callers never have to remember the layout
    [[nodiscard]] constexpr float at(int row, int col) const noexcept { return m[col * 3 + row]; }
    constexpr void set(int row, int col, float value) noexcept { m[col * 3 + row] = value; }

    [[nodiscard]] constexpr vec3 column(int col) const noexcept
    {
        return vec3{m[col * 3 + 0], m[col * 3 + 1], m[col * 3 + 2]};
    }
    [[nodiscard]] constexpr vec3 row(int row) const noexcept
    {
        return vec3{m[0 * 3 + row], m[1 * 3 + row], m[2 * 3 + row]};
    }

    // ---- operators --------------------------------------------------------
    [[nodiscard]] constexpr vec3 operator*(const vec3& v) const noexcept
    {
        return vec3{
            m[0] * v.x + m[3] * v.y + m[6] * v.z,
            m[1] * v.x + m[4] * v.y + m[7] * v.z,
            m[2] * v.x + m[5] * v.y + m[8] * v.z
        };
    }

    [[nodiscard]] constexpr mat3 operator*(const mat3& rhs) const noexcept
    {
        mat3 out{};
        for (int col = 0; col < 3; ++col)
        {
            for (int row = 0; row < 3; ++row)
            {
                out.m[col * 3 + row]
                    = m[0 * 3 + row] * rhs.m[col * 3 + 0]
                    + m[1 * 3 + row] * rhs.m[col * 3 + 1]
                    + m[2 * 3 + row] * rhs.m[col * 3 + 2];
            }
        }
        return out;
    }

    mat3& operator*=(const mat3& rhs) noexcept
    {
        *this = *this * rhs;
        return *this;
    }

    [[nodiscard]] constexpr mat3 transpose() const noexcept
    {
        return mat3{
            m[0], m[3], m[6],
            m[1], m[4], m[7],
            m[2], m[5], m[8]
        };
    }

    [[nodiscard]] constexpr float determinant() const noexcept
    {
        return m[0] * (m[4] * m[8] - m[7] * m[5])
             - m[3] * (m[1] * m[8] - m[7] * m[2])
             + m[6] * (m[1] * m[5] - m[4] * m[2]);
    }

    // Inverse through the adjugate; returns identity for a singular matrix
    [[nodiscard]] mat3 inverse() const noexcept
    {
        const float det = determinant();
        if (std::fabs(det) < 1e-12f)
        {
            return mat3{};
        }
        const float inv_det = 1.0f / det;

        mat3 out;
        out.m[0] = (m[4] * m[8] - m[7] * m[5]) * inv_det;
        out.m[1] = (m[7] * m[2] - m[1] * m[8]) * inv_det;
        out.m[2] = (m[1] * m[5] - m[4] * m[2]) * inv_det;
        out.m[3] = (m[6] * m[5] - m[3] * m[8]) * inv_det;
        out.m[4] = (m[0] * m[8] - m[6] * m[2]) * inv_det;
        out.m[5] = (m[3] * m[2] - m[0] * m[5]) * inv_det;
        out.m[6] = (m[3] * m[7] - m[6] * m[4]) * inv_det;
        out.m[7] = (m[6] * m[1] - m[0] * m[7]) * inv_det;
        out.m[8] = (m[0] * m[4] - m[3] * m[1]) * inv_det;
        return out;
    }

    // ---- generators -------------------------------------------------------
    [[nodiscard]] static constexpr mat3 fromColumns(const vec3& c0, const vec3& c1, const vec3& c2) noexcept
    {
        return mat3{
            c0.x, c0.y, c0.z,
            c1.x, c1.y, c1.z,
            c2.x, c2.y, c2.z
        };
    }

    [[nodiscard]] static constexpr mat3 scale(const vec3& s) noexcept
    {
        return mat3{
            s.x, 0,   0,
            0,   s.y, 0,
            0,   0,   s.z
        };
    }

    // Rotations are right-handed: positive angles turn counter-clockwise when
    // looking from the positive end of the axis towards the origin.
    [[nodiscard]] static mat3 rotateX(float radians) noexcept
    {
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        return mat3{
            1, 0,  0,
            0, c,  s,
            0, -s, c
        };
    }

    [[nodiscard]] static mat3 rotateY(float radians) noexcept
    {
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        return mat3{
            c, 0, -s,
            0, 1, 0,
            s, 0, c
        };
    }

    [[nodiscard]] static mat3 rotateZ(float radians) noexcept
    {
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        return mat3{
            c,  s, 0,
            -s, c, 0,
            0,  0, 1
        };
    }
};

#endif  // CORE_MATH_MAT3_H
