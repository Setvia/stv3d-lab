#include "3d.h"

#include <QOpenGLContext>

#include "core/log/LogManager.h"

// ---------- Construction / destruction ----------
GLWidget::GLWidget(QWidget *parent) : QOpenGLWidget(parent)
{
    // QOpenGLWidget takes no keyboard focus by default; it must be set explicitly or keyPressEvent never arrives
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(false);  // only handle mouse moves while the left button is held and dragging

    // Main loop driving:
    //   ticked       -- fixed step (1/60 s by default) logic update: model spin, character advance, keyboard camera movement
    //   frameStepped -- once per frame: request a repaint only (rendering decoupled from logic)
    connect(&game_loop, &GameLoop::ticked, this, [this](std::uint64_t) { onGameTick(); });
    connect(&game_loop, &GameLoop::frameStepped, this, [this](float) { onGameFrame(); });

    game_loop.init();
    game_loop.start();

    LOG_INFO() << "game loop started: fixed tick = " << game_loop.getFixedTickSeconds()
               << " s, frame interval = " << game_loop.getFrameInterval() << " ms";
    LOG_INFO() << "initial camera view: " << (camera_view == CameraView::FPV ? "FPV" : "TPV")
               << " (press F5 to switch view mode)";
}

GLWidget::~GLWidget()
{
    // GL objects must be destroyed while the context is valid, hence makeCurrent()
    makeCurrent();
    releaseGlResources();
    doneCurrent();
}

// ---------- Camera ----------
void GLWidget::setCamera(const vec3 &eye, const vec3 &target, const vec3 &up)
{
    Camera &camera = character.getCamera();
    camera.setPosition(eye);
    camera.lookAt(target, up);  // the orientation is converted to a quaternion; target/up are not stored
    logCameraPositionIfMoved();
    update();
}

void GLWidget::resetCamera()
{
    character.setPosition(vec3{0.0f, 0.0f, 0.0f});
    character.setCameraOffset(vec3{0.0f, 2.0f, 5.0f});  // reset the TPV orbit offset

    Camera &camera = character.getCamera();
    camera.setOrientation(quat{});  // identity quaternion = looking down -Z, up is +Y
    camera.setPerspective(90.0f, 0.1f, 100.0f);
    character.syncCamera();  // reposition for the current view mode
    syncCharacterModel();

    has_logged_camera = false;  // make the next frame log for sure
    logCameraPositionIfMoved();
    update();
}

// ---------- View-mode switch (FPV / TPV) ----------
void GLWidget::setCameraView(CameraView view)
{
    if (camera_view == view) {
        return;
    }

    camera_view = view;
    character.setView(view);  // the character repositions the camera for the new mode
    clearCameraInput();         // avoids "stuck keys" at the moment of switching

    LOG_INFO() << "camera view: " << (camera_view == CameraView::FPV ? "FPV" : "TPV");
    logCameraPositionIfMoved();
    update();
}

void GLWidget::clearCameraInput()
{
    input = CameraInput{};
}

// ---------- Main loop: fixed-step logic update ----------
void GLWidget::onGameTick()
{
    const float dt = static_cast<float>(game_loop.getFixedTickSeconds());

    // each model advances at its own spin rate (fixed step -> independent of frame rate)
    for (Model &model : models) {
        model.updateSpin(dt);
    }

    // both view modes drive the camera through the character: FPV places it at the eyes, TPV at the orbit offset
    updateCharacter(dt);
}

// ---------- Main loop: per frame, only request a repaint ----------
void GLWidget::onGameFrame()
{
    update();
}

// ---------- Input -> character -> camera (FPV and TPV share this logic) ----------
void GLWidget::updateCharacter(float dt)
{
    if (dt <= 0.0f) {
        return;
    }

    character.getController().setInput(characterInputFromKeys());
    character.update(dt);  // advance the character position and reposition the camera for the current view mode
    syncCharacterModel();
    logCameraPositionIfMoved();
}

// keyboard state -> character controller input
CharacterController::InputState GLWidget::characterInputFromKeys() const
{
    CharacterController::InputState state;
    state.forward = this->input.forward;
    state.backward = this->input.backward;
    state.left = this->input.left;
    state.right = this->input.right;
    state.sprint = this->input.fast;
    state.jump = this->input.up;  // no physics yet; wire up the intent now, implement it once jumping is added
    return state;
}

// character placeholder model: follows the character (the cube center is raised to waist height)
void GLWidget::syncCharacterModel()
{
    if (!character_model.hasMesh()) {
        return;
    }
    character_model.setPosition(character.getPosition() + vec3{0.0f, 0.9f, 0.0f});
}

