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
//   MyMesh          - geometry + its GL resources (shareable between models)
//   MyModel         - a mesh reference + an independent transform
//   MyCamera        - view + projection matrices
//   MyGLWidget      - input -> camera; walks the model list once per frame (one draw call per model)
//
// Requires an OpenGL 4.3 context (see the QSurfaceFormat setup in main.cpp).

#include "GameLoop.h"
#include "Camera.h"
#include "Character.h"
#include "Model.h"
#include "../render/Mesh.h"
#include "../render/ShaderProgram.h"

class MyGLWidget : public QOpenGLWidget, protected QOpenGLFunctions_4_3_Core
{
    Q_OBJECT

public:
    explicit MyGLWidget(QWidget *parent = nullptr);
    ~MyGLWidget() override;

    // ---- View-mode switch ----
    // FPV: first person - the camera sits at the character's eyes, the mouse looks around freely (quaternion yaw/pitch)
    // TPV: third person - the camera orbits the character (MyCharacter::orbitCamera)
    // CameraView is defined in myCamera.h; this alias also makes spellings like MyGLWidget::CameraView::FPV valid
    using CameraView = ::CameraView;

    CameraView cameraView() const { return m_camera_view; }
    void setCameraView(CameraView view);

    // ---- Main loop (external tasks can be attached: gameLoop().enqueue(...) / scheduler()) ----
    GameLoop &gameLoop() { return m_game_loop; }

    // ---- Character (the camera is attached to the character; both view modes share the same one) ----
    MyCharacter &character() { return m_character; }
    const MyCharacter &character() const { return m_character; }

    // ---- Camera ----
    void setCamera(const vec3 &eye, const vec3 &target,
                   const vec3 &up = vec3{0.0f, 1.0f, 0.0f});
    void resetCamera();  // back to the default camera pose

    MyCamera &camera() { return m_character.camera(); }
    const MyCamera &camera() const { return m_character.camera(); }

    // ---- Model list (read-only access, convenient for external inspection/tests) ----
    const std::vector<MyModel> &models() const { return m_models; }
    std::size_t meshCount() const { return m_meshes.size(); }

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

    void setCameraInput(const CameraInput &input) { m_input = input; }
    const CameraInput &cameraInput() const { return m_input; }

    void setMoveSpeed(float metersPerSecond) { m_move_speed = metersPerSecond; }
    float moveSpeed() const { return m_move_speed; }

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
    MyShaderProgram m_program;

    // ---- Scene content: mesh library + model list ----
    std::vector<std::shared_ptr<MyMesh>> m_meshes;  // owns the geometry (shared_ptr lets models share one copy)
    std::vector<MyModel> m_models;                  // each element is an independent model (a spinning cube)
    MyModel m_character_model;                      // placeholder model for the character (follows the character position)

    MyCharacter m_character;  // character (the camera is attached to it; shared by FPV/TPV)
    CameraView m_camera_view = CameraView::TPV;

    GameLoop m_game_loop{this};  // main loop: fixed-step logic ticks + per-frame rendering, replacing the internal QTimer

    // ---- Input state and parameters ----
    CameraInput m_input;
    float m_move_speed = 5.0f;       // meters/second
    float m_fast_multiplier = 3.0f;  // multiplier while Shift is held
    float m_orbit_speed = 0.4f;      // degrees/pixel (mouse drag turns the view)
    float m_wheel_step = 0.5f;       // meters/step (wheel dolly)
    QPoint m_last_mouse_pos;
    bool m_orbiting = false;

    // Camera state change log (a line is written only when position or orientation changes past a threshold, to avoid spamming)
    vec3 m_last_logged_camera_position;
    vec3 m_last_logged_camera_forward;
    bool m_has_logged_camera = false;
    float m_log_move_threshold = 0.5f;

    // ---- Explicitly separated initialization / cleanup steps ----
    void createShaderProgram();        // load and link the shaders from resources (shaders/basic.vert|frag)
    void createScene();                // build geometry (meshes) + place models
    void releaseGlResources();         // release the program and all meshes (requires a current context)

    // ---- Main loop callbacks ----
    void onGameTick();          // fixed step: logic update (model spin, character advance, keyboard camera movement)
    void onGameFrame();         // every frame: request a repaint

    // ---- Per frame ----
    void updateCharacter(float dt);         // input -> character -> camera (shared by both view modes)
    MyCharacterController::InputState characterInputFromKeys() const;
    void syncCharacterModel();              // the placeholder model follows the character position
    void logCameraPositionIfMoved();
    void clearCameraInput();
};

#endif  // MY3D_H
