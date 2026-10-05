#ifndef APP_SANDBOX_H
#define APP_SANDBOX_H

#include "core/platform/FrameInput.h"
#include "game/Camera.h"
#include "game/Character.h"
#include "game/GameLoop.h"
#include "game/Model.h"
#include "render/Mesh.h"
#include "render/ShaderProgram.h"
#include "render/gl/GLFunctions.h"

#include <memory>
#include <string>
#include <vector>

// The demo scene - what used to be GLWidget, minus the window.
//
// It owns the GL resources, the model list, the character and the game loop, and it draws exactly
// one frame when asked. Window, input and context now come from the platform and render layers, so
// this class only orchestrates. When the RHI lands (step A5) the two GL-typed members below are what
// gets replaced by an IRenderDevice.
class Sandbox
{
public:
    Sandbox();

    // Build the shader program and the geometry; requires a current GL context.
    // `shaderDirectory` is where basic.vert/basic.frag live (the app passes <exe dir>/shaders).
    bool createResources(GLFunctions &gfx, const std::string &shaderDirectory);
    void releaseResources();

    // Viewport and camera aspect: call once after the context exists and on every resize
    void resize(int width, int height, GLFunctions &gfx);

    // One frame of input: view switch, reset, wheel zoom, drag look. The movement keys are read
    // during the logic tick instead (see updateCharacter).
    void handleInput(const FrameInput &input);

    void render(GLFunctions &gfx);

    GameLoop &getGameLoop() { return game_loop; }

    const Character &getCharacter() const { return character; }
    CameraView getCameraView() const { return camera_view; }
    const std::vector<Model> &getModels() const { return models; }

private:
    void onTick();
    void updateCharacter(float dt);
    void syncCharacterModel();
    void logCameraPositionIfMoved();

    void resetCamera();
    void setCameraView(CameraView view);

    // ---- rendering ----
    ShaderProgram program;
    std::vector<std::shared_ptr<Mesh>> meshes;  // owns the geometry (models share one copy)
    std::vector<Model> models;                  // each element is an independent spinning cube
    Model character_model;                      // placeholder model for the character

    // ---- scene state ----
    Character character;  // the camera is attached to the character; FPV and TPV share it
    CameraView camera_view = CameraView::TPV;
    GameLoop game_loop;
    FrameInput frame_input;  // the input of the current frame, for the logic tick

    // ---- tuning ----
    float orbit_speed = 0.4f;  // degrees per pixel while dragging
    float wheel_step = 0.5f;   // metres per wheel notch (TPV dolly)

    // ---- camera pose log (a line only when position or orientation changed past a threshold) ----
    vec3 last_logged_camera_position;
    vec3 last_logged_camera_forward;
    bool has_logged_camera = false;
    float log_move_threshold = 0.5f;
};

#endif  // APP_SANDBOX_H
