#ifndef GAME_CAMERA_H
#define GAME_CAMERA_H

#include <algorithm>
#include <cmath>

#include "core/math/conventions.h"
#include "core/math/mat4.h"
#include "core/math/quat.h"
#include "core/math/vec3.h"

// View modes: FPV = first person (camera at the character's eyes),
// TPV = third person (camera orbits the character).
enum class CameraView
{
    FPV,
    TPV  // orbit camera
};

// Camera: pure math.
//
// Orientation is a unit quaternion - no Euler angles (yaw/pitch/roll) and no stored
// target/up. The quaternion rotates the camera's local frame into world space, with the
// fixed local axes  forward = (0,0,-1), right = (1,0,0), up = (0,1,0).
// Therefore:
//   * forward()/right()/up() are always orthonormal and free of gimbal lock
//   * pitch clamping needs no Euler state: the allowed delta is derived from the
//     current pitch (computed from forward()) and the requested amount
//
// State is just: position + orientation + projection parameters. Matrices are computed on
// demand, so there is no way to "forget" to update a cached matrix.
//
// The clip-space convention (depth range and Y direction) is selectable instead of being
// hard-coded, because it is the one thing that genuinely differs between OpenGL
// ([-1,1], +Y up) and Vulkan/D3D ([0,1], +Y down). See core/math/conventions.h.
class Camera
{
public:
    Camera() = default;

    Camera(const vec3& position, const quat& orientation)
        : position(position), orientation(orientation.normalized())
    {
    }

    // ---------- position ----------
    void setPosition(const vec3& position) { this->position = position; }
    vec3 getPosition() const { return position; }

    // ---------- orientation (unit quaternion) ----------
    void setOrientation(const quat& orientation)
    {
        const quat normalized = orientation.normalized();
        this->orientation = (normalized.length() < 1e-6f) ? quat{} : normalized;
    }
    quat getOrientation() const { return orientation; }

    // Point the camera at `target`. The target and the up vector are converted into a
    // quaternion and not stored afterwards.
    void lookAt(const vec3& target, const vec3& worldUp = vec3{0.0f, 1.0f, 0.0f})
    {
        vec3 forward_dir = target - position;
        if (forward_dir.length() < 1e-6f)
        {
            return;  // degenerate: target coincides with the camera
        }
        forward_dir = forward_dir.normalized();

        vec3 up_axis = worldUp.normalized();
        if (std::fabs(forward_dir.dot(up_axis)) > 0.999f)
        {
            // View direction is (nearly) parallel to `up`: pick another reference axis so
            // that the cross product below does not collapse to a zero vector.
            up_axis = (forward_dir.y > 0.0f) ? vec3{0.0f, 0.0f, 1.0f} : vec3{0.0f, 0.0f, -1.0f};
        }

        const vec3 right_dir = forward_dir.cross(up_axis).normalized();
        const vec3 true_up = right_dir.cross(forward_dir);

        // Columns of the rotation matrix are the local basis vectors expressed in world
        // space: X = right, Y = up, Z = -forward
        orientation = quat::fromMat3(mat3::fromColumns(right_dir, true_up, -forward_dir)).normalized();
    }

    // ---------- rotation ----------
    // Around a world axis (pre-multiply): used for yaw around world Y
    void rotateWorld(float degrees, const vec3& worldAxis)
    {
        if (degrees == 0.0f || worldAxis.length() < 1e-6f)
        {
            return;
        }
        orientation = quat::fromAxisAngle(worldAxis, degrees * kDegToRad) * orientation;
        orientation = orientation.normalized();
    }

    // Around one of the camera's own axes (post-multiply): used for pitch around local X
    void rotateLocal(float degrees, const vec3& localAxis)
    {
        if (degrees == 0.0f || localAxis.length() < 1e-6f)
        {
            return;
        }
        orientation = orientation * quat::fromAxisAngle(localAxis, degrees * kDegToRad);
        orientation = orientation.normalized();
    }

