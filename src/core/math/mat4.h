#pragma once

#ifndef CORE_MATH_MAT4_H
#define CORE_MATH_MAT4_H

#include <cmath>

#include "conventions.h"
#include "mat3.h"
#include "quat.h"
#include "vec3.h"
#include "vec4.h"

// 4x4 matrix with column-major storage: element (row, col) lives at m[col * 4 + row].
// This is what OpenGL/GLSL and Vulkan expect, so the raw float array can be uploaded
// to the GPU without transposing.
//
// Conventions (see core/math/conventions.h):
//   * `v' = M * v`, composition `M = P * V * Mmodel` (right-most applies first)
//   * model matrix from position/rotation/scale is `T * R * S`
//   * angles are radians, rotations are right-handed
//   * clip depth range / Y direction is a parameter, not a hard-coded assumption
struct mat4
{
    float m[16];

    // Default constructor: identity
    constexpr mat4() noexcept
        : m{
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1
        }
    {}

    // Construct from 16 floats given in COLUMN-major order:
    // (col0 row0, col0 row1, col0 row2, col0 row3, col1 row0, ...)
    constexpr mat4(
        float c0r0, float c0r1, float c0r2, float c0r3,
        float c1r0, float c1r1, float c1r2, float c1r3,
        float c2r0, float c2r1, float c2r2, float c2r3,
        float c3r0, float c3r1, float c3r2, float c3r3
    ) noexcept
        : m{
            c0r0, c0r1, c0r2, c0r3,
            c1r0, c1r1, c1r2, c1r3,
            c2r0, c2r1, c2r2, c2r3,
            c3r0, c3r1, c3r2, c3r3
        }
    {}

    [[nodiscard]] static constexpr mat4 identity() noexcept { return mat4{}; }

    // ---- element access ---------------------------------------------------
    [[nodiscard]] constexpr float at(int row, int col) const noexcept { return m[col * 4 + row]; }
    constexpr void set(int row, int col, float value) noexcept { m[col * 4 + row] = value; }

    [[nodiscard]] constexpr vec4 column(int col) const noexcept
    {
        return vec4{m[col * 4 + 0], m[col * 4 + 1], m[col * 4 + 2], m[col * 4 + 3]};
    }

    // Translation is stored in the last column, which is also what `at(0..2, 3)` returns
    [[nodiscard]] constexpr vec3 translation() const noexcept
    {
        return vec3{m[12], m[13], m[14]};
    }

    // ---- operators --------------------------------------------------------
    [[nodiscard]] constexpr vec4 operator*(const vec4& v) const noexcept
    {
        return vec4{
            m[0x0] * v.x + m[0x4] * v.y + m[0x8]  * v.z + m[0xC] * v.w,
            m[0x1] * v.x + m[0x5] * v.y + m[0x9]  * v.z + m[0xD] * v.w,
            m[0x2] * v.x + m[0x6] * v.y + m[0xA]  * v.z + m[0xE] * v.w,
            m[0x3] * v.x + m[0x7] * v.y + m[0xB]  * v.z + m[0xF] * v.w
        };
    }

    [[nodiscard]] constexpr mat4 operator*(const mat4& rhs) const noexcept
    {
        mat4 out{};
        for (int col = 0; col < 4; ++col)
        {
            for (int row = 0; row < 4; ++row)
            {
                out.m[col * 4 + row]
                    = m[0 * 4 + row] * rhs.m[col * 4 + 0]
                    + m[1 * 4 + row] * rhs.m[col * 4 + 1]
                    + m[2 * 4 + row] * rhs.m[col * 4 + 2]
                    + m[3 * 4 + row] * rhs.m[col * 4 + 3];
            }
        }
        return out;
    }

    mat4& operator*=(const mat4& rhs) noexcept
    {
        *this = *this * rhs;
        return *this;
    }

