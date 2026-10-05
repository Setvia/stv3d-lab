cmake_minimum_required(VERSION 3.21)
project(stv3d-lab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Layered targets. The dependency direction is strictly downward and there is
# no Qt anywhere any more - the window is Win32 and the GL context is WGL:
#
#   stv3d_core            math, CPU geometry and the platform-independent value
#                         types (Key, FrameInput, NativeWindowHandle).
#                         Links NOTHING - not even the OS: a Qt/GL/OS include in
#                         a core header fails to compile, so the layering is
#                         enforced by the build, not by review.
#   stv3d_engine          scene logic: camera, model instances, character, game
#                         loop, input mapping. Links only stv3d_core.
#   stv3d_platform_win32  the window: class registration, message pump, input.
#   stv3d_render_gl       OpenGL 4.3 backend: WGL context, hand-written function
#                         table, mesh, shader program.
#   stv3d-lab             the executable: assembly root (window + context + scene).
#   stv3d_*_tests         unit tests for core/engine (no window, no GPU needed).
#
# Future steps: stv3d_render_vk / stv3d_render_d3d11 (same shape as
# stv3d_render_gl), and an IRenderDevice that all three implement.
# ---------------------------------------------------------------------------

# ---- stv3d_core: zero third-party dependencies -----------------------------
add_library(stv3d_core STATIC
    src/core/core_smoke.cpp
    src/core/log/LogManager.cpp
    src/core/log/LogManager.h
    src/core/platform/File.cpp
    src/core/platform/File.h
    src/core/platform/FrameInput.h
    src/core/platform/Key.h
    src/core/platform/NativeWindowHandle.h
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
    src/core/geometry/generator/MeshGen.cpp
    src/core/geometry/generator/MeshGen.h
)
# Logging uses a small atomic spin lock rather than std::mutex, so no pthread symbol is referenced.
# Reason: the posix thread model makes g++ pass an implicit -lpthread, which on MinGW resolves to the
# STATIC libpthread.a, and vcpkg's libcpr.dll.a re-exports the same winpthread symbols - linking both
# fails with "multiple definition of pthread_mutex_lock". That is why there is no find_package(Threads)
# and no -lpthread anywhere here (adding either one brings the conflict straight back).
target_include_directories(stv3d_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
# NOTE: deliberately NO Qt, NO OpenGL and NO OS headers here - only the C++ standard library.

# ---- stv3d_engine: scene logic, Qt-free and window-free -------------------
add_library(stv3d_engine STATIC
    src/game/Camera.h
    src/game/Model.h
    src/game/Character.cpp
    src/game/Character.h
    src/game/GameLoop.cpp
    src/game/GameLoop.h
    src/game/InputMapping.cpp
    src/game/InputMapping.h
)
target_link_libraries(stv3d_engine PUBLIC stv3d_core)

# ---- stv3d_platform_win32: the window and its input ------------------------
add_library(stv3d_platform_win32 STATIC
    src/platform/win32/Win32Module.cpp
    src/platform/win32/Win32Module.h
    src/platform/win32/Win32Window.cpp
    src/platform/win32/Win32Window.h
)
# The implementation includes <windows.h>, but the public header deliberately does not, so nothing
# above it inherits the classic `near`/`far`/`min`/`max` macros.
target_link_libraries(stv3d_platform_win32 PUBLIC stv3d_core PRIVATE user32 gdi32)

# ---- stv3d_render: the RHI, backend-independent ---------------------------
# Header-only interface target: every backend (gl/, and later vk/ and d3d11/) implements it, and the
# app links it instead of any particular API.
add_library(stv3d_render INTERFACE)
target_link_libraries(stv3d_render INTERFACE stv3d_core)
target_sources(stv3d_render INTERFACE
    src/render/rhi/RenderDevice.h
    src/render/rhi/RenderTypes.h
)

# ---- stv3d_render_gl: OpenGL 4.3 backend ----------------------------------
add_library(stv3d_render_gl STATIC
    src/render/gl/GLContext.cpp
    src/render/gl/GLContext.h
    src/render/gl/GLFunctions.cpp
    src/render/gl/GLFunctions.h
    src/render/gl/GLFunctions.inc
    src/render/gl/GLRenderDevice.cpp
    src/render/gl/GLRenderDevice.h
)
target_link_libraries(stv3d_render_gl PUBLIC stv3d_render PRIVATE opengl32 gdi32)

# ---- third-party dependencies ---------------------------------------------
find_package(cpr CONFIG REQUIRED)            # HTTP client (libcurl wrapper)
find_package(nlohmann_json CONFIG REQUIRED)  # JSON parsing (header-only, unused so far)

# ---- stv3d-lab: the executable (assembly root) -----------------------------
add_executable(stv3d-lab
    src/main.cpp
    src/app/Sandbox.cpp
    src/app/Sandbox.h
)
target_link_libraries(stv3d-lab PRIVATE
    stv3d_core
    stv3d_engine
    stv3d_platform_win32
    stv3d_render
    stv3d_render_gl
    cpr::cpr
    nlohmann_json::nlohmann_json
)

# ---- unit tests: no window, no GPU, no Qt ---------------------------------
enable_testing()
add_executable(stv3d_core_tests tests/core_math_test.cpp)
target_link_libraries(stv3d_core_tests PRIVATE stv3d_core)
add_test(NAME core_math COMMAND stv3d_core_tests)

add_executable(stv3d_engine_tests tests/engine_test.cpp)
target_link_libraries(stv3d_engine_tests PRIVATE stv3d_engine)
add_test(NAME engine_scene COMMAND stv3d_engine_tests)

# Geometry generators are pure CPU code: the cube can be checked without a device
add_executable(stv3d_core_geometry_tests tests/core_geometry_test.cpp)
target_link_libraries(stv3d_core_geometry_tests PRIVATE stv3d_core)
add_test(NAME core_geometry COMMAND stv3d_core_geometry_tests)

# The logging test also proves that core/log needs nothing but the standard library
add_executable(stv3d_core_log_tests tests/core_log_test.cpp)
target_link_libraries(stv3d_core_log_tests PRIVATE stv3d_core)
add_test(NAME core_log COMMAND stv3d_core_log_tests)

# ---- runtime deployment (Windows) ---------------------------------------
# Shaders are real files on disk now (the Qt resource system is gone), so they are copied next to the
# executable where main.cpp looks for them.
add_custom_command(TARGET stv3d-lab POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders" "$<TARGET_FILE_DIR:stv3d-lab>/shaders"
    COMMENT "Copying shaders next to stv3d-lab.exe")

# Copy the vcpkg runtime DLLs next to the executable so it can be launched directly.
if(WIN32 AND DEFINED VCPKG_INSTALLED_DIR)
    file(GLOB _VCPKG_RUNTIME_DLLS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/*.dll")
    if(_VCPKG_RUNTIME_DLLS)
        add_custom_command(TARGET stv3d-lab POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_VCPKG_RUNTIME_DLLS} $<TARGET_FILE_DIR:stv3d-lab>
            COMMENT "Copying vcpkg runtime DLLs next to stv3d-lab.exe")
    endif()
endif()
