#include "Character.h"

#include <algorithm>
#include <cmath>

#include "core/math/quat.h"

namespace
{
// Orbit limits: the pitch is the angle of the camera offset above the horizontal plane.
// [-89, 89] keeps the offset from becoming parallel to `up`, which would make lookAt
// degenerate.
constexpr float kMinPitchDegrees = -89.0f;
constexpr float kMaxPitchDegrees = 89.0f;
constexpr float kMinCameraDistance = 0.5f;
constexpr float kMaxCameraDistance = 100.0f;
constexpr float kPi = 3.14159265358979323846f;
}  // namespace

// ---------------- CharacterController ----------------

vec3 CharacterController::movementDirection(const Camera& camera) const
{
    // Flatten the view direction: looking up or down must not change the plane the
    // character walks on.
    vec3 forward_dir = camera.forward();
    forward_dir.y = 0.0f;
    if (forward_dir.length() < 1e-6f)
    {
        return vec3{};  // camera looking straight down/up: no horizontal heading
    }
    forward_dir = forward_dir.normalized();

    // right = forward x up; with forward = (0,0,-1) and up = (0,1,0) this is (1,0,0)
    const vec3 right_dir = forward_dir.cross(vec3{0.0f, 1.0f, 0.0f}).normalized();
    if (right_dir.length() < 1e-6f)
    {
        return vec3{};
    }

    vec3 direction;
    if (input.forward)
    {
        direction += forward_dir;
    }
    if (input.backward)
    {
        direction -= forward_dir;
    }
    if (input.right)
    {
        direction += right_dir;
    }
    if (input.left)
    {
        direction -= right_dir;
    }

    if (direction.length() < 1e-6f)
    {
        return vec3{};  // no input, or opposite keys cancelling out
    }
    return direction.normalized();  // normalized so that diagonal movement is not faster
}

void CharacterController::update(vec3& position, const Camera& camera, float dt) const
{
    if (dt <= 0.0f)
    {
        return;
    }

    const vec3 direction = movementDirection(camera);
    if (direction.length() < 1e-6f)
    {
        return;
    }

    position += direction * (currentSpeed() * dt);
}

// ---------------- Character ----------------

Character::Character()
{
    syncCamera();
}

void Character::update(float dt)
{
    controller.update(position, camera, dt);
    syncCamera();  // move the camera along with the character (per view mode)
}

void Character::setView(CameraView view)
{
    if (this->view == view)
    {
        return;
    }
    this->view = view;
    syncCamera();
}

void Character::syncCamera()
{
    if (view == CameraView::FPV)
    {
        // First person: sit at eye height. The orientation is owned by the player, so it
        // must NOT be overwritten here.
        camera.setPosition(position + vec3{0.0f, eye_height, 0.0f});
        return;
    }

    // Third person: camera at "position + offset", looking at the character. The look-at
    // target height is controlled by camera_target_height (0 = the feet).
    camera.setPosition(position + camera_offset);
    camera.lookAt(position + vec3{0.0f, camera_target_height, 0.0f});
}

void Character::orbitCamera(float yawDegrees, float pitchDegrees)
{
    if (camera_offset.length() < 1e-6f)
    {
        return;  // degenerate: zero offset, nothing to orbit
    }

    // (1) Horizontal orbit: rotate the offset around world Y with a quaternion. Only the
    // direction changes, the length is preserved.
    vec3 offset = camera_offset;
    if (yawDegrees != 0.0f)
    {
        offset = quat::fromAxisAngle(vec3{0.0f, 1.0f, 0.0f}, yawDegrees * kPi / 180.0f).rotate(offset);
    }

    // (2) Pitch: rotate around the horizontal axis perpendicular to the offset, with the
    // angle between the offset and the horizontal plane clamped to [5, 89] degrees.
    if (pitchDegrees != 0.0f)
    {
        const float horizontal = std::hypot(offset.x, offset.z);
        const float current_pitch = std::atan2(offset.y, horizontal) * 180.0f / kPi;
        const float target_pitch = std::clamp(current_pitch + pitchDegrees,
                                              kMinPitchDegrees, kMaxPitchDegrees);
        const float applied = target_pitch - current_pitch;

        // cross(offset, up) points to the right when looking from the camera towards the
        // character; rotating around it by a positive angle raises the camera.
        vec3 right_axis = offset.cross(vec3{0.0f, 1.0f, 0.0f});
        if (right_axis.length() > 1e-6f)
        {
            offset = quat::fromAxisAngle(right_axis, applied * kPi / 180.0f).rotate(offset);
        }
    }

    camera_offset = offset;
    syncCamera();
}

void Character::setCameraDistance(float distance)
{
    distance = std::clamp(distance, kMinCameraDistance, kMaxCameraDistance);

    const float current = camera_offset.length();
    if (current < 1e-6f)
    {
        camera_offset = vec3{0.0f, 2.0f, distance};
    }
    else
    {
        camera_offset *= (distance / current);  // only the length changes
    }

    syncCamera();
}
