#include "3d.h"

#include <QDebug>
#include <QOpenGLContext>

// ---------- Construction / destruction ----------
MyGLWidget::MyGLWidget(QWidget *parent) : QOpenGLWidget(parent)
{
    // QOpenGLWidget takes no keyboard focus by default; it must be set explicitly or keyPressEvent never arrives
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(false);  // only handle mouse moves while the left button is held and dragging

    // Main loop driving:
    //   ticked       -- fixed step (1/60 s by default) logic update: model spin, character advance, keyboard camera movement
    //   frameStepped -- once per frame: request a repaint only (rendering decoupled from logic)
    connect(&m_game_loop, &GameLoop::ticked, this, [this](std::uint64_t) { onGameTick(); });
    connect(&m_game_loop, &GameLoop::frameStepped, this, [this](float) { onGameFrame(); });

    m_game_loop.init();
    m_game_loop.start();

    qInfo().noquote() << "game loop started: fixed tick =" << m_game_loop.fixedTickSeconds()
                      << "s, frame interval =" << m_game_loop.frameInterval() << "ms";
    qInfo().noquote() << "initial camera view:"
                      << (m_camera_view == CameraView::FPV ? "FPV" : "TPV")
                      << "(press F5 to switch view mode)";
}

MyGLWidget::~MyGLWidget()
{
    // GL objects must be destroyed while the context is valid, hence makeCurrent()
    makeCurrent();
    releaseGlResources();
    doneCurrent();
}

// ---------- Camera ----------
void MyGLWidget::setCamera(const vec3 &eye, const vec3 &target, const vec3 &up)
{
    MyCamera &camera = m_character.camera();
    camera.setPosition(eye);
    camera.lookAt(target, up);  // the orientation is converted to a quaternion; target/up are not stored
    logCameraPositionIfMoved();
    update();
}

void MyGLWidget::resetCamera()
{
    m_character.setPosition(vec3{0.0f, 0.0f, 0.0f});
    m_character.setCameraOffset(vec3{0.0f, 2.0f, 5.0f});  // reset the TPV orbit offset

    MyCamera &camera = m_character.camera();
    camera.setOrientation(quat{});  // identity quaternion = looking down -Z, up is +Y
    camera.setPerspective(90.0f, 0.1f, 100.0f);
    m_character.syncCamera();  // reposition for the current view mode
    syncCharacterModel();

    m_has_logged_camera = false;  // make the next frame log for sure
    logCameraPositionIfMoved();
    update();
}

// ---------- View-mode switch (FPV / TPV) ----------
void MyGLWidget::setCameraView(CameraView view)
{
    if (m_camera_view == view) {
        return;
    }

    m_camera_view = view;
    m_character.setView(view);  // the character repositions the camera for the new mode
    clearCameraInput();         // avoids "stuck keys" at the moment of switching

    qInfo().noquote() << "camera view:" << (m_camera_view == CameraView::FPV ? "FPV" : "TPV");
    logCameraPositionIfMoved();
    update();
}

void MyGLWidget::clearCameraInput()
{
    m_input = CameraInput{};
}

// ---------- Main loop: fixed-step logic update ----------
void MyGLWidget::onGameTick()
{
    const float dt = static_cast<float>(m_game_loop.fixedTickSeconds());

    // each model advances at its own spin rate (fixed step -> independent of frame rate)
    for (MyModel &model : m_models) {
        model.updateSpin(dt);
    }

    // both view modes drive the camera through the character: FPV places it at the eyes, TPV at the orbit offset
    updateCharacter(dt);
}

// ---------- Main loop: per frame, only request a repaint ----------
void MyGLWidget::onGameFrame()
{
    update();
}

// ---------- Input -> character -> camera (FPV and TPV share this logic) ----------
void MyGLWidget::updateCharacter(float dt)
{
    if (dt <= 0.0f) {
        return;
    }

    m_character.controller().setInput(characterInputFromKeys());
    m_character.update(dt);  // advance the character position and reposition the camera for the current view mode
    syncCharacterModel();
    logCameraPositionIfMoved();
}

// keyboard state -> character controller input
MyCharacterController::InputState MyGLWidget::characterInputFromKeys() const
{
    MyCharacterController::InputState input;
    input.forward = m_input.forward;
    input.backward = m_input.backward;
    input.left = m_input.left;
    input.right = m_input.right;
    input.sprint = m_input.fast;
    input.jump = m_input.up;  // no physics yet; wire up the intent now, implement it once jumping is added
    return input;
}

// character placeholder model: follows the character (the cube center is raised to waist height)
void MyGLWidget::syncCharacterModel()
{
    if (!m_character_model.hasMesh()) {
        return;
    }
    m_character_model.setPosition(m_character.position() + vec3{0.0f, 0.9f, 0.0f});
}