    [[nodiscard]] constexpr mat4 transpose() const noexcept
    {
        return mat4{
            m[0x0], m[0x4], m[0x8],  m[0xC],
            m[0x1], m[0x5], m[0x9],  m[0xD],
            m[0x2], m[0x6], m[0xA],  m[0xE],
            m[0x3], m[0x7], m[0xB],  m[0xF]
        };
    }

    // ---- vector transforms ------------------------------------------------
    // Full transform including translation, with perspective divide
    [[nodiscard]] vec3 transformPoint(const vec3& p) const noexcept
    {
        const vec4 r = (*this) * vec4{p.x, p.y, p.z, 1.0f};
        if (std::fabs(r.w) > 1e-6f && std::fabs(r.w - 1.0f) > 1e-6f)
        {
            return vec3{r.x / r.w, r.y / r.w, r.z / r.w};
        }
        return vec3{r.x, r.y, r.z};
    }

    // Direction transform: translation is ignored (w = 0)
    [[nodiscard]] constexpr vec3 transformDirection(const vec3& d) const noexcept
    {
        const vec4 r = (*this) * vec4{d.x, d.y, d.z, 0.0f};
        return vec3{r.x, r.y, r.z};
    }

    // ---- determinant ------------------------------------------------------
    [[nodiscard]] constexpr float determinant() const noexcept
    {
        const float a00 = at(0, 0), a01 = at(0, 1), a02 = at(0, 2), a03 = at(0, 3);
        const float a10 = at(1, 0), a11 = at(1, 1), a12 = at(1, 2), a13 = at(1, 3);
        const float a20 = at(2, 0), a21 = at(2, 1), a22 = at(2, 2), a23 = at(2, 3);
        const float a30 = at(3, 0), a31 = at(3, 1), a32 = at(3, 2), a33 = at(3, 3);

        // 2x2 minors
        const float b00 = a00 * a11 - a01 * a10;
        const float b01 = a00 * a12 - a02 * a10;
        const float b02 = a00 * a13 - a03 * a10;
        const float b03 = a01 * a12 - a02 * a11;
        const float b04 = a01 * a13 - a03 * a11;
        const float b05 = a02 * a13 - a03 * a12;
        const float b06 = a20 * a31 - a21 * a30;
        const float b07 = a20 * a32 - a22 * a30;
        const float b08 = a20 * a33 - a23 * a30;
        const float b09 = a21 * a32 - a22 * a31;
        const float b10 = a21 * a33 - a23 * a31;
        const float b11 = a22 * a33 - a23 * a32;

        return b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
    }

