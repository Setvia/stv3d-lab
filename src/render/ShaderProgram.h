#ifndef SHADER_H
#define SHADER_H

#include <QOpenGLFunctions_4_3_Core>
#include <QString>

#include "core/math/mat4.h"
#include "core/math/vec3.h"

#include <string>
#include <unordered_map>
#include <vector>

// Shader program: wraps "compile vertex/fragment shader -> link -> look up and cache uniform locations".
//
// Consistent with the rest of the project:
//   * raw GL calls are used directly, not QOpenGLShaderProgram
//   * explicit error logging (on compile/link failure the GL info log goes into stv3d-lab.log)
//   * copying is disabled, moving is allowed (a GL handle can only have one owner)
//   * every GL call requires a current valid context: create in initializeGL(), makeCurrent() before destruction
//
// Shader source comes from files (either a Qt resource path such as ":/shaders/basic.vert" or a disk path);
// resources are compiled into the exe via stv3d-lab.qrc, so no shader files need to be copied on release.
class ShaderProgram : protected QOpenGLFunctions_4_3_Core
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

    // Load from files and create (handles Qt resource paths :/... automatically)
    bool createFromFiles(const QString &vertexPath,
                         const QString &fragmentPath,
                         const std::vector<AttributeBinding> &attributeBindings = {});

    // Create straight from source (handy for unit tests or runtime assembly)
    bool createFromSource(const QString &vertexSource,
                          const QString &fragmentSource,
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

    // Uniform setters take core math types, so the engine layer never has to know which
    // graphics API (or which math library) is behind this class.
    bool setMat4(const char *name, const mat4 &value);
    bool setVec3(const char *name, const vec3 &value);
    bool setFloat(const char *name, float value);

    // Read a text file (supports ":/..." resource paths); returns false and logs on failure
    static bool readTextFile(const QString &path, QString &outText);

private:
    GLuint compileShader(GLenum type, const QString &source, const QString &label);
    bool linkProgram(GLuint vertexShader, GLuint fragmentShader,
                     const std::vector<AttributeBinding> &attributeBindings);

    GLuint program = 0;
    std::unordered_map<std::string, GLint> uniform_cache;  // uniform name -> location
};

#endif  // SHADER_H
