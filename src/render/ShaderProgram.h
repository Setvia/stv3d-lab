#ifndef RENDER_SHADERPROGRAM_H
#define RENDER_SHADERPROGRAM_H

#include "render/gl/GLFunctions.h"

#include "core/math/mat4.h"
#include "core/math/vec3.h"

#include <string>
#include <unordered_map>
#include <vector>

// Shader program: "compile vertex/fragment shader -> link -> look up and cache uniform locations".
//
// Consistent with the rest of the project:
//   * raw GL calls through the injected function table (never QOpenGLShaderProgram)
//   * explicit error logging: on compile/link failure the GL info log goes into stv3d-lab.log
//   * copying is disabled, moving is allowed (a GL handle can only have one owner)
//   * every GL call requires a current context: create after GLContext::makeCurrent(), and destroy()
//     while it is still current
//
// Shader sources are plain files on disk (shaders/basic.vert, shaders/basic.frag). CMake copies the
// shaders/ directory next to the executable, so the app reads them from its own directory and the
// quirk of Qt resource paths (":/shaders/...") is gone for good.
class ShaderProgram
{
public:
    // Explicit attribute number binding: location matches layout(location = N) in the shader
    struct AttributeBinding
    {
        GLuint location;
        const char *name;
    };

    ShaderProgram() = default;
    ~ShaderProgram();

    ShaderProgram(const ShaderProgram &) = delete;
    ShaderProgram &operator=(const ShaderProgram &) = delete;
    ShaderProgram(ShaderProgram &&other) noexcept;
    ShaderProgram &operator=(ShaderProgram &&other) noexcept;

    // Load from files and create; false (with the reason logged) when a file or a shader is bad
    bool createFromFiles(GLFunctions &gfx,
                         const std::string &vertexPath,
                         const std::string &fragmentPath,
                         const std::vector<AttributeBinding> &attributeBindings = {});

    // Create straight from source (handy for tests or runtime assembly)
    bool createFromSource(GLFunctions &gfx,
                          const std::string &vertexSource,
                          const std::string &fragmentSource,
                          const std::vector<AttributeBinding> &attributeBindings = {});

    // Release the program (safe to call repeatedly; requires a current context)
    void destroy();

    bool isValid() const { return program != 0; }
    GLuint getProgramId() const { return program; }

    // Enable/disable the program
    void bind();
    void release();

    // ---------- uniform ----------
    // The location is queried once and then cached (repeated calls do not touch GL again)
    GLint uniformLocation(const char *name);

    // Uniform setters take core math types, so the engine layer never has to know which graphics API
    // (or which math library) is behind this class
    bool setMat4(const char *name, const mat4 &value);
    bool setVec3(const char *name, const vec3 &value);
    bool setFloat(const char *name, float value);

    // Read a text file (returns false and logs when it cannot be opened)
    static bool readTextFile(const std::string &path, std::string &outText);

private:
    GLuint compileShader(GLenum type, const std::string &source, const std::string &label);
    bool linkProgram(GLuint vertexShader, GLuint fragmentShader,
                     const std::vector<AttributeBinding> &attributeBindings);

    GLFunctions *gfx = nullptr;  // not owned: the table belongs to the render backend
    GLuint program = 0;
    std::unordered_map<std::string, GLint> uniform_cache;  // uniform name -> location
};

#endif  // RENDER_SHADERPROGRAM_H
