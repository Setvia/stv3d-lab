cmake_minimum_required(VERSION 3.21)
project(stv3d-lab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Layered targets. The dependency direction is strictly downward:
#
#   stv3d_core          math + CPU geometry. Links NOTHING (no Qt, no OpenGL).
#                       Adding a Qt/GL include to a core header fails to compile -
#                       the layering is enforced by the build, not by review.
#   stv3d_engine        scene-side logic: camera, model instances, character. Links only
#                       stv3d_core - no Qt, no OpenGL, no window.
#   stv3d_render_gl     OpenGL backend (still uses Qt's QOpenGLFunctions wrapper).
#   stv3d-lab           the executable: Qt window, input events, scene assembly.
#   stv3d_core_tests    unit tests for core   (no Qt, no GPU)
#   stv3d_engine_tests  unit tests for engine (no Qt, no GPU)
#
# Future steps: stv3d_render_vk (Vulkan backend), stv3d_platform_qt (window/input/timer
# implementations). Both plug in below stv3d-lab without touching core or engine.
# ---------------------------------------------------------------------------

# ---- stv3d_core: zero third-party dependencies -----------------------------
add_library(stv3d_core STATIC
    src/core/core_smoke.cpp
    src/core/log/LogManager.cpp
    src/core/log/LogManager.h
    src/core/math/conventions.h
    src/core/math/vec2.h
    src/core/math/vec3.h
    src/core/math/vec4.h
    src/core/math/mat3.h
    src/core/math/mat4.h
    src/core/math/quat.h
    src/core/geometry/Vertex.h
    src/core/geometry/Triangle.h
    src/core/geometry/MeshData.h
)
# Logging uses a small atomic spin lock rather than std::mutex, so no pthread symbol is referenced.
# Reason: the posix thread model makes g++ pass an implicit -lpthread, which on MinGW resolves to the
# STATIC libpthread.a, and vcpkg's libcpr.dll.a re-exports the same winpthread symbols - linking both
# fails with "multiple definition of pthread_mutex_lock". That is why there is no find_package(Threads)
# and no -lpthread anywhere here (adding either one brings the conflict straight back).
target_include_directories(stv3d_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
# NOTE: deliberately NO Qt and NO OpenGL here - only the C++ standard library.

# ---- third-party dependencies of the Qt/GL side ---------------------------
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)
find_package(cpr CONFIG REQUIRED)            # HTTP client (libcurl wrapper)
find_package(nlohmann_json CONFIG REQUIRED)  # JSON parsing (header-only, unused so far)

# ---- stv3d_render_gl: OpenGL backend --------------------------------------
add_library(stv3d_render_gl STATIC
    src/render/Mesh.cpp
    src/render/Mesh.h
    src/render/ShaderProgram.cpp
    src/render/ShaderProgram.h
)
target_link_libraries(stv3d_render_gl
    PUBLIC stv3d_core
    PRIVATE Qt6::Core Qt6::Gui Qt6::OpenGL
)

# ---- stv3d_engine: scene logic, still Qt-free -----------------------------
# Camera, model instances and the character controller were converted from Qt math types
# (QVector3D/QQuaternion/QMatrix4x4) to core types (vec3/quat/mat4). They no longer need a
# window, a GPU or Qt at all, which is what makes them testable in a plain executable.
add_library(stv3d_engine STATIC
    src/game/Camera.h
    src/game/Model.h
    src/game/Character.cpp
    src/game/Character.h
    src/game/GameLoop.cpp
    src/game/GameLoop.h
)
target_link_libraries(stv3d_engine PUBLIC stv3d_core)

# ---- stv3d_app: the executable (Qt lives here and above) ------------------
# Header files are listed so AUTOMOC can see the last Q_OBJECT header (3d.h).
# stv3d-lab.qrc embeds the shaders into the executable (AUTORCC).
# LogManager moved into stv3d_core and GameLoop into stv3d_engine: both are Qt-free services now.
add_executable(stv3d-lab
    src/main.cpp
    src/game/3d.cpp
    src/game/3d.h
    stv3d-lab.qrc
)
set_target_properties(stv3d-lab PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
)
target_link_libraries(stv3d-lab PRIVATE
    stv3d_core
    stv3d_engine
    stv3d_render_gl
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    cpr::cpr
    nlohmann_json::nlohmann_json
)

# ---- unit tests: no Qt, no GPU -------------------------------------------
enable_testing()
add_executable(stv3d_core_tests tests/core_math_test.cpp)
target_link_libraries(stv3d_core_tests PRIVATE stv3d_core)
add_test(NAME core_math COMMAND stv3d_core_tests)

add_executable(stv3d_engine_tests tests/engine_test.cpp)
target_link_libraries(stv3d_engine_tests PRIVATE stv3d_engine)
add_test(NAME engine_scene COMMAND stv3d_engine_tests)

# The logging test also proves that core/log needs nothing but the standard library
add_executable(stv3d_core_log_tests tests/core_log_test.cpp)
target_link_libraries(stv3d_core_log_tests PRIVATE stv3d_core)
add_test(NAME core_log COMMAND stv3d_core_log_tests)

# ---- runtime deployment (Windows) ---------------------------------------
# Copy the vcpkg runtime DLLs next to the executable so it can be launched directly.
if(WIN32 AND DEFINED VCPKG_INSTALLED_DIR)
    file(GLOB _VCPKG_RUNTIME_DLLS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/*.dll")
    if(_VCPKG_RUNTIME_DLLS)
        add_custom_command(TARGET stv3d-lab POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_VCPKG_RUNTIME_DLLS} $<TARGET_FILE_DIR:stv3d-lab>
            COMMENT "Copying vcpkg runtime DLLs next to stv3d-lab.exe")
    endif()
endif()

# Deploy the Qt runtime (Qt6*.dll, platforms/qwindows.dll, MinGW runtime) so that the
# executable runs by double-click without a manual PATH setup.
if(WIN32)
    get_filename_component(_QT_BIN_DIR "${Qt6_DIR}/../../../bin" ABSOLUTE)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_QT_BIN_DIR}")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET stv3d-lab POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations "$<TARGET_FILE:stv3d-lab>"
            COMMENT "Deploying Qt runtime next to stv3d-lab.exe")
    endif()
endif()