// Camera pose log: a line is written only when the position or orientation changes past a threshold (an FPV head turn still counts)
void GLWidget::logCameraPositionIfMoved()
{
    const Camera &camera = character.getCamera();
    const vec3 position = camera.getPosition();
    const vec3 forward_dir = camera.forward();

    const bool position_changed = !has_logged_camera
                                  || (position - last_logged_camera_position).length() >= log_move_threshold;
    const bool forward_changed = !has_logged_camera
                                 || forward_dir.dot(last_logged_camera_forward) < 0.999f;
    if (!position_changed && !forward_changed) {
        return;
    }

    has_logged_camera = true;
    last_logged_camera_position = position;
    last_logged_camera_forward = forward_dir;
    LOG_INFO() << "camera[" << (camera_view == CameraView::FPV ? "FPV" : "TPV") << "]: eye("
               << logFixed(position.x, 2) << ", " << logFixed(position.y, 2) << ", "
               << logFixed(position.z, 2) << ") forward(" << logFixed(forward_dir.x, 3) << ", "
               << logFixed(forward_dir.y, 3) << ", " << logFixed(forward_dir.z, 3) << ")";
}

// ---------- Keyboard: drive the camera ----------
void GLWidget::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
        case Qt::Key_W: case Qt::Key_Up:    input.forward = true;  break;
        case Qt::Key_S: case Qt::Key_Down:  input.backward = true; break;
        case Qt::Key_A: case Qt::Key_Left:  input.left = true;     break;
        case Qt::Key_D: case Qt::Key_Right: input.right = true;    break;
        case Qt::Key_Space: case Qt::Key_E: input.up = true;       break;
        case Qt::Key_C: case Qt::Key_Q:     input.down = true;     break;
        case Qt::Key_Shift:                 input.fast = true;     break;
        // View-mode switch. Tab is not used: Qt swallows it in QWidget::event() for focus handling, so keyPressEvent never arrives
        case Qt::Key_F5:                    setCameraView(camera_view == CameraView::FPV
                                                              ? CameraView::TPV
                                                              : CameraView::FPV); break;
        case Qt::Key_R:                     resetCamera();           break;
        case Qt::Key_Escape:                window()->close();       break;
        default:
            QOpenGLWidget::keyPressEvent(event);  // hand unhandled events back to the base class
            return;
    }
    event->accept();
}

void GLWidget::keyReleaseEvent(QKeyEvent *event)
{
    switch (event->key()) {
        case Qt::Key_W: case Qt::Key_Up:    input.forward = false;  break;
        case Qt::Key_S: case Qt::Key_Down:  input.backward = false; break;
        case Qt::Key_A: case Qt::Key_Left:  input.left = false;     break;
        case Qt::Key_D: case Qt::Key_Right: input.right = false;    break;
        case Qt::Key_Space: case Qt::Key_E: input.up = false;       break;
        case Qt::Key_C: case Qt::Key_Q:     input.down = false;     break;
        case Qt::Key_Shift:                 input.fast = false;     break;
        default:
            QOpenGLWidget::keyReleaseEvent(event);
            return;
    }
    event->accept();
}

// ---------- Wheel: TPV dollies the follow distance in/out / FPV zooms the field of view ----------
void GLWidget::wheelEvent(QWheelEvent *event)
{
    const float steps = static_cast<float>(event->angleDelta().y()) / 120.0f;  // one notch = 120
    if (steps == 0.0f) {
        event->accept();
        return;
    }

    Camera &camera = character.getCamera();
    if (camera_view == CameraView::TPV) {
        character.setCameraDistance(character.cameraDistance() - steps * wheel_step);
    } else {
        // FPV: the wheel is used as zoom (20° to 110°)
        camera.setPerspective(camera.getFovYDegrees() - steps * 2.0f,
                              camera.getNearPlane(), camera.getFarPlane());
    }

    logCameraPositionIfMoved();
    update();
    event->accept();
}

// ---------- Left-button drag: FPV free look / TPV orbit around the character ----------
void GLWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        orbiting = true;
        last_mouse_pos = event->position().toPoint();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QOpenGLWidget::mousePressEvent(event);
}

void GLWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!orbiting) {
        QOpenGLWidget::mouseMoveEvent(event);
        return;
    }

    const QPoint current_pos = event->position().toPoint();
    const QPoint delta = current_pos - last_mouse_pos;
    last_mouse_pos = current_pos;

    const float dx = static_cast<float>(delta.x()) * orbit_speed;
    const float dy = static_cast<float>(delta.y()) * orbit_speed;

    if (camera_view == CameraView::FPV) {
        // First person: drag right -> the view turns right (negative yaw); drag down -> look down (negative pitch)
        // Pitch clamping is handled inside Camera::yawPitch (no Euler angles are stored)
        character.getCamera().yawPitch(-dx, -dy);
    } else {
        // Third person: drag right -> the camera swings to the left; drag down -> the camera rises ("grab and drag the character" feel)
        character.orbitCamera(-dx, dy);
    }

    logCameraPositionIfMoved();
    update();
    event->accept();
}

void GLWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && orbiting) {
        orbiting = false;
        unsetCursor();
        event->accept();
        return;
    }
    QOpenGLWidget::mouseReleaseEvent(event);
}

