// Unit tests for the Qt-free, GPU-free core layer.
//
// These tests link nothing but stv3d_core: no Qt, no OpenGL, no window. If they ever
// stop building, something leaked a Qt/GL dependency into core.
//
// Every expectation is written with explicit numbers (hand-derived from the
// conventions in core/math/conventions.h) instead of comparing against a
// third-party library, so the tests stay meaningful after Qt is gone.

#include "core/math/conventions.h"
#include "core/math/mat3.h"
#include "core/math/mat4.h"
#include "core/math/quat.h"
#include "core/math/vec3.h"
#include "core/math/vec4.h"

#include <cmath>
#include <cstdio>

namespace
{
int g_failures = 0;

void check(bool condition, const char* name)
{
    std::printf("%s %s\n", condition ? "[PASS]" : "[FAIL]", name);
    if (!condition)
    {
        ++g_failures;
    }
}

bool near(float a, float b, float eps = 1e-4f)
{
    return std::fabs(a - b) < eps;
}

bool near3(const vec3& a, const vec3& b, float eps = 1e-4f)
{
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps);
}

bool near4(const vec4& a, const vec4& b, float eps = 1e-4f)
{
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps) && near(a.w, b.w, eps);
}

bool nearMat4(const mat4& a, const mat4& b, float eps = 1e-4f)
{
    for (int i = 0; i < 16; ++i)
    {
        if (!near(a.m[i], b.m[i], eps))
        {
            return false;
        }
    }
    return true;
}

constexpr float kPi = 3.14159265358979323846f;
constexpr float kHalfPi = kPi * 0.5f;
}  // namespace

