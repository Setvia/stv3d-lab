#ifndef GAME_3D_H
#define GAME_3D_H

#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLFunctions_4_3_Core>
#include <QOpenGLWidget>
#include <QPoint>
#include <QWheelEvent>

#include "core/math/vec3.h"

#include <cstdint>
#include <memory>
#include <vector>

// Renders with raw OpenGL 4.3 (no Qt GL wrapper classes) and keeps the "where does the
// vertex data come from" question explicit:
//
//   glVertexAttribFormat(attrib, components, type, normalized, byteOffset)  <- describe the format
//   glVertexAttribBinding(attrib, bindingIndex)                             <- attrib -> binding slot
//   glBindVertexBuffer(bindingIndex, vbo, offset, stride)                   <- binding slot -> buffer
//
// Responsibilities:
//   Mesh          - geometry + its GL resources (shareable between models)
//   Model         - a mesh reference + an independent transform
//   Camera        - view + projection matrices
//   GLWidget      - input -> camera; walks the model list once per frame (one draw call per model)
//
// Requires an OpenGL 4.3 context (see the QSurfaceFormat setup in main.cpp).

#include "GameLoop.h"
#include "Camera.h"
#include "Character.h"
#include "Model.h"
#include "../render/Mesh.h"
#include "../render/ShaderProgram.h"

class GLWidget : public QOpenGLWidget, protected QOpenGLFunctions_4_3_Core
{
    Q_OBJECT

public:
    explicit GLWidget(QWidget *parent = nullptr);
    ~GLWidget() override;

    // ---- View-mode switch ----
    // FPV: first person - the camera sits at the character's eyes, the mouse looks around freely (quaternion yaw/pitch)
    // TPV: third person - the camera orbits the character (Character::orbitCamera)
    // CameraView is defined in Camera.h; this alias also makes spellings like GLWidget::CameraView::FPV valid
    using CameraView = ::CameraView;

    CameraView getCameraView() const { return camera_view; }
    void setCameraView(CameraView view);

    // ---- Main loop (external tasks can be attached: getGameLoop().enqueue(...) / getScheduler()) ----
    GameLoop &getGameLoop() { return game_loop; }

    // ---- Character (the camera is attached to the character; both view modes share the same one) ----
    Character &getCharacter() { return character; }
    const Character &getCharacter() const { return character; }

    // ---- Camera ----
    void setCamera(const vec3 &eye, const vec3 &target,
                   const vec3 &up = vec3{0.0f, 1.0f, 0.0f});
    void resetCamera();  // back to the default camera pose

    Camera &getCamera() { return character.getCamera(); }
    const Camera &getCamera() const { return character.getCamera(); }

    // ---- Model list (read-only access, convenient for external inspection/tests) ----
    const std::vector<Model> &getModels() const { return models; }
    std::size_t meshCount() const { return meshes.size(); }

    // ---- Input state (filled by events, but can also be set from code for tests/scripted driving) ----
    struct CameraInput
    {
        bool forward = false;   // W / ↑
        bool backward = false;  // S / ↓
        bool left = false;      // A / ←
        bool right = false;     // D / →
        bool up = false;        // Space / E
        bool down = false;      // C / Q
        bool fast = false;      // Shift (sprint)
    };

    void setCameraInput(const CameraInput &input) { this->input = input; }
    const CameraInput &getCameraInput() const { return input; }

    void setMoveSpeed(float metersPerSecond) { move_speed = metersPerSecond; }
    float getMoveSpeed() const { return move_speed; }

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    // Input events
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    // ---- Shader program (sources in shaders/*.vert|frag, embedded into the exe via stv3d-lab.qrc; one shared by all models) ----
    ShaderProgram program;

    // ---- Scene content: mesh library + model list ----
    std::vector<std::shared_ptr<Mesh>> meshes;  // owns the geometry (shared_ptr lets models share one copy)
    std::vector<Model> models;                  // each element is an independent model (a spinning cube)
    Model character_model;                      // placeholder model for the character (follows the character position)

    Character character;  // character (the camera is attached to it; shared by FPV/TPV)
    CameraView camera_view = CameraView::TPV;

    GameLoop game_loop{this};  // main loop: fixed-step logic ticks + per-frame rendering, replacing the internal QTimer

    // ---- Input state and parameters ----
    CameraInput input;
    float move_speed = 5.0f;       // meters/second
    float fast_multiplier = 3.0f;  // multiplier while Shift is held
    float orbit_speed = 0.4f;      // degrees/pixel (mouse drag turns the view)
    float wheel_step = 0.5f;       // meters/step (wheel dolly)
    QPoint last_mouse_pos;
    bool orbiting = false;

    // Camera state change log (a line is written only when position or orientation changes past a threshold, to avoid spamming)
    vec3 last_logged_camera_position;
    vec3 last_logged_camera_forward;
    bool has_logged_camera = false;
    float log_move_threshold = 0.5f;

    // ---- Explicitly separated initialization / cleanup steps ----
    void createShaderProgram();        // load and link the shaders from resources (shaders/basic.vert|frag)
    void createScene();                // build geometry (meshes) + place models
    void releaseGlResources();         // release the program and all meshes (requires a current context)

    // ---- Main loop callbacks ----
    void onGameTick();          // fixed step: logic update (model spin, character advance, keyboard camera movement)
    void onGameFrame();         // every frame: request a repaint

    // ---- Per frame ----
    void updateCharacter(float dt);         // input -> character -> camera (shared by both view modes)
    CharacterController::InputState characterInputFromKeys() const;
    void syncCharacterModel();              // the placeholder model follows the character position
    void logCameraPositionIfMoved();
    void clearCameraInput();
};

#endif  // GAME_3D_H
