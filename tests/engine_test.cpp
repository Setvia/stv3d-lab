// Unit tests for the engine layer: camera, model instances, the character controller and the loop.
//
// These link only stv3d_engine (which links only stv3d_core): no window, no graphics API.
// They exist because Camera/Model/Character/GameLoop were converted to core types - as long as
// this executable builds, the engine stays within core and the standard library.

#include "game/Camera.h"
#include "game/Character.h"
#include "game/GameLoop.h"
#include "game/InputMapping.h"
#include "game/LoadQueue.h"
#include "game/Model.h"

#include "core/math/mat4.h"
#include "core/math/quat.h"
#include "core/math/vec3.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

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
        check(near3(character.getCamera().getPosition(),
                    character.cameraTarget() + character.getCameraOffset()),
              "character: TPV camera sits at cameraTarget() + offset");
        check(near3(character.getCamera().forward(),
                    (character.cameraTarget() - character.getCamera().getPosition()).normalized()),
              "character: TPV camera looks at cameraTarget()");

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
        check(near3(character.getCamera().getPosition(),
                    character.cameraTarget() + character.getCameraOffset()),
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

        // The dolly has to travel along the line to the point being looked at. While the offset
        // was measured from the feet, zooming slid the camera down towards the origin while
        // lookAt stayed on the head, so the view pitched up by ~1.9 degrees on the first notch
        // and ran away to ~72 degrees near the closest setting.
        Character dolly;
        const vec3 target = dolly.cameraTarget();
        const vec3 view_before = dolly.getCamera().forward();
        const float one_notch_in = dolly.cameraDistance() - 0.5f;

        dolly.setCameraDistance(one_notch_in);
        check(near(dolly.cameraDistance(), one_notch_in),
              "zoom: the distance asked for is the distance held");
        check(near3(dolly.getCamera().forward(), view_before),
              "zoom: dollying does not change the view direction");
        check(near((dolly.getCamera().getPosition() - target).length(), one_notch_in),
              "zoom: the reported distance is the distance to the look-at target");
        check(near3(dolly.cameraTarget(), target), "zoom: the camera keeps orbiting the same point");

        dolly.setCameraDistance(0.5f);
        check(near((dolly.getCamera().getPosition() - target).length(), 0.5f),
              "zoom: the closest setting leaves 0.5 m to the look-at target");
        check(dolly.getCamera().getPosition().y > target.y - 0.5f,
              "zoom: zooming in does not sink the camera towards the feet");
    }

    // ============================== game loop ==============================
    // The loop takes its elapsed time as an argument (advanceBy), so the accumulator can be
    // exercised with exact numbers instead of sleeping.
    {
        const double tick = 1.0 / 60.0;

        GameLoop loop;
        check(!loop.isActive(), "loop: not active before start");
        check(near(static_cast<float>(loop.getFixedTickSeconds()), 1.0f / 60.0f),
              "loop: the default fixed tick is 1/60 s");

        loop.init();
        check((loop.getState() & GameLoopFlags::INITIALIZED) != 0, "loop: init sets the INITIALIZED bit");
        check(!loop.isActive(), "loop: init alone does not activate the loop");

        int ticks = 0;
        int frames = 0;
        float last_dt = 0.0f;
        std::uint64_t last_tick_number = 0;
        loop.setTickCallback([&](std::uint64_t tickNumber) { ++ticks; last_tick_number = tickNumber; });
        loop.setFrameCallback([&](float dt) { ++frames; last_dt = dt; });

        // Nothing runs while the loop is inactive (the pending queue is the only exception, below)
        loop.advanceBy(1.0);
        check(ticks == 0 && frames == 0, "loop: an inactive loop runs nothing");

        loop.start();
        check(loop.isActive(), "loop: start activates the loop");
        check((loop.getState() & GameLoopFlags::TIME_ACTIVE) != 0, "loop: start enables time advance");

        loop.advanceBy(tick);
        check(ticks == 1, "loop: one tick of elapsed time produces exactly one logic tick");
        check(last_tick_number == 1, "loop: the tick callback receives the running tick number");
        check(frames == 1, "loop: one frame callback per advance");
        check(near(last_dt, static_cast<float>(tick)), "loop: the frame callback receives dt in seconds");

        // Sub-tick leftovers accumulate instead of producing a tick each time
        loop.advanceBy(0.01);
        check(ticks == 1, "loop: a leftover shorter than one tick produces no tick");
        check(near(static_cast<float>(loop.getTickAccumulator()), 0.01f),
              "loop: the leftover stays in the accumulator");
        loop.advanceBy(0.01);
        check(ticks == 2, "loop: two leftovers add up to one tick");
        check(near(static_cast<float>(loop.getTickAccumulator()), 0.0033f, 1e-3f),
              "loop: the accumulator keeps only the remainder");

        // A long stall is capped: at most 5 catch-up ticks, and the backlog is dropped
        const std::uint64_t before_stall = loop.getTickCount();
        loop.advanceBy(10.0);
        check(loop.getTickCount() - before_stall == 5, "loop: a long frame catches up at most 5 ticks");
        check(near(static_cast<float>(loop.getTickAccumulator()), 0.0f),
              "loop: the backlog is dropped after the catch-up cap (no death spiral)");

        // pause(): time stops, frames keep coming
        loop.pause();
        const int ticks_before_pause = ticks;
        const int frames_before_pause = frames;
        loop.advanceBy(1.0);
        check(ticks == ticks_before_pause, "loop: pause stops the logic ticks");
        check(frames == frames_before_pause + 1, "loop: pause still renders frames");
        check(loop.isActive(), "loop: pause keeps the loop active");

        loop.start();  // re-arm: this is also what resets the clock
        loop.advanceBy(tick);
        check(ticks == ticks_before_pause + 1, "loop: start() after pause resumes the logic ticks");

        // enqueue(): runs at the start of the next step, exactly once
        int queued = 0;
        loop.enqueue([&] { ++queued; });
        loop.enqueue([&] { ++queued; });
        loop.advanceBy(0.0);
        check(queued == 2, "loop: enqueued tasks run on the next advance");
        loop.advanceBy(0.0);
        check(queued == 2, "loop: enqueued tasks are removed after they ran");

        // TaskScheduler: one call per kind and step, clear() empties all three lists
        int scheduled_ticks = 0;
        int scheduled_frames = 0;
        int scheduled_realtime = 0;
        loop.getScheduler().addTickTask([&] { ++scheduled_ticks; });
        loop.getScheduler().addFrameTask([&] { ++scheduled_frames; });
        loop.getScheduler().addRealTimeTask([&] { ++scheduled_realtime; });
        loop.advanceBy(tick);
        check(scheduled_ticks == 1 && scheduled_frames == 1 && scheduled_realtime == 1,
              "loop: scheduler tasks run once per tick/frame");
        loop.getScheduler().clear();
        loop.advanceBy(tick);
        check(scheduled_ticks == 1 && scheduled_frames == 1 && scheduled_realtime == 1,
              "loop: scheduler::clear() removes all three task kinds");

        // Defensive input: a zero or negative delta must not tick, and non-positive settings are ignored
        const std::uint64_t before_zero = loop.getTickCount();
        loop.advanceBy(0.0);
        loop.advanceBy(-5.0);
        check(loop.getTickCount() == before_zero, "loop: a zero or negative delta produces no tick");
        loop.setFixedTickSeconds(0.0);
        check(near(static_cast<float>(loop.getFixedTickSeconds()), 1.0f / 60.0f),
              "loop: a non-positive fixed tick is ignored");
        loop.setFixedTickSeconds(0.5);
        check(near(static_cast<float>(loop.getFixedTickSeconds()), 0.5f),
              "loop: setFixedTickSeconds takes effect");

        // exit(): stops the loop, clears the queue and the scheduler
        loop.exit();
        check(!loop.isActive(), "loop: exit deactivates the loop");
        const int frames_before_exit_step = frames;
        loop.advanceBy(tick);
        check(frames == frames_before_exit_step, "loop: an exited loop runs nothing");
    }

    // ============================= load queue ==============================
    // The queue owns no thread and no timer: the loop decides when work runs, so a test can drive it
    // step by step and assert the exact order things happen in.
    {
        LoadQueue queue;
        check(queue.isIdle(), "loads: a fresh queue is idle");
        check(queue.pendingCount() == 0, "loads: a fresh queue has nothing pending");

        int produced = 0;
        int applied = 0;
        std::vector<int> order;

        const LoadHandle first = queue.request(LoadPriority::Streaming,
                                               [&] { ++produced; return true; },
                                               [&] { ++applied; order.push_back(1); return true; });
        check(first != kNoLoad, "loads: request hands out a handle");
        check(!queue.isIdle(), "loads: a queued item makes the queue busy");
        check(queue.pendingCount() == 1, "loads: a queued item counts as pending");

        check(queue.runPreload(), "loads: runPreload succeeds when there is nothing to preload");
        check(produced == 0 && applied == 0, "loads: runPreload leaves streaming items alone");

        check(queue.step(0) == 0, "loads: a zero budget runs nothing");
        check(queue.step(1) == 1, "loads: a budget of one runs one item");
        check(produced == 1 && applied == 1, "loads: a step runs both phases of an item");
        check(queue.isIdle() && queue.doneCount() == 1, "loads: the item is done and the queue idles again");
        check(queue.step(1) == 0, "loads: a finished item does not run a second time");
        check(!queue.cancel(first), "loads: a finished item cannot be cancelled");

        // Request order is completion order, and an empty phase counts as success
        queue.request(LoadPriority::Streaming, nullptr, [&] { order.push_back(2); return true; });
        queue.request(LoadPriority::Streaming, [&] { ++produced; return true; }, [&] { order.push_back(3); return true; });
        check(queue.pendingCount() == 2, "loads: two more requests are pending");

        check(queue.step(2) == 2, "loads: a budget of two runs two items");
        check(order.size() == 3 && order[1] == 2 && order[2] == 3, "loads: items run in request order");
        check(queue.doneCount() == 3 && queue.failedCount() == 0, "loads: successes are counted separately");

        // A failed produce skips the apply phase; a failed apply fails the item
        int skipped_apply = 0;
        queue.request(LoadPriority::Streaming, [] { return false; }, [&] { ++skipped_apply; return true; });
        queue.request(LoadPriority::Streaming, nullptr, [] { return false; });
        queue.step(2);
        check(skipped_apply == 0, "loads: a failed produce skips the apply phase");
        check(queue.failedCount() == 2, "loads: both failure kinds are counted as failures");
        check(queue.isIdle(), "loads: failed items do not stay pending");

        // A running item is not "pending": this is what a progress line reads from inside a phase
        LoadQueue running_queue;
        std::size_t pending_inside = 999;
        bool idle_inside = true;
        running_queue.request(LoadPriority::Streaming, nullptr, [&] {
            pending_inside = running_queue.pendingCount();
            idle_inside = running_queue.isIdle();
            return true;
        });
        check(running_queue.step(1) == 1, "loads: the running item is executed");
        check(pending_inside == 0, "loads: the item being run is not counted as pending");
        check(!idle_inside, "loads: the queue is not idle while an item runs");

        // Preload: run to completion on demand, in request order, and only that class
        LoadQueue preload_queue;
        int preload_done = 0;
        preload_queue.request(LoadPriority::Preload, [&] { ++preload_done; return true; }, nullptr);
        preload_queue.request(LoadPriority::Streaming, [&] { ++preload_done; return true; }, nullptr);
        preload_queue.request(LoadPriority::Preload, nullptr, [&] { ++preload_done; return true; });
        check(preload_queue.runPreload(), "loads: runPreload reports success");
        check(preload_done == 2, "loads: runPreload runs the preload items and only those");
        check(preload_queue.pendingCount() == 1, "loads: streaming items wait through runPreload");
        check(preload_queue.doneCount() == 2, "loads: preload items count as done");
        check(preload_queue.step(4) == 1, "loads: a step runs the streaming item that is left");

        // One broken asset does not hide the others, but the caller still learns that something failed
        LoadQueue failing_preload;
        int second_preload_ran = 0;
        failing_preload.request(LoadPriority::Preload, [] { return false; }, nullptr);
        failing_preload.request(LoadPriority::Preload, nullptr, [&] { ++second_preload_ran; return true; });
        check(!failing_preload.runPreload(), "loads: runPreload reports a failure");
        check(second_preload_ran == 1, "loads: one failed item does not stop the remaining ones");

        // cancel(): a queued item is abandoned before either phase runs
        LoadQueue cancel_queue;
        int cancelled_produced = 0;
        const LoadHandle doomed = cancel_queue.request(LoadPriority::Streaming,
                                                       [&] { ++cancelled_produced; return true; }, nullptr);
        check(cancel_queue.cancel(doomed), "loads: a queued item can be cancelled");
        check(cancel_queue.isIdle(), "loads: a cancelled item stops being pending");
        check(cancel_queue.step(4) == 0, "loads: a cancelled item never runs");
        check(cancelled_produced == 0, "loads: cancelling skips the produce phase as well");
        check(!cancel_queue.cancel(doomed), "loads: cancelling twice reports nothing to cancel");
        check(!cancel_queue.cancel(kNoLoad), "loads: an invalid handle cannot be cancelled");
        check(cancel_queue.doneCount() == 0 && cancel_queue.failedCount() == 0,
              "loads: a cancelled item is neither done nor failed");

        LoadQueue clear_queue;
        clear_queue.request(LoadPriority::Streaming, nullptr, nullptr);
        clear_queue.request(LoadPriority::Preload, nullptr, nullptr);
        clear_queue.clear();
        check(clear_queue.isIdle() && clear_queue.pendingCount() == 0, "loads: clear() drops every item");
        check(clear_queue.doneCount() == 0 && clear_queue.failedCount() == 0,
              "loads: clear() resets the counters");
    }

    // ====================== streaming inside the loop ======================
    // The plan may be registered before init()/start(); nothing runs until the loop actually turns.
    {
        const double tick = 1.0 / 60.0;

        GameLoop loop;
        int streamed = 0;
        for (int i = 0; i < 3; ++i) {
            loop.getLoadQueue().request(LoadPriority::Streaming, [&] { ++streamed; return true; }, nullptr);
        }

        check(loop.getStreamingItemsPerTurn() == 1, "loop: one streaming item per turn by default");
        check((loop.getState() & GameLoopFlags::OVERLOAD) == 0,
              "loop: pending loads keep the resource-ready bit clear");

        loop.init();
        check(loop.getLoadQueue().pendingCount() == 3, "loop: init keeps loads that were registered before it");
        loop.advanceBy(0.0);
        check(streamed == 0, "loop: a loop that never started streams nothing");

        loop.start();
        loop.advanceBy(tick);
        check(streamed == 1, "loop: a turn streams up to the budget");
        check((loop.getState() & GameLoopFlags::OVERLOAD) == 0,
              "loop: the resource-ready bit stays clear while work is left");

        loop.setStreamingItemsPerTurn(2);
        check(loop.getStreamingItemsPerTurn() == 2, "loop: the streaming budget is settable");
        loop.advanceBy(tick);
        check(streamed == 3, "loop: a larger budget finishes the rest in one turn");
        check((loop.getState() & GameLoopFlags::OVERLOAD) != 0,
              "loop: an idle queue sets the resource-ready bit");

        // Loading is not part of the logic clock: a paused loop keeps streaming while ticks are frozen
        loop.pause();
        const std::uint64_t ticks_before = loop.getTickCount();
        loop.getLoadQueue().request(LoadPriority::Streaming, [&] { ++streamed; return true; }, nullptr);
        loop.advanceBy(tick);
        check(streamed == 4, "loop: a paused loop keeps streaming");
        check(loop.getTickCount() == ticks_before, "loop: streaming does not tick the logic clock");

        // exit() drops what is left, and the loop then reports nothing pending
        loop.getLoadQueue().request(LoadPriority::Streaming, nullptr, nullptr);
        loop.exit();
        check(loop.getLoadQueue().isIdle(), "loop: exit drops the pending loads");
        check((loop.getState() & GameLoopFlags::OVERLOAD) != 0,
              "loop: an exited loop reports nothing pending");

        GameLoop idle_loop;
        idle_loop.init();
        check((idle_loop.getState() & GameLoopFlags::OVERLOAD) != 0,
              "loop: an empty queue reads as resources ready");
    }

    // ============================ input mapping ============================
    // The platform layer collects raw input; this mapping is what turns it into intents, so it can
    // be tested without a window.
    {
        FrameInput input;

        check(!InputMapping::viewToggleRequested(input) && !InputMapping::quitRequested(input),
              "input: an empty frame requests nothing");
        const CharacterController::InputState idle = InputMapping::characterInputFromKeys(input);
        check(!idle.forward && !idle.backward && !idle.left && !idle.right && !idle.jump && !idle.sprint,
              "input: an empty frame produces no movement intent");

        input.key_down[keyIndex(Key::W)] = true;
        input.key_down[keyIndex(Key::Up)] = true;
        const CharacterController::InputState walking = InputMapping::characterInputFromKeys(input);
        check(walking.forward, "input: W and the up arrow both mean forward");
        check(!walking.backward && !walking.left && !walking.right,
              "input: forward alone does not produce the other directions");

        input.key_down[keyIndex(Key::W)] = false;
        input.key_down[keyIndex(Key::Up)] = false;
        input.key_down[keyIndex(Key::S)] = true;
        input.key_down[keyIndex(Key::Down)] = true;
        input.key_down[keyIndex(Key::A)] = true;
        input.key_down[keyIndex(Key::Left)] = true;
        input.key_down[keyIndex(Key::D)] = true;
        input.key_down[keyIndex(Key::Right)] = true;
        input.key_down[keyIndex(Key::Space)] = true;
        input.key_down[keyIndex(Key::E)] = true;
        input.key_down[keyIndex(Key::Shift)] = true;
        const CharacterController::InputState every_key = InputMapping::characterInputFromKeys(input);
        check(every_key.backward && every_key.left && every_key.right,
              "input: S/A/D and the arrow keys map to their directions");
        check(every_key.jump, "input: Space (or E) is the jump intent");
        check(every_key.sprint, "input: Shift is the sprint intent");
        check(!every_key.forward, "input: a released key is not reported as held");

        // Toggles are edges, not levels: holding the key must not fire every frame
        input.key_pressed[keyIndex(Key::F5)] = true;
        check(InputMapping::viewToggleRequested(input), "input: F5 requests the view toggle");
        input.key_down[keyIndex(Key::F5)] = true;
        input.key_pressed[keyIndex(Key::F5)] = false;
        check(!InputMapping::viewToggleRequested(input), "input: holding F5 does not toggle again");

        input.key_pressed[keyIndex(Key::R)] = true;
        check(InputMapping::resetRequested(input), "input: R requests a reset");

        input.key_pressed[keyIndex(Key::Escape)] = true;
        check(InputMapping::quitRequested(input), "input: Escape requests a quit");
        input.key_pressed[keyIndex(Key::Escape)] = false;
        check(!InputMapping::quitRequested(input), "input: the quit request is not sticky");
        input.close_requested = true;
        check(InputMapping::quitRequested(input), "input: the window close button also requests a quit");

        // endFrame() drops the per-frame parts but keeps the situation (held keys, cursor position)
        input.mouse_x = 640;
        input.mouse_delta_x = 5;
        input.mouse_delta_y = -3;
        input.wheel_steps = 2.0f;
        input.resized = true;
        input.clearPerFrame();
        check(!InputMapping::resetRequested(input) && !input.close_requested && !input.resized,
              "input: clearPerFrame drops the edges, the close request and the resize flag");
        check(input.mouse_delta_x == 0 && input.mouse_delta_y == 0 && input.wheel_steps == 0.0f,
              "input: clearPerFrame drops the mouse delta and the wheel");
        check(input.isDown(Key::Shift) && input.mouse_x == 640,
              "input: clearPerFrame keeps the held keys and the cursor position");
    }

    std::printf("\n%s (%d failure(s))\n", g_failures == 0 ? "all checks passed" : "FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
