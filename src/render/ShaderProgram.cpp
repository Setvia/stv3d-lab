#include "ShaderProgram.h"

#include "core/log/LogManager.h"

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

namespace
{

// A GL info log arrives as a NUL-terminated text buffer whose lines are padded with spaces and
// usually wrapped in newlines. The project writes one message per log line, so the lines are
// trimmed and joined with " | " instead of letting a driver message break the format.
std::string sanitizedInfoLog(const std::string &info)
{
    std::istringstream lines(info);
    std::string text;
    std::string line;
    while (std::getline(lines, line))
    {
        const std::size_t first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos)
        {
            continue;  // blank line
        }
        const std::size_t last = line.find_last_not_of(" \t\r");
        if (!text.empty())
        {
            text += " | ";
        }
        text += line.substr(first, last - first + 1);
    }
    return text;
}

// GL writes a NUL-terminated string into the buffer; stop at the first NUL byte
std::string infoLogFromBuffer(const std::string &buffer)
{
    return sanitizedInfoLog(std::string(buffer.c_str()));
}

}  // namespace

// ---------------- construction / destruction / move ----------------

ShaderProgram::~ShaderProgram()
{
    // Note: the caller must guarantee a current GL context here
    // (the app destroys its programs before the GLContext goes away)
    destroy();
}

ShaderProgram::ShaderProgram(ShaderProgram &&other) noexcept
    : gfx(other.gfx), program(other.program), uniform_cache(std::move(other.uniform_cache))
{
    other.gfx = nullptr;
    other.program = 0;  // handle ownership transfer, so the source does not delete it when destroyed
}

ShaderProgram &ShaderProgram::operator=(ShaderProgram &&other) noexcept
{
    if (this != &other) {
        destroy();

        gfx = other.gfx;
        program = other.program;
        uniform_cache = std::move(other.uniform_cache);

        other.gfx = nullptr;
        other.program = 0;
        other.uniform_cache.clear();
    }
    return *this;
}

// ---------------- file reading ----------------

bool ShaderProgram::readTextFile(const std::string &path, std::string &outText)
{
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR() << "cannot open shader file: " << path;
        return false;
    }

    std::ostringstream contents;
    contents << file.rdbuf();
    outText = contents.str();

    if (outText.find_first_not_of(" \t\r\n") == std::string::npos) {
        LOG_ERROR() << "shader file is empty: " << path;
        return false;
    }
    return true;
}

// ---------------- create / destroy ----------------

bool ShaderProgram::createFromFiles(GLFunctions &gfx,
                                    const std::string &vertexPath,
                                    const std::string &fragmentPath,
                                    const std::vector<AttributeBinding> &attributeBindings)
{
    std::string vertex_source;
    std::string fragment_source;

    if (!readTextFile(vertexPath, vertex_source) || !readTextFile(fragmentPath, fragment_source)) {
        return false;  // no GL state is touched when reading the files fails
    }

    if (!createFromSource(gfx, vertex_source, fragment_source, attributeBindings)) {
        return false;
    }

    LOG_INFO() << "shader program ready: id = " << program
               << " | vertex: " << vertexPath
               << " | fragment: " << fragmentPath;
    return true;
}

bool ShaderProgram::createFromSource(GLFunctions &gfx,
                                     const std::string &vertexSource,
                                     const std::string &fragmentSource,
                                     const std::vector<AttributeBinding> &attributeBindings)
{
    if (!gfx.isLoaded()) {
        LOG_ERROR() << "cannot create the shader program: the OpenGL function table is not loaded";
        return false;
    }

    this->gfx = &gfx;
    destroy();  // release the old program first when creating again

    const GLuint vs = compileShader(GL_VERTEX_SHADER, vertexSource, "vertex shader");
    const GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentSource, "fragment shader");
    if (vs == 0 || fs == 0) {
        return false;
    }

    const bool linked = linkProgram(vs, fs, attributeBindings);

    // Clean up the shader objects regardless of success (they are useless after linking)
    this->gfx->glDetachShader(program, vs);
    this->gfx->glDetachShader(program, fs);
    this->gfx->glDeleteShader(vs);
    this->gfx->glDeleteShader(fs);

    if (!linked) {
        return false;
    }
    return true;
}