    // Mouse look (FPV): yaw around world Y, pitch around local X, pitch clamped to
    // [minPitchDegrees, maxPitchDegrees]. No Euler angle is ever stored.
    void yawPitch(float yawDeltaDegrees, float pitchDeltaDegrees,
                  float minPitchDegrees = -85.0f, float maxPitchDegrees = 85.0f)
    {
        rotateWorld(yawDeltaDegrees, vec3{0.0f, 1.0f, 0.0f});

        if (pitchDeltaDegrees == 0.0f)
        {
            return;
        }

        // Clamp the target pitch and apply only the difference
        const float current = pitchDegrees();
        const float target = std::clamp(current + pitchDeltaDegrees, minPitchDegrees, maxPitchDegrees);
        rotateLocal(target - current, vec3{1.0f, 0.0f, 0.0f});
    }

    // ---------- local axes (derived from the quaternion, always orthonormal) ----------
    vec3 forward() const { return orientation.rotate(vec3{0.0f, 0.0f, -1.0f}); }
    vec3 right() const { return orientation.rotate(vec3{1.0f, 0.0f, 0.0f}); }
    vec3 up() const { return orientation.rotate(vec3{0.0f, 1.0f, 0.0f}); }

    // Pitch in degrees (+ = looking up); derived from forward(), used for clamping/debug
    float pitchDegrees() const
    {
        const float y = std::clamp(forward().y, -1.0f, 1.0f);
        return std::asin(y) * kRadToDeg;
    }

    // Yaw in degrees (+ = turning left; 0 when facing -Z)
    float yawDegrees() const
    {
        const vec3 heading = -forward();
        return std::atan2(heading.x, heading.z) * kRadToDeg;
    }

    // ---------- movement ----------
    void move(const vec3& worldDelta) { position += worldDelta; }

    // Move in the camera's own frame: forward along the view direction, right, upward
    void moveLocal(float forwardAmount, float rightAmount, float upwardAmount)
    {
        position += forward() * forwardAmount + right() * rightAmount + up() * upwardAmount;
    }

    // ---------- projection parameters ----------
    void setPerspective(float fovYDegrees, float nearPlane, float farPlane)
    {
        fov_y = std::clamp(fovYDegrees, 1.0f, 179.0f);
        near_plane = nearPlane;
        far_plane = farPlane;
    }

    void setViewportAspect(float aspect) { this->aspect = (aspect > 0.0f) ? aspect : 1.0f; }

    float getFovYDegrees() const { return fov_y; }
    float getAspect() const { return aspect; }
    float getNearPlane() const { return near_plane; }
    float getFarPlane() const { return far_plane; }

    // ---------- clip-space convention (OpenGL today, Vulkan later) ----------
    void setClipDepth(ClipDepth clip) { this->clip = clip; }
    ClipDepth getClipDepth() const { return clip; }
    void setFlipY(bool flip) { flip_y = flip; }
    bool getFlipY() const { return flip_y; }

    // ---------- matrices (computed on demand) ----------
    // view = inverse(translate(position) * rotate(orientation))
    mat4 viewMatrix() const
    {
        return mat4::fromQuat(orientation.conjugate()) * mat4::translate(-position);
    }

    mat4 projectionMatrix() const
    {
        return mat4::perspective(fov_y * kDegToRad, aspect, near_plane, far_plane, clip, flip_y);
    }

private:
    static constexpr float kPi = 3.14159265358979323846f;
    static constexpr float kDegToRad = kPi / 180.0f;
    static constexpr float kRadToDeg = 180.0f / kPi;

    vec3 position{0.0f, 0.0f, 3.0f};
    quat orientation;  // identity = looking down -Z with +Y up

    float fov_y = 90.0f;
    float aspect = 1.0f;
    // Note: `near` and `far` are macros defined by <windows.h>,
    // so these members carry explicit plane names.
    float near_plane = 0.1f;
    float far_plane = 100.0f;

    ClipDepth clip = ClipDepth::NegativeOneToOne;  // OpenGL default
    bool flip_y = false;
};

#endif  // GAME_CAMERA_H
