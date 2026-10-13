#ifndef GAME_CHARACTER_H
#define GAME_CHARACTER_H

#include "Camera.h"
#include "core/math/vec3.h"

// Character controller: turns "key intents" into a world-space displacement.
//
// The controller does not own a position - the caller passes one by
// reference. That keeps the state explicit, lets the same controller drive any body and
// makes it trivially unit-testable.
class CharacterController
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

    void setInput(const InputState& input) { this->input = input; }
    const InputState& getInput() const { return input; }

    void setSpeeds(const Speeds& speeds) { this->speeds = speeds; }
    const Speeds& getSpeeds() const { return speeds; }

    // Target speed for the current input (holding sprint raises it)
    float currentSpeed() const { return input.sprint ? speeds.sprint : speeds.walk; }

    // World-space movement direction derived from the camera's facing.
    // Only the horizontal part of the view direction is used, so looking up or down never
    // makes the character fly. Returns a unit vector, or (0,0,0) when there is no input.
    vec3 movementDirection(const Camera& camera) const;

    // Apply this frame's input to `position` (distance = speed * dt)
    void update(vec3& position, const Camera& camera, float dt) const;

private:
    InputState input;
    Speeds speeds;
};

// Character: position + camera + controller. Each update advances the position and then
// places the camera according to the current view mode (FPV or TPV).
class Character
{
public:
    Character();

    // dt is in seconds
    void update(float dt);

    // ---------- view mode ----------
    // FPV: the camera sits at eye height and its orientation is fully driven by the mouse
    //      (syncCamera must not touch the orientation)
    // TPV: the camera sits at "cameraTarget() + offset" and looks at cameraTarget()
    void setView(CameraView view);
    CameraView getView() const { return view; }

    // FPV eye height in metres above the character's feet
    void setEyeHeight(float height)
    {
        eye_height = height;
        syncCamera();
    }
    float getEyeHeight() const { return eye_height; }

    // Place the camera according to the current view mode
    void syncCamera();

    vec3 getPosition() const { return position; }
    void setPosition(const vec3& position)
    {
        this->position = position;
        syncCamera();
    }

    Camera& getCamera() { return camera; }
    const Camera& getCamera() const { return camera; }

    CharacterController& getController() { return controller; }
    const CharacterController& getController() const { return controller; }

    // ---------- TPV orbit ----------
    // The point the third-person camera orbits and looks at: the character plus
    // camera_target_height, i.e. the head. The orbit offset, the dolly and lookAt are all
    // measured from here, so moving in or out travels along the line to what is being looked
    // at. Measuring the offset from `position` instead puts the pivot at the feet while the
    // view stays anchored at the head: the two then drift apart as the camera moves, which
    // tilts the view and sinks the camera towards the origin.
    vec3 cameraTarget() const { return position + vec3{0.0f, camera_target_height, 0.0f}; }

    // Camera offset relative to cameraTarget() (third person: behind and above)
    void setCameraOffset(const vec3& offset)
    {
        camera_offset = offset;
        syncCamera();
    }
    vec3 getCameraOffset() const { return camera_offset; }

    // Mouse drag: orbit around cameraTarget(). The pitch (angle of the offset above the
    // horizontal plane) is clamped to [minPitch, maxPitch] degrees so that the offset cannot
    // become parallel to `up`, which is what would make lookAt degenerate.
    void orbitCamera(float yawDegrees, float pitchDegrees);

    // Wheel: move closer / further away, clamped to [0.5, 100] metres. The distance is
    // measured to cameraTarget(), so the point being looked at holds its place on screen.
    void setCameraDistance(float distance);
    float cameraDistance() const { return camera_offset.length(); }

private:
    vec3 position{0.0f, 0.0f, 0.0f};
    // Relative to cameraTarget(), not to the feet: (0, 0.4, 5) places the camera 2 m above
    // the character's feet and 5 m behind, which is what the old feet-relative (0, 2, 5) meant.
    vec3 camera_offset{0.0f, 0.4f, 5.0f};
    float camera_target_height = 1.6f;  // TPV: the point the camera looks at (head height)
    float eye_height = 1.6f;            // FPV: eye height above the feet
    CameraView view = CameraView::TPV;

    Camera camera;
    CharacterController controller;
};

#endif  // GAME_CHARACTER_H