// ---------- Shaders: sources in shaders/basic.vert / basic.frag, embedded into the exe via stv3d-lab.qrc ----------
// Compilation, linking, error logging and uniform location caching are all handled by ShaderProgram (see Shader.h/.cpp)
void GLWidget::createShaderProgram()
{
    const bool ok = program.createFromFiles(
        QStringLiteral(":/shaders/basic.vert"),
        QStringLiteral(":/shaders/basic.frag"),
        // explicit attribute index binding (index constants live in Mesh, so shader and mesh share one numbering)
        {{Mesh::kAttribPos, "aPos"}, {Mesh::kAttribColor, "aColor"}});

    if (!ok) {
        LOG_ERROR() << "shader program creation failed; models cannot be drawn";
    }
}

// ---------- Build the scene: one mesh + several independent models ----------
void GLWidget::createScene()
{
    // (1) Geometry: the cube is uploaded once and placed into the mesh library
    std::vector<Vertex> vertices;
    std::vector<GLuint> indices;
    MeshFactory::makeCube(vertices, indices, 1.0f);

    auto cube_mesh = std::make_shared<Mesh>();
    cube_mesh->create(vertices, indices);
    if (!cube_mesh->isValid()) {
        LOG_ERROR() << "cube mesh creation failed";
        return;
    }
    meshes.push_back(cube_mesh);

    // (2) Models: three independent models share the same geometry but differ in position, scale and spin
    struct ModelSpec
    {
        vec3 position;
        float scale;
        float spin_degrees_per_second;
        vec3 spin_axis;
    };

    const ModelSpec specs[] = {
        {vec3{-4.0f, 0.0f, -2.0f}, 1.0f, 30.0f, vec3{0.0f, 1.0f, 0.0f}},
        {vec3{4.0f, 0.0f, -2.0f}, 1.4f, 60.0f, vec3{1.0f, 0.0f, 0.0f}},
        {vec3{0.0f, 0.0f, -7.0f}, 0.7f, 90.0f, vec3{1.0f, 1.0f, 0.0f}},
    };

    for (const ModelSpec &spec : specs) {
        Model model(cube_mesh);  // * shares the same mesh
        model.setPosition(spec.position);
        model.setUniformScale(spec.scale);
        model.setSpin(spec.spin_degrees_per_second, spec.spin_axis);
        models.push_back(std::move(model));
    }

    // (3) Character placeholder model: reuses the same cube geometry, scaled down into a "humanoid placeholder"; follows the character
    character_model = Model(cube_mesh);
    character_model.setUniformScale(0.6f);
    character_model.setSpin(0.0f);  // the character does not spin
    syncCharacterModel();

    LOG_INFO() << "scene: " << meshes.size() << " mesh(es), " << (models.size() + 1) << " model(s)";
}

// ---------- Explicitly release all GL resources (requires a current context) ----------
void GLWidget::releaseGlResources()
{
    // mesh objects call glDelete* in their destructor, so clearing the containers is enough (shared_ptr refcount reaches zero)
    models.clear();
    meshes.clear();

    program.destroy();
}

// ---------- Initialization ----------
void GLWidget::initializeGL()
{
    if (!initializeOpenGLFunctions()) {
        LOG_ERROR() << "failed to load the OpenGL 4.3 Core functions (context version too low; check the QSurfaceFormat in main.cpp)";
        return;
    }

    const QSurfaceFormat fmt = context()->format();
    LOG_INFO() << "GL context: " << fmt.majorVersion() << "." << fmt.minorVersion()
               << " | core profile: " << (fmt.profile() == QSurfaceFormat::CoreProfile)
               << " | GL_VERSION: " << reinterpret_cast<const char *>(glGetString(GL_VERSION))
               << " | GPU: " << reinterpret_cast<const char *>(glGetString(GL_RENDERER));

    glClearColor(0.1f, 0.12f, 0.15f, 0.1f);
    glEnable(GL_DEPTH_TEST);
    // glEnable(GL_MULTISAMPLE);   // kept disabled for your change

    createShaderProgram();
    createScene();

    // explicitly check for a GL error once during initialization
    const GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        LOG_WARNING() << "GL error after initialization, code = " << logHex(err, 4);
    }
}

// ---------- Resize ----------
void GLWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
    // only the new aspect ratio is handed to the camera; fov / near / far are held by the camera itself
    character.getCamera().setViewportAspect(h > 0 ? float(w) / float(h) : 1.0f);
}

// ---------- Drawing: walk the model list, one draw call per model ----------
void GLWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const Camera &camera = character.getCamera();
    const mat4 view_projection = camera.projectionMatrix() * camera.viewMatrix();

    program.bind();  // all models share the same shader program

    // scene models (the spinning cubes)
    for (const Model &model : models) {
        Mesh *mesh = model.getMesh();
        if (mesh == nullptr || !mesh->isValid()) {
            continue;
        }

        // each model has its own model matrix -> the uniform is rewritten per model (the location is cached by ShaderProgram)
        program.setMat4("uMvp", view_projection * model.modelMatrix());
        mesh->draw();
    }

    // character placeholder model
    if (Mesh *mesh = character_model.getMesh(); mesh != nullptr && mesh->isValid()) {
        program.setMat4("uMvp", view_projection * character_model.modelMatrix());
        mesh->draw();
    }

    program.release();
}
