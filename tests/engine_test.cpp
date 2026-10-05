// Unit tests for the engine layer: camera, model instances and the character controller.
//
// These link only stv3d_engine (which links only stv3d_core): no Qt, no OpenGL, no window.
// They exist because Camera/Model/Character were converted to core math types - as long as
// this executable builds, those three files are free of Qt.

#include "game/Camera.h"
#include "game/Character.h"
#include "game/Model.h"

#include "core/math/mat4.h"
#include "core/math/quat.h"
#include "core/math/vec3.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

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

bool near(float a, float b, float eps = 1e-3f)
{
    return std::fabs(a - b) < eps;
}

bool near3(const vec3& a, const vec3& b, float eps = 1e-3f)
{
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps);
}

bool nearMat4(const mat4& a, const mat4& b, float eps = 1e-3f)
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
    // ================================ camera ================================
    {
        Camera camera;
        check(near3(camera.getPosition(), vec3{0.0f, 0.0f, 3.0f}), "camera: default position");
        check(near3(camera.forward(), vec3{0.0f, 0.0f, -1.0f}), "camera: default forward is -Z");
        check(near3(camera.right(), vec3{1.0f, 0.0f, 0.0f}), "camera: default right is +X");
        check(near3(camera.up(), vec3{0.0f, 1.0f, 0.0f}), "camera: default up is +Y");
        check(near(camera.pitchDegrees(), 0.0f), "camera: default pitch is 0");
        check(near(camera.yawDegrees(), 0.0f), "camera: default yaw is 0");

        const mat4 view = camera.viewMatrix();
        check(near3(view.transformPoint(camera.getPosition()), vec3{0.0f, 0.0f, 0.0f}),
              "camera: viewMatrix maps the position to the origin");
        check(near3(view.transformPoint(vec3{0.0f, 0.0f, 0.0f}), vec3{0.0f, 0.0f, -3.0f}),
              "camera: viewMatrix maps the world origin onto -Z");
        check(near(nearMat4(view * view.inverse(), mat4{}) ? 1.0f : 0.0f, 1.0f),
              "camera: the view matrix is invertible");
    }

    // ---------- lookAt ----------
    {
        Camera camera;
        camera.setPosition(vec3{0.0f, 2.0f, 5.0f});
        camera.lookAt(vec3{0.0f, 1.6f, 0.0f});

        const vec3 expected = (vec3{0.0f, 1.6f, 0.0f} - camera.getPosition()).normalized();
        check(near3(camera.forward(), expected), "camera.lookAt points forward at the target");
        check(near(camera.right().y, 0.0f, 1e-3f), "camera.lookAt produces no roll");

        // Degenerate case: looking straight down while `up` is +Y
        Camera down;
        down.setPosition(vec3{0.0f, 5.0f, 0.0f});
        down.lookAt(vec3{0.0f, 0.0f, 0.0f});
        bool finite = std::isfinite(down.forward().x) && std::isfinite(down.forward().y)
                      && std::isfinite(down.forward().z);
        for (float value : down.viewMatrix().m)
        {
            finite = finite && std::isfinite(value);
        }
        check(finite, "camera.lookAt stays finite for a view direction parallel to up");
    }

    // ---------- rotation, quaternion properties ----------
    {
        Camera camera;
        camera.rotateWorld(90.0f, vec3{0.0f, 1.0f, 0.0f});
        check(near3(camera.forward(), vec3{-1.0f, 0.0f, 0.0f}, 1e-3f),
              "camera.rotateWorld(+90 deg about Y) turns forward to -X");
        check(near(camera.yawDegrees(), 90.0f, 0.1f), "camera: yaw reads back 90 degrees");
        check(near3(camera.up(), vec3{0.0f, 1.0f, 0.0f}), "camera: pure yaw leaves up unchanged");

        Camera pitch;
        pitch.rotateLocal(90.0f, vec3{1.0f, 0.0f, 0.0f});
        check(near3(pitch.forward(), vec3{0.0f, 1.0f, 0.0f}, 1e-3f),
              "camera.rotateLocal(+90 deg about X) looks straight up");
        check(near(pitch.pitchDegrees(), 90.0f, 0.1f), "camera: pitch reads back 90 degrees");

        Camera clamped;
        clamped.yawPitch(0.0f, 1000.0f);
        check(near(clamped.pitchDegrees(), 85.0f, 0.5f), "camera.yawPitch clamps pitch at +85");
        clamped.yawPitch(0.0f, -2000.0f);
        check(near(clamped.pitchDegrees(), -85.0f, 0.5f), "camera.yawPitch clamps pitch at -85");

        // A long random sequence must not introduce roll or denormalize the quaternion
        Camera spin;
        std::srand(1234);
        for (int i = 0; i < 500; ++i)
        {
            spin.yawPitch(static_cast<float>(std::rand() % 720 - 360),
                          static_cast<float>(std::rand() % 200 - 100));
        }
        check(near(spin.getOrientation().length(), 1.0f, 1e-4f),
              "camera: orientation stays a unit quaternion after 500 rotations");
        check(near(spin.right().y, 0.0f, 1e-3f), "camera: no roll accumulates (right stays level)");
        check(near(spin.forward().dot(spin.right()), 0.0f) && near(spin.forward().dot(spin.up()), 0.0f)
                  && near(spin.right().dot(spin.up()), 0.0f),
              "camera: the three axes stay orthogonal");
    }

    // ---------- movement ----------
    {
        Camera camera;
        camera.moveLocal(1.0f, 0.0f, 0.0f);
        check(near3(camera.getPosition(), vec3{0.0f, 0.0f, 2.0f}), "camera.moveLocal moves forward");
        camera.moveLocal(0.0f, 2.0f, 0.0f);
        check(near3(camera.getPosition(), vec3{2.0f, 0.0f, 2.0f}), "camera.moveLocal strafes right");
        camera.moveLocal(0.0f, 0.0f, 1.5f);
        check(near3(camera.getPosition(), vec3{2.0f, 1.5f, 2.0f}), "camera.moveLocal rises");
        camera.move(vec3{1.0f, 0.0f, 0.0f});
        check(near3(camera.getPosition(), vec3{3.0f, 1.5f, 2.0f}), "camera.move translates in world space");
    }

    // ---------- projection: OpenGL vs Vulkan clip conventions ----------
    {
        Camera camera;
        camera.setPerspective(90.0f, 0.1f, 100.0f);
        camera.setViewportAspect(2.0f);

        const mat4 gl = camera.projectionMatrix();
        const vec4 gl_near = gl * vec4{0.0f, 0.0f, -0.1f, 1.0f};
        const vec4 gl_far = gl * vec4{0.0f, 0.0f, -100.0f, 1.0f};
        check(near(gl_near.z / gl_near.w, -1.0f), "camera: GL projection maps near to depth -1");
        check(near(gl_far.z / gl_far.w, 1.0f), "camera: GL projection maps far to depth +1");

        camera.setClipDepth(ClipDepth::ZeroToOne);
        camera.setFlipY(true);
        const mat4 vk = camera.projectionMatrix();
        const vec4 vk_near = vk * vec4{0.0f, 0.0f, -0.1f, 1.0f};
        const vec4 vk_far = vk * vec4{0.0f, 0.0f, -100.0f, 1.0f};
        check(near(vk_near.z / vk_near.w, 0.0f), "camera: Vulkan projection maps near to depth 0");
        check(near(vk_far.z / vk_far.w, 1.0f), "camera: Vulkan projection maps far to depth 1");
        check(vk.m[5] < 0.0f && gl.m[5] > 0.0f, "camera: flipY negates the Y scale only");

        camera.setViewportAspect(0.0f);
        check(near(camera.getAspect(), 1.0f), "camera: an invalid aspect is clamped to 1");
        camera.setPerspective(300.0f, 0.1f, 100.0f);
        check(near(camera.getFovYDegrees(), 179.0f), "camera: an invalid fov is clamped to 179");
    }

    // ================================= model ================================
    {
        Model model;
        check(!model.hasMesh(), "model: no mesh by default");
        check(nearMat4(model.modelMatrix(), mat4{}), "model: default model matrix is the identity");

        model.setPosition(vec3{3.0f, -2.0f, 5.0f});
        check(near3(model.modelMatrix().transformPoint(vec3{0.0f, 0.0f, 0.0f}), vec3{3.0f, -2.0f, 5.0f}),
              "model: the local origin maps onto the position");
        model.translate(vec3{1.0f, 1.0f, 1.0f});
        check(near3(model.getPosition(), vec3{4.0f, -1.0f, 6.0f}), "model: translate accumulates");

        Model trs;
        trs.setPosition(vec3{1.0f, 0.0f, 0.0f});
        trs.setRotationDegrees(90.0f, vec3{0.0f, 1.0f, 0.0f});
        trs.setUniformScale(2.0f);
        // (1,0,0) --scale--> (2,0,0) --rotate 90 about Y--> (0,0,-2) --translate--> (1,0,-2)
        check(near3(trs.modelMatrix().transformPoint(vec3{1.0f, 0.0f, 0.0f}), vec3{1.0f, 0.0f, -2.0f}),
              "model: the transform order is S -> R -> T");
        check(nearMat4(trs.modelMatrix(),
                       mat4::fromTRS(trs.getPosition(), trs.getRotation(), trs.getScale())),
              "model: modelMatrix equals mat4::fromTRS");

        Model spin;
        spin.setSpin(90.0f, vec3{0.0f, 1.0f, 0.0f});
        spin.updateSpin(1.0f);
        check(near3(spin.modelMatrix().transformDirection(vec3{1.0f, 0.0f, 0.0f}),
                    vec3{0.0f, 0.0f, -1.0f}),
              "model: updateSpin(1s) at 90 deg/s turns +X into -Z");
        spin.updateSpin(0.5f);
        const vec3 half_turn = spin.modelMatrix().transformDirection(vec3{1.0f, 0.0f, 0.0f});
        check(near3(half_turn, vec3{-0.7071f, 0.0f, -0.7071f}), "model: spin keeps accumulating");

        Model still;
        still.setSpin(0.0f, vec3{0.0f, 1.0f, 0.0f});
        still.updateSpin(5.0f);
        check(near3(still.modelMatrix().transformDirection(vec3{1.0f, 0.0f, 0.0f}), vec3{1.0f, 0.0f, 0.0f}),
              "model: zero spin does not rotate");
        still.updateSpin(-1.0f);
        check(near3(still.modelMatrix().transformDirection(vec3{1.0f, 0.0f, 0.0f}), vec3{1.0f, 0.0f, 0.0f}),
              "model: a negative dt does not rotate");
    }

    // ========================== character controller =======================
    {
        Camera camera;
        CharacterController controller;
        CharacterController::InputState input;

        input.forward = true;
        controller.setInput(input);
        check(near3(controller.movementDirection(camera), vec3{0.0f, 0.0f, -1.0f}),
              "controller: forward follows the camera heading");

        input.forward = false;
        input.right = true;
        controller.setInput(input);
        check(near3(controller.movementDirection(camera), vec3{1.0f, 0.0f, 0.0f}),
              "controller: right strafes to +X");

        input.forward = true;  // forward + right
        controller.setInput(input);
        check(near(controller.movementDirection(camera).length(), 1.0f),
              "controller: diagonal input is normalized");

        input.right = false;
        input.sprint = true;
        controller.setInput(input);
        check(near(controller.currentSpeed(), controller.getSpeeds().sprint),
              "controller: sprint raises the target speed");

        vec3 position{0.0f, 0.0f, 0.0f};
        controller.update(position, camera, 0.5f);
        check(near3(position, vec3{0.0f, 0.0f, -1.0f} * (controller.getSpeeds().sprint * 0.5f)),
              "controller: update moves by speed * dt");

        // Looking down must not make the character fly
        Camera looking_down;
        looking_down.setPosition(vec3{0.0f, 3.0f, 3.0f});
        looking_down.lookAt(vec3{0.0f, 0.0f, 0.0f});
        input.sprint = false;
        controller.setInput(input);
        check(near(controller.movementDirection(looking_down).y, 0.0f),
              "controller: the heading stays horizontal while looking down");
    }

    // ============================== character ==============================
    {
        Character character;
        check(character.getView() == CameraView::TPV, "character: default view is TPV");
        check(near3(character.getCamera().getPosition(), character.getPosition() + character.getCameraOffset()),
              "character: TPV camera sits at position + offset");
        check(near3(character.getCamera().forward(),
                    (character.getPosition() + vec3{0.0f, 1.6f, 0.0f} - character.getCamera().getPosition()).normalized()),
              "character: TPV camera looks at the character");

        // FPV keeps the player's orientation
        character.getCamera().yawPitch(35.0f, -12.0f);
        const quat before = character.getCamera().getOrientation();
        character.setView(CameraView::FPV);
        check(near3(character.getCamera().getPosition(),
                    character.getPosition() + vec3{0.0f, character.getEyeHeight(), 0.0f}),
              "character: FPV camera sits at eye height");
        check(near(character.getCamera().getOrientation().dot(before), 1.0f, 1e-4f),
              "character: syncCamera does not overwrite the FPV orientation");

        // Walk forward and check that the camera follows
        CharacterController::InputState input;
        input.forward = true;
        character.getController().setInput(input);
        const vec3 heading = character.getCamera().forward();
        character.update(1.0f);
        const vec3 expected_dir = vec3{heading.x, 0.0f, heading.z}.normalized();
        check(near3(character.getPosition(), expected_dir * character.getController().currentSpeed()),
              "character: FPV walking follows the camera heading");
        check(near3(character.getCamera().getPosition(),
                    character.getPosition() + vec3{0.0f, character.getEyeHeight(), 0.0f}),
              "character: the FPV camera keeps following");

        // Back to TPV: the orbit offset survives and the camera looks at the character again
        character.setView(CameraView::TPV);
        check(near3(character.getCamera().getPosition(), character.getPosition() + character.getCameraOffset()),
              "character: switching back to TPV restores the orbit placement");
        check(near3(character.getCamera().forward(),
                    (character.getPosition() + vec3{0.0f, 1.6f, 0.0f} - character.getCamera().getPosition()).normalized()),
              "character: TPV looks at the character again");
    }

    // ---------- TPV orbit limits and zoom ----------
    {
        Character character;
        const float radius = character.cameraDistance();
        character.orbitCamera(90.0f, 0.0f);
        check(near(character.cameraDistance(), radius), "orbit: the radius is preserved");
        check(near(character.getCamera().getPosition().y - character.getPosition().y, 2.0f),
              "orbit: a horizontal orbit keeps the height");

        Character dive;
        for (int i = 0; i < 50; ++i)
        {
            dive.orbitCamera(0.0f, -10.0f);
        }
        const vec3 dive_offset = dive.getCameraOffset();
        const float dive_pitch = std::atan2(dive_offset.y, std::hypot(dive_offset.x, dive_offset.z)) * 180.0f / kPi;
        check(near(dive_pitch, -89.0f, 0.5f), "orbit: the downward pitch stops at -89 degrees");
        check(std::hypot(dive_offset.x, dive_offset.z) > 0.01f,
              "orbit: the offset keeps a horizontal part so lookAt cannot degenerate");

        Character rise;
        for (int i = 0; i < 50; ++i)
        {
            rise.orbitCamera(0.0f, 10.0f);
        }
        const vec3 rise_offset = rise.getCameraOffset();
        const float rise_pitch = std::atan2(rise_offset.y, std::hypot(rise_offset.x, rise_offset.z)) * 180.0f / kPi;
        check(near(rise_pitch, 89.0f, 0.5f), "orbit: the upward pitch stops at +89 degrees");
        check(rise_offset.y > 0.0f, "orbit: the camera ends up above the character");

        Character zoom;
        zoom.setCameraDistance(2.0f);
        check(near(zoom.cameraDistance(), 2.0f), "zoom: setCameraDistance takes effect");
        zoom.setCameraDistance(0.0f);
        check(near(zoom.cameraDistance(), 0.5f), "zoom: too close is clamped to 0.5");
        zoom.setCameraDistance(999.0f);
        check(near(zoom.cameraDistance(), 100.0f), "zoom: too far is clamped to 100");
    }

    std::printf("\n%s (%d failure(s))\n", g_failures == 0 ? "all checks passed" : "FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
