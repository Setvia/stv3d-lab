#pragma once

#ifndef CORE_MATH_CONVENTIONS_H
#define CORE_MATH_CONVENTIONS_H

// ---------------------------------------------------------------------------
// Math conventions used across `core`. Keep this file as the single reference:
// every generator, camera and render backend must agree with it, otherwise the
// bugs show up as mirrored/rotated geometry that is very hard to trace back.
//
// 1. Handedness        : right-handed world space. +X right, +Y up, +Z towards
//                        the viewer. A camera looks down its local -Z axis.
// 2. Angles            : radians everywhere (sin/cos/tan take radians). Only the
//                        UI layer converts from degrees.
// 3. Matrix storage    : column-major, like OpenGL/GLSL/Vulkan expect it.
//                        `mat4::m[col * 4 + row]`, `mat3::m[col * 3 + row]`.
//                        A matrix can therefore be uploaded to the GPU as-is.
// 4. Vector/matrix use : `v' = M * v` (matrix on the left). Composition is
//                        `M = P * V * Mmodel`, i.e. the right-most matrix is
//                        applied to the vertex first.
// 5. Transform order   : scale -> rotate -> translate. A model matrix built from
//                        position/rotation/scale must be `T * R * S`.
// 6. Quaternions       : `quat{w, x, y, z}` (scalar first), unit length, and
//                        `rotate(v) = q * v * q^-1`. Composition follows the
//                        matrix convention: `(a * b)` applies `b` first.
// 7. Positive rotation : counter-clockwise when looking from the positive end of
//                        the axis towards the origin (right-hand rule).
//
// ---------------------------------------------------------------------------
// Clip space: this is the one place where graphics APIs genuinely differ, so it
// is a parameter instead of a hard-coded convention.
//
//                              OpenGL            Vulkan / D3D
//   clip depth (z_ndc)         [-1, 1]           [0, 1]
//   NDC y axis                 +Y up             +Y down (viewport is flipped)
//
// The OpenGL/Vulkan backends therefore build their projection with different
// arguments and, for Vulkan, a Y flip. Never bake one API's convention into
// core data - that is why `ClipDepth` exists.

enum class ClipDepth
{
    NegativeOneToOne,  // OpenGL, OpenGL ES
    ZeroToOne          // Vulkan, D3D, Metal
};

#endif  // CORE_MATH_CONVENTIONS_H