void ShaderProgram::destroy()
{
    if (program != 0 && gfx != nullptr) {
        gfx->glDeleteProgram(program);
    }
    program = 0;
    uniform_cache.clear();
}

// ---------------- compilation / linking ----------------

GLuint ShaderProgram::compileShader(GLenum type, const std::string &source, const std::string &label)
{
    const GLuint shader = gfx->glCreateShader(type);

    const char *source_text = source.c_str();
    const GLint source_length = static_cast<GLint>(source.size());
    gfx->glShaderSource(shader, 1, &source_text, &source_length);
    gfx->glCompileShader(shader);

    GLint compiled = GL_FALSE;
    gfx->glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        GLint length = 0;
        gfx->glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::string info(static_cast<std::size_t>(length > 0 ? length : 1), '\0');
        gfx->glGetShaderInfoLog(shader, length, nullptr, &info[0]);
        LOG_ERROR() << label << ": compile failed: " << infoLogFromBuffer(info);
        gfx->glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool ShaderProgram::linkProgram(GLuint vertexShader, GLuint fragmentShader,
                                const std::vector<AttributeBinding> &attributeBindings)
{
    program = gfx->glCreateProgram();
    gfx->glAttachShader(program, vertexShader);
    gfx->glAttachShader(program, fragmentShader);

    // Explicitly bind the attribute numbers (belt and braces alongside layout(location = N) in the shader)
    for (const AttributeBinding &binding : attributeBindings) {
        gfx->glBindAttribLocation(program, binding.location, binding.name);
    }

    gfx->glLinkProgram(program);

    GLint linked = GL_FALSE;
    gfx->glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        GLint length = 0;
        gfx->glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::string info(static_cast<std::size_t>(length > 0 ? length : 1), '\0');
        gfx->glGetProgramInfoLog(program, length, nullptr, &info[0]);
        LOG_ERROR() << "program link failed: " << infoLogFromBuffer(info);
        gfx->glDeleteProgram(program);
        program = 0;
        return false;
    }
    return true;
}

// ---------------- usage ----------------

void ShaderProgram::bind()
{
    if (isValid()) {
        gfx->glUseProgram(program);
    }
}

void ShaderProgram::release()
{
    if (gfx != nullptr) {
        gfx->glUseProgram(0);
    }
}

// ---------------- uniform ----------------

GLint ShaderProgram::uniformLocation(const char *name)
{
    if (!isValid() || name == nullptr) {
        return -1;
    }

    const std::string key(name);
    const auto found = uniform_cache.find(key);
    if (found != uniform_cache.end()) {
        return found->second;  // cache hit, no GL access needed
    }

    const GLint location = gfx->glGetUniformLocation(program, name);
    if (location < 0) {
        LOG_WARNING() << "uniform not found (it may have been optimized away by the compiler): " << name;
    }
    uniform_cache.emplace(key, location);
    return location;
}

bool ShaderProgram::setMat4(const char *name, const mat4 &value)
{
    const GLint location = uniformLocation(name);
    if (location < 0) {
        return false;
    }
    // core::mat4 stores its 16 floats column-major, which is exactly what GL expects
    gfx->glUniformMatrix4fv(location, 1, GL_FALSE, value.m);
    return true;
}

bool ShaderProgram::setVec3(const char *name, const vec3 &value)
{
    const GLint location = uniformLocation(name);
    if (location < 0) {
        return false;
    }
    gfx->glUniform3f(location, value.x, value.y, value.z);
    return true;
}

bool ShaderProgram::setFloat(const char *name, float value)
{
    const GLint location = uniformLocation(name);
    if (location < 0) {
        return false;
    }
    gfx->glUniform1f(location, value);
    return true;
}