int main()
{
    // ---------------------------------------------------------------- layout
    {
        const mat4 identity;
        check(near(identity.at(0, 0), 1.0f) && near(identity.at(1, 1), 1.0f)
                  && near(identity.at(2, 2), 1.0f) && near(identity.at(3, 3), 1.0f),
              "identity matrix has a unit diagonal");

        // Column-major storage: the translation lives in the LAST column -> m[12..14]
        const mat4 t = mat4::translate(vec3{1.0f, 2.0f, 3.0f});
        check(near(t.m[12], 1.0f) && near(t.m[13], 2.0f) && near(t.m[14], 3.0f)
                  && near(t.m[15], 1.0f),
              "column-major storage: translation occupies m[12..14]");
        check(near3(t.translation(), vec3{1.0f, 2.0f, 3.0f}), "translation() reads the last column");
        check(near4(t.column(0), vec4{1.0f, 0.0f, 0.0f, 0.0f}), "column(0) of a translation is +X");

        const mat4 m = mat4::translate(vec3{5.0f, 0.0f, 0.0f}) * mat4::rotateY(kHalfPi);
        check(nearMat4(m.transpose().transpose(), m), "transpose twice is the original matrix");
    }

    // ---------------------------------------------------------- multiplication
    {
        const mat4 a = mat4::translate(vec3{1.0f, 0.0f, 0.0f});
        const mat4 b = mat4::scale(vec3{2.0f, 2.0f, 2.0f});

        // Translation is applied AFTER scale, so (1,0,0) -> (2,0,0) -> (3,0,0)
        check(near3((a * b).transformPoint(vec3{1.0f, 0.0f, 0.0f}), vec3{3.0f, 0.0f, 0.0f}),
              "T * S applies scale first, then translation");
        // Reversed order scales the translation itself: (1,0,0) -> (2,0,0) -> (4,0,0)
        check(near3((b * a).transformPoint(vec3{1.0f, 0.0f, 0.0f}), vec3{4.0f, 0.0f, 0.0f}),
              "S * T scales the translation too (order matters)");

        const mat4 r = mat4::rotateY(kHalfPi);
        check(near3(r.transformDirection(vec3{1.0f, 0.0f, 0.0f}), vec3{0.0f, 0.0f, -1.0f}),
              "rotateY(+90 deg) maps +X to -Z (right-handed)");
        check(near3(r.transformDirection(vec3{0.0f, 0.0f, -1.0f}), vec3{-1.0f, 0.0f, 0.0f}),
              "rotateY(+90 deg) maps -Z to -X");
        check(near3(r.transformDirection(vec3{0.0f, 1.0f, 0.0f}), vec3{0.0f, 1.0f, 0.0f}),
              "rotateY leaves the Y axis untouched");

        const mat4 full = mat4::translate(vec3{10.0f, 0.0f, 0.0f});
        check(near3(full.transformDirection(vec3{1.0f, 0.0f, 0.0f}), vec3{1.0f, 0.0f, 0.0f}),
              "transformDirection ignores translation");
    }

    // ------------------------------------------------------------------- TRS
    {
        const vec3 position{1.0f, 0.0f, 0.0f};
        const quat rotation = quat::fromAxisAngle(vec3{0.0f, 1.0f, 0.0f}, kHalfPi);
        const vec3 scale{2.0f, 1.0f, 1.0f};

        const mat4 model = mat4::fromTRS(position, rotation, scale);
        // (1,0,0) --scale--> (2,0,0) --rotate 90 deg about Y--> (0,0,-2) --translate--> (1,0,-2)
        check(near3(model.transformPoint(vec3{1.0f, 0.0f, 0.0f}), vec3{1.0f, 0.0f, -2.0f}),
              "fromTRS applies scale, then rotation, then translation");

        const mat4 manual = mat4::translate(position) * mat4::fromQuat(rotation) * mat4::scale(scale);
        check(nearMat4(model, manual), "fromTRS equals T * R * S");

        const vec3 origin = model.transformPoint(vec3{0.0f, 0.0f, 0.0f});
        check(near3(origin, position), "the local origin maps onto the model position");
    }

    // ----------------------------------------------------------------- lookAt
    {
        const vec3 eye{0.0f, 0.0f, 3.0f};
        const vec3 target{0.0f, 0.0f, 0.0f};
        const mat4 view = mat4::lookAt(eye, target, vec3{0.0f, 1.0f, 0.0f});

        check(near3(view.transformPoint(eye), vec3{0.0f, 0.0f, 0.0f}),
              "lookAt maps the eye position to the camera-space origin");
        check(near3(view.transformPoint(target), vec3{0.0f, 0.0f, -3.0f}),
              "lookAt maps the target onto the -Z axis at the eye distance");
        check(near3(view.transformPoint(vec3{1.0f, 0.0f, 3.0f}), vec3{1.0f, 0.0f, 0.0f}),
              "lookAt keeps +X to the right (world (1,0,3) -> camera (1,0,0))");
        check(near3(view.transformDirection(vec3{0.0f, 1.0f, 0.0f}), vec3{0.0f, 1.0f, 0.0f}),
              "lookAt keeps +Y up for a horizontal camera");

        // Degenerate case: the view direction is parallel to `up`
        const mat4 straight_down = mat4::lookAt(vec3{0.0f, 5.0f, 0.0f}, vec3{0.0f, 0.0f, 0.0f},
                                                vec3{0.0f, 1.0f, 0.0f});
        const vec3 down_origin = straight_down.transformPoint(vec3{0.0f, 5.0f, 0.0f});
        bool finite = std::isfinite(down_origin.x) && std::isfinite(down_origin.y)
                      && std::isfinite(down_origin.z);
        for (float value : straight_down.m)
        {
            finite = finite && std::isfinite(value);
        }
        check(finite, "lookAt stays finite when the view direction is parallel to up");
    }

    // ----------------------------------------------------------- perspective
    {
        const float near_plane = 0.1f;
        const float far_plane = 100.0f;
        const float aspect = 2.0f;
        const float fov_y = kHalfPi;  // 90 degrees

        // OpenGL: depth maps to [-1, 1]
        const mat4 gl = mat4::perspective(fov_y, aspect, near_plane, far_plane);
        check(near(gl.m[0], 1.0f / (aspect * std::tan(fov_y * 0.5f))), "GL projection: (0,0) uses aspect");
        check(near(gl.m[5], 1.0f / std::tan(fov_y * 0.5f)), "GL projection: (1,1) from the vertical fov");
        check(near(gl.m[11], -1.0f), "GL projection: w = -z");

        const vec4 near_clip = gl * vec4{0.0f, 0.0f, -near_plane, 1.0f};
        const vec4 far_clip = gl * vec4{0.0f, 0.0f, -far_plane, 1.0f};
        check(near(near_clip.z / near_clip.w, -1.0f), "GL projection: near plane maps to depth -1");
        check(near(far_clip.z / far_clip.w, 1.0f), "GL projection: far plane maps to depth +1");

        // Vulkan: depth maps to [0, 1] and NDC Y points down (flip_y)
        const mat4 vk = mat4::perspective(fov_y, aspect, near_plane, far_plane,
                                          ClipDepth::ZeroToOne, true);
        const vec4 vk_near = vk * vec4{0.0f, 0.0f, -near_plane, 1.0f};
        const vec4 vk_far = vk * vec4{0.0f, 0.0f, -far_plane, 1.0f};
        check(near(vk_near.z / vk_near.w, 0.0f), "Vulkan projection: near plane maps to depth 0");
        check(near(vk_far.z / vk_far.w, 1.0f), "Vulkan projection: far plane maps to depth 1");
        check(vk.m[5] < 0.0f && gl.m[5] > 0.0f, "flip_y negates the Y scale only");
        check(near(vk.m[0], gl.m[0]), "flip_y leaves the X scale alone");

        // A point above the centre ends up below the centre once Y is flipped
        const vec4 gl_up = gl * vec4{0.0f, 1.0f, -5.0f, 1.0f};
        const vec4 vk_up = vk * vec4{0.0f, 1.0f, -5.0f, 1.0f};
        check(gl_up.y / gl_up.w > 0.0f && vk_up.y / vk_up.w < 0.0f,
              "the same world point has opposite NDC Y in GL and Vulkan");
    }

    // ----------------------------------------------------------------- ortho
    {
        const float near_plane = 1.0f;
        const float far_plane = 11.0f;

        const mat4 gl = mat4::ortho(-2.0f, 2.0f, -1.0f, 1.0f, near_plane, far_plane);
        check(near3(gl.transformPoint(vec3{-2.0f, -1.0f, -near_plane}), vec3{-1.0f, -1.0f, -1.0f}),
              "GL ortho maps the near/bottom/left corner to (-1,-1,-1)");
        check(near3(gl.transformPoint(vec3{2.0f, 1.0f, -far_plane}), vec3{1.0f, 1.0f, 1.0f}),
              "GL ortho maps the far/top/right corner to (1,1,1)");

        const mat4 vk = mat4::ortho(-2.0f, 2.0f, -1.0f, 1.0f, near_plane, far_plane,
                                    ClipDepth::ZeroToOne);
        check(near3(vk.transformPoint(vec3{0.0f, 0.0f, -near_plane}), vec3{0.0f, 0.0f, 0.0f}),
              "Vulkan ortho maps the near plane to depth 0");
        check(near3(vk.transformPoint(vec3{0.0f, 0.0f, -far_plane}), vec3{0.0f, 0.0f, 1.0f}),
              "Vulkan ortho maps the far plane to depth 1");
    }

    // ------------------------------------------------------------- quaternion
    {
        const quat identity;
        check(near(identity.w, 1.0f) && near(identity.x, 0.0f), "default quaternion is the identity");
        check(near3(identity.rotate(vec3{1.0f, 2.0f, 3.0f}), vec3{1.0f, 2.0f, 3.0f}),
              "identity rotation leaves a vector unchanged");

        const quat yaw = quat::fromAxisAngle(vec3{0.0f, 1.0f, 0.0f}, kHalfPi);
        check(near3(yaw.rotate(vec3{1.0f, 0.0f, 0.0f}), vec3{0.0f, 0.0f, -1.0f}),
              "quaternion: +90 deg about Y maps +X to -Z");
        check(near3(yaw.rotate(vec3{0.0f, 1.0f, 0.0f}), vec3{0.0f, 1.0f, 0.0f}),
              "quaternion: rotation about Y leaves Y alone");
        check(near(yaw.length(), 1.0f), "fromAxisAngle returns a unit quaternion");

        const quat pitch = quat::fromAxisAngle(vec3{1.0f, 0.0f, 0.0f}, kHalfPi);
        check(near3(pitch.rotate(vec3{0.0f, 0.0f, -1.0f}), vec3{0.0f, 1.0f, 0.0f}),
              "quaternion: +90 deg about X maps -Z to +Y (look up)");

        // Composition order matches the matrix convention: (a * b) applies b first
        const vec3 v{1.0f, 0.0f, 0.0f};
        const vec3 composed = (yaw * pitch).rotate(v);
        const vec3 stepwise = yaw.rotate(pitch.rotate(v));
        check(near3(composed, stepwise), "quaternion composition: (a * b) applies b first");
        check(near3(composed, yaw.rotate(v)), "rotation about X does not move +X, so only yaw shows");

        // Rotation matrices agree with the 3x3/4x4 generators
        check(near3(yaw.rotate(v), yaw.toMat3() * v), "quat::rotate matches quat::toMat3");
        check(near3(yaw.rotate(v), mat4::fromQuat(yaw).transformDirection(v)),
              "quat::rotate matches mat4::fromQuat");

        // Inverse and conjugate
        const quat yaw_inv = yaw.conjugate();
        check(near3(yaw_inv.rotate(yaw.rotate(v)), v), "conjugate undoes the rotation");
        const quat unity = yaw * yaw.conjugate();
        check(near(unity.w, 1.0f) && near(unity.x, 0.0f) && near(unity.y, 0.0f) && near(unity.z, 0.0f),
              "q * conjugate(q) is the identity for a unit quaternion");

        // Normalization
        const quat scaled{2.0f, 0.0f, 0.0f, 0.0f};
        check(near(scaled.normalized().w, 1.0f), "normalized() scales a non-unit quaternion");
        check(near(quat{0.0f, 0.0f, 0.0f, 0.0f}.normalized().w, 1.0f),
              "a zero quaternion normalizes to the identity instead of NaN");

        // fromTo and slerp
        const quat to_z = quat::fromTo(vec3{1.0f, 0.0f, 0.0f}, vec3{0.0f, 0.0f, -1.0f});
        check(near3(to_z.rotate(vec3{1.0f, 0.0f, 0.0f}), vec3{0.0f, 0.0f, -1.0f}),
              "fromTo maps the source direction onto the target");
        const quat half = quat::slerp(identity, yaw, 0.5f);
        check(near3(half.rotate(v), vec3{0.7071068f, 0.0f, -0.7071068f}, 1e-3f),
              "slerp halfway between identity and +90 deg about Y gives 45 deg");
        check(near4(vec4{quat::slerp(identity, yaw, 0.0f).w, quat::slerp(identity, yaw, 0.0f).x,
                          quat::slerp(identity, yaw, 0.0f).y, quat::slerp(identity, yaw, 0.0f).z},
                    vec4{1.0f, 0.0f, 0.0f, 0.0f}),
              "slerp(t=0) returns the start rotation");
    }

    // ------------------------------------------------------------------ mat3
    {
        const mat3 r = mat3::rotateY(kHalfPi);
        check(near3(r * vec3{1.0f, 0.0f, 0.0f}, vec3{0.0f, 0.0f, -1.0f}), "mat3::rotateY maps +X to -Z");
        check(near3(r.transpose() * vec3{0.0f, 0.0f, -1.0f}, vec3{1.0f, 0.0f, 0.0f}),
              "transposing a rotation inverts it");
        check(near3((r.inverse() * r) * vec3{1.0f, 2.0f, 3.0f}, vec3{1.0f, 2.0f, 3.0f}),
              "mat3 inverse undoes the rotation");
        check(near(r.determinant(), 1.0f), "a rotation matrix has determinant 1");

        const quat q = quat::fromAxisAngle(vec3{0.0f, 0.0f, 1.0f}, kHalfPi);
        check(near3(q.toMat3() * vec3{1.0f, 0.0f, 0.0f}, vec3{0.0f, 1.0f, 0.0f}),
              "quat::toMat3: +90 deg about Z maps +X to +Y");

        const mat3 singular = mat3::scale(vec3{1.0f, 0.0f, 1.0f});
        check(nearMat4(mat4::fromMat3(singular.inverse()), mat4::fromMat3(mat3{}), 1e-3f),
              "a singular mat3 inverts to identity instead of NaN");
    }

    // --------------------------------------------------------- matrix inverse
    {
        const mat4 model = mat4::fromTRS(vec3{1.0f, -2.0f, 3.0f},
                                         quat::fromAxisAngle(vec3{0.3f, 1.0f, 0.5f}, 1.1f),
                                         vec3{2.0f, 0.5f, 1.5f});
        const mat4 inv = model.inverse();
        check(nearMat4(model * inv, mat4{}, 1e-3f), "M * inverse(M) is the identity for a TRS matrix");
        check(near(inv.at(3, 3), 1.0f), "the inverse of an affine matrix keeps (3,3) = 1");

        // Determinant invariants: they catch a wrongly scaled adjugate, which is exactly
        // the bug that a point round-trip through transformPoint() can hide, because the
        // perspective divide cancels a uniform scale error.
        check(near(mat4::rotateY(0.7f).determinant(), 1.0f), "a rotation matrix has determinant 1");
        check(near(model.determinant(), 2.0f * 0.5f * 1.5f, 1e-3f),
              "the determinant of T * R * S is the product of the scale factors");

        const vec3 point{1.5f, -0.25f, 4.0f};
        check(near3(inv.transformPoint(model.transformPoint(point)), point, 1e-3f),
              "inverse() round-trips a point through the model matrix");

        const mat4 rotation = mat4::rotateY(0.7f);
        mat4 rotation_transposed = rotation.transpose();
        rotation_transposed.set(3, 3, 1.0f);
        check(nearMat4(rotation.inverse(), rotation_transposed, 1e-3f),
              "the inverse of a pure rotation is its transpose");

        const mat4 view = mat4::lookAt(vec3{4.0f, 3.0f, 2.0f}, vec3{0.0f, 1.0f, 0.0f},
                                       vec3{0.0f, 1.0f, 0.0f});
        check(nearMat4(view * view.inverse(), mat4{}, 1e-3f), "a view matrix is invertible");
        check(near(view.determinant(), 1.0f, 1e-3f), "a view matrix has determinant 1 (rigid transform)");
        check(near3(view.inverse().transformPoint(vec3{0.0f, 0.0f, 0.0f}), vec3{4.0f, 3.0f, 2.0f}),
              "inverse(view) puts the camera back at its world position");

        const mat4 singular = mat4::scale(vec3{1.0f, 0.0f, 1.0f});
        check(near(singular.determinant(), 0.0f), "a flattened scale has determinant 0");
        check(nearMat4(singular.inverse(), mat4{}), "a singular matrix inverts to identity, not NaN");
    }

    std::printf("\n%s (%d failure(s))\n", g_failures == 0 ? "all checks passed" : "FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