    // ---- inverse ----------------------------------------------------------
    // General 4x4 inverse: adjugate divided by the determinant.
    // A near-singular matrix returns identity instead of NaNs.
    [[nodiscard]] mat4 inverse() const noexcept
    {
        // Work on a row-major copy so the classic cofactor formulas can be used verbatim
        float a[4][4];
        for (int row = 0; row < 4; ++row)
        {
            for (int col = 0; col < 4; ++col)
            {
                a[row][col] = at(row, col);
            }
        }

        // Adjugate (transposed cofactors), row-major
        float inv[4][4];
        inv[0][0] =  a[1][1] * a[2][2] * a[3][3] - a[1][1] * a[2][3] * a[3][2]
                   - a[1][2] * a[2][1] * a[3][3] + a[1][2] * a[2][3] * a[3][1]
                   + a[1][3] * a[2][1] * a[3][2] - a[1][3] * a[2][2] * a[3][1];
        inv[0][1] = -a[0][1] * a[2][2] * a[3][3] + a[0][1] * a[2][3] * a[3][2]
                   + a[0][2] * a[2][1] * a[3][3] - a[0][2] * a[2][3] * a[3][1]
                   - a[0][3] * a[2][1] * a[3][2] + a[0][3] * a[2][2] * a[3][1];
        inv[0][2] =  a[0][1] * a[1][2] * a[3][3] - a[0][1] * a[1][3] * a[3][2]
                   - a[0][2] * a[1][1] * a[3][3] + a[0][2] * a[1][3] * a[3][1]
                   + a[0][3] * a[1][1] * a[3][2] - a[0][3] * a[1][2] * a[3][1];
        inv[0][3] = -a[0][1] * a[1][2] * a[2][3] + a[0][1] * a[1][3] * a[2][2]
                   + a[0][2] * a[1][1] * a[2][3] - a[0][2] * a[1][3] * a[2][1]
                   - a[0][3] * a[1][1] * a[2][2] + a[0][3] * a[1][2] * a[2][1];

        inv[1][0] = -a[1][0] * a[2][2] * a[3][3] + a[1][0] * a[2][3] * a[3][2]
                   + a[1][2] * a[2][0] * a[3][3] - a[1][2] * a[2][3] * a[3][0]
                   - a[1][3] * a[2][0] * a[3][2] + a[1][3] * a[2][2] * a[3][0];
        inv[1][1] =  a[0][0] * a[2][2] * a[3][3] - a[0][0] * a[2][3] * a[3][2]
                   - a[0][2] * a[2][0] * a[3][3] + a[0][2] * a[2][3] * a[3][0]
                   + a[0][3] * a[2][0] * a[3][2] - a[0][3] * a[2][2] * a[3][0];
        inv[1][2] = -a[0][0] * a[1][2] * a[3][3] + a[0][0] * a[1][3] * a[3][2]
                   + a[0][2] * a[1][0] * a[3][3] - a[0][2] * a[1][3] * a[3][0]
                   - a[0][3] * a[1][0] * a[3][2] + a[0][3] * a[1][2] * a[3][0];
        inv[1][3] =  a[0][0] * a[1][2] * a[2][3] - a[0][0] * a[1][3] * a[2][2]
                   - a[0][2] * a[1][0] * a[2][3] + a[0][2] * a[1][3] * a[2][0]
                   + a[0][3] * a[1][0] * a[2][2] - a[0][3] * a[1][2] * a[2][0];

        inv[2][0] =  a[1][0] * a[2][1] * a[3][3] - a[1][0] * a[2][3] * a[3][1]
                   - a[1][1] * a[2][0] * a[3][3] + a[1][1] * a[2][3] * a[3][0]
                   + a[1][3] * a[2][0] * a[3][1] - a[1][3] * a[2][1] * a[3][0];
        inv[2][1] = -a[0][0] * a[2][1] * a[3][3] + a[0][0] * a[2][3] * a[3][1]
                   + a[0][1] * a[2][0] * a[3][3] - a[0][1] * a[2][3] * a[3][0]
                   - a[0][3] * a[2][0] * a[3][1] + a[0][3] * a[2][1] * a[3][0];
        inv[2][2] =  a[0][0] * a[1][1] * a[3][3] - a[0][0] * a[1][3] * a[3][1]
                   - a[0][1] * a[1][0] * a[3][3] + a[0][1] * a[1][3] * a[3][0]
                   + a[0][3] * a[1][0] * a[3][1] - a[0][3] * a[1][1] * a[3][0];
        inv[2][3] = -a[0][0] * a[1][1] * a[2][3] + a[0][0] * a[1][3] * a[2][1]
                   + a[0][1] * a[1][0] * a[2][3] - a[0][1] * a[1][3] * a[2][0]
                   - a[0][3] * a[1][0] * a[2][1] + a[0][3] * a[1][1] * a[2][0];

        inv[3][0] = -a[1][0] * a[2][1] * a[3][2] + a[1][0] * a[2][2] * a[3][1]
                   + a[1][1] * a[2][0] * a[3][2] - a[1][1] * a[2][2] * a[3][0]
                   - a[1][2] * a[2][0] * a[3][1] + a[1][2] * a[2][1] * a[3][0];
        inv[3][1] =  a[0][0] * a[2][1] * a[3][2] - a[0][0] * a[2][2] * a[3][1]
                   - a[0][1] * a[2][0] * a[3][2] + a[0][1] * a[2][2] * a[3][0]
                   + a[0][2] * a[2][0] * a[3][1] - a[0][2] * a[2][1] * a[3][0];
        inv[3][2] = -a[0][0] * a[1][1] * a[3][2] + a[0][0] * a[1][2] * a[3][1]
                   + a[0][1] * a[1][0] * a[3][2] - a[0][1] * a[1][2] * a[3][0]
                   - a[0][2] * a[1][0] * a[3][1] + a[0][2] * a[1][1] * a[3][0];
        inv[3][3] =  a[0][0] * a[1][1] * a[2][2] - a[0][0] * a[1][2] * a[2][1]
                   - a[0][1] * a[1][0] * a[2][2] + a[0][1] * a[1][2] * a[2][0]
                   + a[0][2] * a[1][0] * a[2][1] - a[0][2] * a[1][1] * a[2][0];

        // The adjugate must be divided by the determinant. Note that the determinant is
        // the first-row expansion  sum_j a[0][j] * C[0][j] = sum_j a[0][j] * adj[j][0],
        // i.e. the adjugate's first COLUMN (a naive sum over the first row silently
        // produces a wrong scale factor for non-symmetric matrices).
        const float det = determinant();
        if (std::fabs(det) < 1e-12f)
        {
            return mat4{};
        }
        const float inv_det = 1.0f / det;

        mat4 out;
        for (int row = 0; row < 4; ++row)
        {
            for (int col = 0; col < 4; ++col)
            {
                out.set(row, col, inv[row][col] * inv_det);
            }
        }
        return out;
    }

