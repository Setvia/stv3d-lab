#include "ShaderProgram.h"

#include <QByteArray>
#include <QDebug>
#include <QFile>
#include <QOpenGLContext>

#include <utility>

// ---------------- construction / destruction / move ----------------

MyShaderProgram::~MyShaderProgram()
{
    // Note: the caller must guarantee a "current GL context" here
    // (MyGLWidget is destroyed after makeCurrent())
    destroy();
}

MyShaderProgram::MyShaderProgram(MyShaderProgram &&other) noexcept
    : m_program(other.m_program), m_uniform_cache(std::move(other.m_uniform_cache))
{
    other.m_program = 0;  // handle ownership transfer, so the source does not delete it when destroyed
}

MyShaderProgram &MyShaderProgram::operator=(MyShaderProgram &&other) noexcept
{
    if (this != &other) {
        destroy();

        m_program = other.m_program;
        m_uniform_cache = std::move(other.m_uniform_cache);

        other.m_program = 0;
        other.m_uniform_cache.clear();
    }
    return *this;
}

// ---------------- file reading ----------------

bool MyShaderProgram::readTextFile(const QString &path, QString &outText)
{
    QFile file(path);  // QFile natively supports Qt resource paths (":/shaders/basic.vert")
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCritical().noquote() << "cannot open shader file:" << path;
        return false;
    }

    outText = QString::fromUtf8(file.readAll());
    if (outText.trimmed().isEmpty()) {
        qCritical().noquote() << "shader file is empty:" << path;
        return false;
    }
    return true;
}

// ---------------- create / destroy ----------------

bool MyShaderProgram::createFromFiles(const QString &vertexPath,
                                      const QString &fragmentPath,
                                      const std::vector<AttributeBinding> &attributeBindings)
{
    QString vertex_source;
    QString fragment_source;

    if (!readTextFile(vertexPath, vertex_source) || !readTextFile(fragmentPath, fragment_source)) {
        return false;  // no GL state is touched when reading the files fails
    }

    if (!createFromSource(vertex_source, fragment_source, attributeBindings)) {
        return false;
    }

    qInfo().noquote() << "shader program ready: id =" << m_program
                      << "| vertex:" << vertexPath << "| fragment:" << fragmentPath;
    return true;
}

bool MyShaderProgram::createFromSource(const QString &vertexSource,
                                       const QString &fragmentSource,
                                       const std::vector<AttributeBinding> &attributeBindings)
{
    // Check explicitly for a "current GL context" first: without a context, calling
    // initializeOpenGLFunctions() directly hits a null pointer inside Qt (it does crash in practice),
    // so we must block it here
    if (QOpenGLContext::currentContext() == nullptr) {
        qCritical("No current OpenGL context; cannot create the shader program (create it inside initializeGL())");
        return false;
    }

    if (!initializeOpenGLFunctions()) {
        qCritical("Failed to load the OpenGL 4.3 Core functions (context version too low)");
        return false;
    }

    destroy();  // release the old program first when creating again

    const GLuint vs = compileShader(GL_VERTEX_SHADER, vertexSource, "vertex shader");
    const GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentSource, "fragment shader");
    if (vs == 0 || fs == 0) {
        return false;
    }

    const bool linked = linkProgram(vs, fs, attributeBindings);

    // Clean up the shader objects regardless of success (they are useless after linking)
    glDetachShader(m_program, vs);
    glDetachShader(m_program, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    if (!linked) {
        return false;
    }

    m_uniform_cache.clear();  // uniform locations must be queried again for the new program
    return true;
}

void MyShaderProgram::destroy()
{
    if (m_program != 0) {
        glDeleteProgram(m_program);
        m_program = 0;
    }
    m_uniform_cache.clear();
}

// ---------------- compile / link ----------------

GLuint MyShaderProgram::compileShader(GLenum type, const QString &source, const QString &label)
{
    const QByteArray utf8 = source.toUtf8();  // GLSL takes a byte stream, so convert the Qt string to UTF-8 first

    const GLuint shader = glCreateShader(type);
    const char *raw = utf8.constData();
    glShaderSource(shader, 1, &raw, nullptr);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        QByteArray info(length > 0 ? length : 1, '\0');
        glGetShaderInfoLog(shader, length, nullptr, info.data());
        qCritical().noquote() << label << "compile failed:" << QString::fromUtf8(info).trimmed();
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool MyShaderProgram::linkProgram(GLuint vertexShader, GLuint fragmentShader,
                                  const std::vector<AttributeBinding> &attributeBindings)
{
    m_program = glCreateProgram();
    glAttachShader(m_program, vertexShader);
    glAttachShader(m_program, fragmentShader);

    // Explicitly bind the attribute numbers (belt and braces alongside layout(location = N) in the shader)
    for (const AttributeBinding &binding : attributeBindings) {
        glBindAttribLocation(m_program, binding.location, binding.name);
    }

    glLinkProgram(m_program);

    GLint linked = GL_FALSE;
    glGetProgramiv(m_program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        GLint length = 0;
        glGetProgramiv(m_program, GL_INFO_LOG_LENGTH, &length);
        QByteArray info(length > 0 ? length : 1, '\0');
        glGetProgramInfoLog(m_program, length, nullptr, info.data());
        qCritical().noquote() << "program link failed:" << QString::fromUtf8(info).trimmed();
        glDeleteProgram(m_program);
        m_program = 0;
        return false;
    }
    return true;
}

// ---------------- usage ----------------

void MyShaderProgram::bind()
{
    if (isValid()) {
        glUseProgram(m_program);
    }
}

void MyShaderProgram::release()
{
    glUseProgram(0);
}

// ---------------- uniform ----------------

GLint MyShaderProgram::uniformLocation(const char *name)
{
    if (!isValid() || name == nullptr) {
        return -1;
    }

    const std::string key(name);
    const auto found = m_uniform_cache.find(key);
    if (found != m_uniform_cache.end()) {
        return found->second;  // cache hit, no GL access needed
    }

    const GLint location = glGetUniformLocation(m_program, name);
    if (location < 0) {
        qWarning().noquote() << "uniform not found (it may have been optimized away by the compiler):" << name;
    }
    m_uniform_cache.emplace(key, location);
    return location;
}

bool MyShaderProgram::setMat4(const char *name, const mat4 &value)
{
    const GLint location = uniformLocation(name);
    if (location < 0) {
        return false;
    }
    // core::mat4 stores its 16 floats column-major, which is exactly what GL expects
    glUniformMatrix4fv(location, 1, GL_FALSE, value.m);
    return true;
}

bool MyShaderProgram::setVec3(const char *name, const vec3 &value)
{
    const GLint location = uniformLocation(name);
    if (location < 0) {
        return false;
    }
    glUniform3f(location, value.x, value.y, value.z);
    return true;
}

bool MyShaderProgram::setFloat(const char *name, float value)
{
    const GLint location = uniformLocation(name);
    if (location < 0) {
        return false;
    }
    glUniform1f(location, value);
    return true;
}