// Camera pose log: a line is written only when the position or orientation changes past a threshold (an FPV head turn still counts)
void MyGLWidget::logCameraPositionIfMoved()
{
    const MyCamera &camera = m_character.camera();
    const vec3 position = camera.position();
    const vec3 forward_dir = camera.forward();

    const bool position_changed = !m_has_logged_camera
                                  || (position - m_last_logged_camera_position).length() >= m_log_move_threshold;
    const bool forward_changed = !m_has_logged_camera
                                 || forward_dir.dot(m_last_logged_camera_forward) < 0.999f;
    if (!position_changed && !forward_changed) {
        return;
    }

    m_has_logged_camera = true;
    m_last_logged_camera_position = position;
    m_last_logged_camera_forward = forward_dir;
    qInfo().noquote() << QStringLiteral("camera[%1]: eye(%2, %3, %4) forward(%5, %6, %7)")
                             .arg(m_camera_view == CameraView::FPV ? QStringLiteral("FPV") : QStringLiteral("TPV"))
                             .arg(position.x, 0, 'f', 2)
                             .arg(position.y, 0, 'f', 2)
                             .arg(position.z, 0, 'f', 2)
                             .arg(forward_dir.x, 0, 'f', 3)
                             .arg(forward_dir.y, 0, 'f', 3)
                             .arg(forward_dir.z, 0, 'f', 3);
}

// ---------- Keyboard: drive the camera ----------
void MyGLWidget::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
        case Qt::Key_W: case Qt::Key_Up:    m_input.forward = true;  break;
        case Qt::Key_S: case Qt::Key_Down:  m_input.backward = true; break;
        case Qt::Key_A: case Qt::Key_Left:  m_input.left = true;     break;
        case Qt::Key_D: case Qt::Key_Right: m_input.right = true;    break;
        case Qt::Key_Space: case Qt::Key_E: m_input.up = true;       break;
        case Qt::Key_C: case Qt::Key_Q:     m_input.down = true;     break;
        case Qt::Key_Shift:                 m_input.fast = true;     break;
        // View-mode switch. Tab is not used: Qt swallows it in QWidget::event() for focus handling, so keyPressEvent never arrives
        case Qt::Key_F5:                    setCameraView(m_camera_view == CameraView::FPV
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

void MyGLWidget::keyReleaseEvent(QKeyEvent *event)
{
    switch (event->key()) {
        case Qt::Key_W: case Qt::Key_Up:    m_input.forward = false;  break;
        case Qt::Key_S: case Qt::Key_Down:  m_input.backward = false; break;
        case Qt::Key_A: case Qt::Key_Left:  m_input.left = false;     break;
        case Qt::Key_D: case Qt::Key_Right: m_input.right = false;    break;
        case Qt::Key_Space: case Qt::Key_E: m_input.up = false;       break;
        case Qt::Key_C: case Qt::Key_Q:     m_input.down = false;     break;
        case Qt::Key_Shift:                 m_input.fast = false;     break;
        default:
            QOpenGLWidget::keyReleaseEvent(event);
            return;
    }
    event->accept();
}

// ---------- Wheel: TPV dollies the follow distance in/out / FPV zooms the field of view ----------
void MyGLWidget::wheelEvent(QWheelEvent *event)
{
    const float steps = static_cast<float>(event->angleDelta().y()) / 120.0f;  // one notch = 120
    if (steps == 0.0f) {
        event->accept();
        return;
    }

    MyCamera &camera = m_character.camera();
    if (m_camera_view == CameraView::TPV) {
        m_character.setCameraDistance(m_character.cameraDistance() - steps * m_wheel_step);
    } else {
        // FPV: the wheel is used as zoom (20° to 110°)
        camera.setPerspective(camera.fovYDegrees() - steps * 2.0f,
                              camera.nearPlane(), camera.farPlane());
    }

    logCameraPositionIfMoved();
    update();
    event->accept();
}

// ---------- Left-button drag: FPV free look / TPV orbit around the character ----------
void MyGLWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_orbiting = true;
        m_last_mouse_pos = event->position().toPoint();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QOpenGLWidget::mousePressEvent(event);
}

void MyGLWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_orbiting) {
        QOpenGLWidget::mouseMoveEvent(event);
        return;
    }

    const QPoint current_pos = event->position().toPoint();
    const QPoint delta = current_pos - m_last_mouse_pos;
    m_last_mouse_pos = current_pos;

    const float dx = static_cast<float>(delta.x()) * m_orbit_speed;
    const float dy = static_cast<float>(delta.y()) * m_orbit_speed;

    if (m_camera_view == CameraView::FPV) {
        // First person: drag right -> the view turns right (negative yaw); drag down -> look down (negative pitch)
        // Pitch clamping is handled inside MyCamera::yawPitch (no Euler angles are stored)
        m_character.camera().yawPitch(-dx, -dy);
    } else {
        // Third person: drag right -> the camera swings to the left; drag down -> the camera rises ("grab and drag the character" feel)
        m_character.orbitCamera(-dx, dy);
    }

    logCameraPositionIfMoved();
    update();
    event->accept();
}

void MyGLWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_orbiting) {
        m_orbiting = false;
        unsetCursor();
        event->accept();
        return;
    }
    QOpenGLWidget::mouseReleaseEvent(event);
}

// ---------- Shaders: sources in shaders/basic.vert / basic.frag, embedded into the exe via stv3d-lab.qrc ----------
// Compilation, linking, error logging and uniform location caching are all handled by MyShaderProgram (see myShader.h/.cpp)
void MyGLWidget::createShaderProgram()
{
    const bool ok = m_program.createFromFiles(
        QStringLiteral(":/shaders/basic.vert"),
        QStringLiteral(":/shaders/basic.frag"),
        // explicit attribute index binding (index constants live in MyMesh, so shader and mesh share one numbering)
        {{MyMesh::kAttribPos, "aPos"}, {MyMesh::kAttribColor, "aColor"}});

    if (!ok) {
        qCritical("shader program creation failed; models cannot be drawn");
    }
}

// ---------- Build the scene: one mesh + several independent models ----------
void MyGLWidget::createScene()
{
    // (1) Geometry: the cube is uploaded once and placed into the mesh library
    std::vector<MyVertex> vertices;
    std::vector<GLuint> indices;
    MyMeshFactory::makeCube(vertices, indices, 1.0f);

    auto cube_mesh = std::make_shared<MyMesh>();
    cube_mesh->create(vertices, indices);
    if (!cube_mesh->isValid()) {
        qCritical("cube mesh creation failed");
        return;
    }
    m_meshes.push_back(cube_mesh);

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
        MyModel model(cube_mesh);  // * shares the same mesh
        model.setPosition(spec.position);
        model.setUniformScale(spec.scale);
        model.setSpin(spec.spin_degrees_per_second, spec.spin_axis);
        m_models.push_back(std::move(model));
    }

    // (3) Character placeholder model: reuses the same cube geometry, scaled down into a "humanoid placeholder"; follows the character
    m_character_model = MyModel(cube_mesh);
    m_character_model.setUniformScale(0.6f);
    m_character_model.setSpin(0.0f);  // the character does not spin
    syncCharacterModel();

    qInfo().noquote() << "scene:" << m_meshes.size() << "mesh(es),"
                      << (m_models.size() + 1) << "model(s)";
}

// ---------- Explicitly release all GL resources (requires a current context) ----------
void MyGLWidget::releaseGlResources()
{
    // mesh objects call glDelete* in their destructor, so clearing the containers is enough (shared_ptr refcount reaches zero)
    m_models.clear();
    m_meshes.clear();

    m_program.destroy();
}

// ---------- Initialization ----------
void MyGLWidget::initializeGL()
{
    if (!initializeOpenGLFunctions()) {
        qCritical("failed to load the OpenGL 4.3 Core functions (context version too low; check the QSurfaceFormat in main.cpp)");
        return;
    }

    const QSurfaceFormat fmt = context()->format();
    qInfo().noquote() << "GL context:" << fmt.majorVersion() << "." << fmt.minorVersion()
                      << "| core profile:" << (fmt.profile() == QSurfaceFormat::CoreProfile)
                      << "| GL_VERSION:" << reinterpret_cast<const char *>(glGetString(GL_VERSION))
                      << "| GPU:" << reinterpret_cast<const char *>(glGetString(GL_RENDERER));

    glClearColor(0.1f, 0.12f, 0.15f, 0.1f);
    glEnable(GL_DEPTH_TEST);
    // glEnable(GL_MULTISAMPLE);   // kept disabled for your change

    createShaderProgram();
    createScene();

    // explicitly check for a GL error once during initialization
    const GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        qWarning() << "GL error after initialization, code = 0x" << Qt::hex << err;
    }
}

// ---------- Resize ----------
void MyGLWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
    // only the new aspect ratio is handed to the camera; fov / near / far are held by the camera itself
    m_character.camera().setViewportAspect(h > 0 ? float(w) / float(h) : 1.0f);
}

// ---------- Drawing: walk the model list, one draw call per model ----------
void MyGLWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const MyCamera &camera = m_character.camera();
    const mat4 view_projection = camera.projectionMatrix() * camera.viewMatrix();

    m_program.bind();  // all models share the same shader program

    // scene models (the spinning cubes)
    for (const MyModel &model : m_models) {
        MyMesh *mesh = model.mesh();
        if (mesh == nullptr || !mesh->isValid()) {
            continue;
        }

        // each model has its own model matrix -> the uniform is rewritten per model (the location is cached by MyShaderProgram)
        m_program.setMat4("uMvp", view_projection * model.modelMatrix());
        mesh->draw();
    }

    // character placeholder model
    if (MyMesh *mesh = m_character_model.mesh(); mesh != nullptr && mesh->isValid()) {
        m_program.setMat4("uMvp", view_projection * m_character_model.modelMatrix());
        mesh->draw();
    }

    m_program.release();
}