    // ---- generators -------------------------------------------------------
    [[nodiscard]] static constexpr mat4 translate(const vec3& t) noexcept
    {
        return mat4{
            1,   0,   0,   0,
            0,   1,   0,   0,
            0,   0,   1,   0,
            t.x, t.y, t.z, 1
        };
    }

    [[nodiscard]] static constexpr mat4 scale(const vec3& s) noexcept
    {
        return mat4{
            s.x, 0,   0,   0,
            0,   s.y, 0,   0,
            0,   0,   s.z, 0,
            0,   0,   0,   1
        };
    }

    // Rotation of a 3x3 rotation matrix (upper-left block, no translation)
    [[nodiscard]] static constexpr mat4 fromMat3(const mat3& r) noexcept
    {
        return mat4{
            r.m[0], r.m[1], r.m[2], 0,
            r.m[3], r.m[4], r.m[5], 0,
            r.m[6], r.m[7], r.m[8], 0,
            0,      0,      0,      1
        };
    }

    [[nodiscard]] static mat4 fromQuat(const quat& q) noexcept
    {
        return fromMat3(q.toMat3());
    }

    // Model matrix: scale first, then rotate, then translate (T * R * S)
    [[nodiscard]] static mat4 fromTRS(const vec3& position, const quat& rotation, const vec3& scale_factor) noexcept
    {
        return translate(position) * fromQuat(rotation) * scale(scale_factor);
    }

    // Rotations are right-handed (positive angles turn counter-clockwise when looking
    // from the positive end of the axis towards the origin)
    [[nodiscard]] static mat4 rotateX(float radians) noexcept
    {
        return fromMat3(mat3::rotateX(radians));
    }

    [[nodiscard]] static mat4 rotateY(float radians) noexcept
    {
        return fromMat3(mat3::rotateY(radians));
    }

    [[nodiscard]] static mat4 rotateZ(float radians) noexcept
    {
        return fromMat3(mat3::rotateZ(radians));
    }

