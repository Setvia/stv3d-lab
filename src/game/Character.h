#ifndef GAME_CHARACTER_H
#define GAME_CHARACTER_H

#include "Camera.h"
#include "core/math/vec3.h"

// Character controller: turns "key intents" into a world-space displacement.
//
// The controller deliberately does NOT own a position - the caller passes one by
// reference. That keeps the state explicit, lets the same controller drive any body and
// makes it trivially unit-testable.
class MyCharacterController
{
public:
    struct InputState
    {
        bool forward = false;
        bool backward = false;
        bool left = false;
        bool right = false;
        bool jump = false;
        bool sprint = false;
    };

    struct Speeds
    {
        float walk = 5.0f;    // m/s
        float sprint = 9.0f;  // m/s
    };

    void setInput(const InputState& input) { m_input = input; }
    const InputState& input() const { return m_input; }

    void setSpeeds(const Speeds& speeds) { m_speeds = speeds; }
    const Speeds& speeds() const { return m_speeds; }

    // Target speed for the current input (holding sprint raises it)
    float currentSpeed() const { return m_input.sprint ? m_speeds.sprint : m_speeds.walk; }

    // World-space movement direction derived from the camera's facing.
    // Only the horizontal part of the view direction is used, so looking up or down never
    // makes the character fly. Returns a unit vector, or (0,0,0) when there is no input.
    vec3 movementDirection(const MyCamera& camera) const;

    // Apply this frame's input to `position` (distance = speed * dt)
    void update(vec3& position, const MyCamera& camera, float dt) const;

private:
    InputState m_input;
    Speeds m_speeds;
};

// Character: position + camera + controller. Each update advances the position and then
// places the camera according to the current view mode (FPV or TPV).
class MyCharacter
{
public:
    MyCharacter();

    // dt is in seconds
    void update(float dt);

    // ---------- view mode ----------
    // FPV: the camera sits at eye height and its orientation is fully driven by the mouse
    //      (syncCamera must not touch the orientation)
    // TPV: the camera sits at "position + offset" and looks at the character
    void setView(CameraView view);
    CameraView view() const { return m_view; }

    // FPV eye height in metres above the character's feet
    void setEyeHeight(float height)
    {
        m_eye_height = height;
        syncCamera();
    }
    float eyeHeight() const { return m_eye_height; }

    // Place the camera according to the current view mode
    void syncCamera();

    vec3 position() const { return m_position; }
    void setPosition(const vec3& position)
    {
        m_position = position;
        syncCamera();
    }

    MyCamera& camera() { return m_camera; }
    const MyCamera& camera() const { return m_camera; }

    MyCharacterController& controller() { return m_controller; }
    const MyCharacterController& controller() const { return m_controller; }

    // ---------- TPV orbit ----------
    // Camera offset relative to the character (third person: behind and above)
    void setCameraOffset(const vec3& offset)
    {
        m_camera_offset = offset;
        syncCamera();
    }
    vec3 cameraOffset() const { return m_camera_offset; }

    // Mouse drag: orbit around the character. The pitch (angle of the offset above the
    // horizontal plane) is clamped to [minPitch, maxPitch] degrees so that the camera
    // cannot dive under the character's feet or flip over its head.
    void orbitCamera(float yawDegrees, float pitchDegrees);

    // Wheel: move closer / further away, clamped to [0.5, 100] metres
    void setCameraDistance(float distance);
    float cameraDistance() const { return m_camera_offset.length(); }

private:
    vec3 m_position{0.0f, 0.0f, 0.0f};
    vec3 m_camera_offset{0.0f, 2.0f, 5.0f};
    float m_camera_target_height = 1.6f;  // TPV: the point the camera looks at (head height)
    float m_eye_height = 1.6f;            // FPV: eye height above the feet
    CameraView m_view = CameraView::TPV;

    MyCamera m_camera;
    MyCharacterController m_controller;
};

#endif  // GAME_CHARACTER_H
