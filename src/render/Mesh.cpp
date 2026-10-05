#include "Mesh.h"

#include <QOpenGLContext>

#include "core/log/LogManager.h"

#include <utility>

// ---------------- construction / destruction / move ----------------

Mesh::~Mesh()
{
    // Note: the caller must guarantee a "current GL context" here
    // (GLWidget is destroyed after makeCurrent())
    destroy();
}

Mesh::Mesh(Mesh &&other) noexcept
    : vao(other.vao), vbo(other.vbo), ebo(other.ebo), index_count(other.index_count)
{
    // Handle ownership transfer: null out the source so it does not delete the resources when destroyed
    other.vao = 0;
    other.vbo = 0;
    other.ebo = 0;
    other.index_count = 0;
}

Mesh &Mesh::operator=(Mesh &&other) noexcept
{
    if (this != &other) {
        destroy();  // release our own resources first

        vao = other.vao;
        vbo = other.vbo;
        ebo = other.ebo;
        index_count = other.index_count;

        other.vao = 0;
        other.vbo = 0;
        other.ebo = 0;
        other.index_count = 0;
    }
    return *this;
}

// ---------------- create / destroy ----------------

void Mesh::create(const std::vector<Vertex> &vertices, const std::vector<GLuint> &indices)
{
    if (vertices.empty() || indices.empty()) {
        return;  // empty mesh: create no GL objects at all
    }

    // Check explicitly for a "current GL context" first: without a context, calling
    // initializeOpenGLFunctions() directly hits a null pointer inside Qt, so we must block it here
    if (QOpenGLContext::currentContext() == nullptr) {
        LOG_ERROR() << "No current OpenGL context; cannot create the mesh (create it inside initializeGL())";
        return;
    }

    // This class has its own GL function table (QOpenGLFunctions is initialized per context)
    if (!initializeOpenGLFunctions()) {
        return;
    }

    destroy();  // wipe the old objects first when create() is called again

    // Under the core profile every buffer binding requires a bound VAO first
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
                 vertices.data(),
                 GL_STATIC_DRAW);

    glGenBuffers(1, &ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices.size() * sizeof(GLuint)),
                 indices.data(),
                 GL_STATIC_DRAW);

    // (1) Attribute format: attribute number, component count, type, normalized flag, byte offset (offsetof spells it out)
    glVertexAttribFormat(kAttribPos, 3, GL_FLOAT, GL_FALSE,
                         static_cast<GLuint>(offsetof(Vertex, position)));
    glVertexAttribFormat(kAttribColor, 3, GL_FLOAT, GL_FALSE,
                         static_cast<GLuint>(offsetof(Vertex, color)));

    // (2) Attribute -> binding index
    glVertexAttribBinding(kAttribPos, kBindingInterleaved);
    glVertexAttribBinding(kAttribColor, kBindingInterleaved);

    glEnableVertexAttribArray(kAttribPos);
    glEnableVertexAttribArray(kAttribColor);

    // (3) Binding index -> concrete buffer + start offset + stride
    glBindVertexBuffer(kBindingInterleaved, vbo, 0, vertexStride());

    index_count = static_cast<GLsizei>(indices.size());

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void Mesh::destroy()
{
    // Skip straight away when the handle is 0: this keeps the call repeatable and safe
    // to no-op after the context has already been destroyed
    if (ebo != 0) {
        glDeleteBuffers(1, &ebo);
        ebo = 0;
    }
    if (vbo != 0) {
        glDeleteBuffers(1, &vbo);
        vbo = 0;
    }
    if (vao != 0) {
        glDeleteVertexArrays(1, &vao);
        vao = 0;
    }
    index_count = 0;
}

// ---------------- drawing ----------------

void Mesh::draw()
{
    if (!isValid()) {
        return;
    }

    // Bind explicitly all state needed for drawing, without relying on the copy recorded in the VAO
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBindVertexBuffer(kBindingInterleaved, vbo, 0, vertexStride());

    glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_INT, nullptr);

    glBindVertexArray(0);
}

// ---------------- geometry factories ----------------

namespace MeshFactory
{

void makeCube(std::vector<Vertex> &vertices, std::vector<GLuint> &indices, float size)
{
    const float h = size * 0.5f;  // half edge length

    vertices = {
        // position              // color
        {{-h, -h, -h}, {0.2f, 0.3f, 0.8f}},
        {{ h, -h, -h}, {0.2f, 0.8f, 0.3f}},
        {{ h,  h, -h}, {0.8f, 0.8f, 0.2f}},
        {{-h,  h, -h}, {0.8f, 0.2f, 0.3f}},
        {{-h, -h,  h}, {0.3f, 0.8f, 0.8f}},
        {{ h, -h,  h}, {0.8f, 0.3f, 0.8f}},
        {{ h,  h,  h}, {0.9f, 0.9f, 0.9f}},
        {{-h,  h,  h}, {0.3f, 0.3f, 0.3f}},
    };

    indices = {
        0, 1, 2, 2, 3, 0,  // back
        4, 5, 6, 6, 7, 4,  // front
        0, 4, 7, 7, 3, 0,  // left
        1, 5, 6, 6, 2, 1,  // right
        3, 2, 6, 6, 7, 3,  // top
        0, 1, 5, 5, 4, 0,  // bottom
    };
}

}  // namespace MeshFactory