    // View matrix for a right-handed camera that looks down its local -Z axis.
    // Handles the degenerate case where the view direction is parallel to `up`.
    [[nodiscard]] static mat4 lookAt(const vec3& eye, const vec3& target, const vec3& up) noexcept
    {
        vec3 z_axis = (eye - target).normalized();
        if (z_axis.length() < 1e-6f)
        {
            z_axis = vec3{0.0f, 0.0f, 1.0f};  // eye == target: keep a valid basis
        }

        vec3 up_ref = up.normalized();
        if (std::fabs(up_ref.dot(z_axis)) > 0.999f)
        {
            // View direction is (nearly) parallel to `up`: choose another reference axis
            up_ref = (std::fabs(z_axis.y) > 0.9f) ? vec3{0.0f, 0.0f, 1.0f} : vec3{0.0f, 1.0f, 0.0f};
        }

        const vec3 x_axis = up_ref.cross(z_axis).normalized();
        const vec3 y_axis = z_axis.cross(x_axis);

        mat4 view;
        // The camera axes become the ROWS of the rotation part
        view.m[0] = x_axis.x; view.m[4] = x_axis.y; view.m[8]  = x_axis.z;
        view.m[1] = y_axis.x; view.m[5] = y_axis.y; view.m[9]  = y_axis.z;
        view.m[2] = z_axis.x; view.m[6] = z_axis.y; view.m[10] = z_axis.z;

        // Then move the eye to the origin
        view.m[12] = -x_axis.dot(eye);
        view.m[13] = -y_axis.dot(eye);
        view.m[14] = -z_axis.dot(eye);
        return view;
    }

    // Perspective projection for a right-handed camera.
    // `clip` selects the clip-space depth range (OpenGL vs Vulkan/D3D); `flip_y` negates
    // the Y axis, which Vulkan needs because its NDC Y points down.
    [[nodiscard]] static mat4 perspective(float fovY, float aspect, float near_plane, float far_plane,
                                          ClipDepth clip = ClipDepth::NegativeOneToOne,
                                          bool flip_y = false) noexcept
    {
        const float tan_half = std::tan(fovY * 0.5f);

        mat4 out;
        for (float& value : out.m)
        {
            value = 0.0f;
        }

        out.m[0]  = 1.0f / (aspect * tan_half);  // (row 0, col 0)
        out.m[5]  = 1.0f / tan_half;             // (row 1, col 1)
        out.m[11] = -1.0f;                       // (row 3, col 2): w = -z

        if (clip == ClipDepth::NegativeOneToOne)
        {
            out.m[10] = -(far_plane + near_plane) / (far_plane - near_plane);
            out.m[14] = -(2.0f * far_plane * near_plane) / (far_plane - near_plane);
        }
        else
        {
            out.m[10] = far_plane / (near_plane - far_plane);
            out.m[14] = -(far_plane * near_plane) / (far_plane - near_plane);
        }

        if (flip_y)
        {
            out.m[5] = -out.m[5];
        }
        return out;
    }

    [[nodiscard]] static mat4 ortho(float left, float right, float bottom, float top,
                                    float near_plane, float far_plane,
                                    ClipDepth clip = ClipDepth::NegativeOneToOne,
                                    bool flip_y = false) noexcept
    {
        mat4 out;
        for (float& value : out.m)
        {
            value = 0.0f;
        }

        out.m[0]  = 2.0f / (right - left);                // (0, 0)
        out.m[5]  = 2.0f / (top - bottom);                // (1, 1)
        out.m[12] = -(right + left) / (right - left);     // (0, 3)
        out.m[13] = -(top + bottom) / (top - bottom);     // (1, 3)
        out.m[15] = 1.0f;                                 // (3, 3)

        if (clip == ClipDepth::NegativeOneToOne)
        {
            out.m[10] = -2.0f / (far_plane - near_plane);
            out.m[14] = -(far_plane + near_plane) / (far_plane - near_plane);
        }
        else
        {
            out.m[10] = -1.0f / (far_plane - near_plane);
            out.m[14] = -near_plane / (far_plane - near_plane);
        }

        if (flip_y)
        {
            out.m[5] = -out.m[5];
        }
        return out;
    }
};

#endif  // CORE_MATH_MAT4_H
