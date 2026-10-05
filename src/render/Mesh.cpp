#include "Mesh.h"

#include "core/log/LogManager.h"

#include <utility>

// ---------------- construction / destruction / move ----------------

Mesh::~Mesh()
{
    // Note: the caller must guarantee a current GL context here
    // (the app destroys its meshes before the GLContext goes away)
    destroy();
}

Mesh::Mesh(Mesh &&other) noexcept
    : gfx(other.gfx), vao(other.vao), vbo(other.vbo), ebo(other.ebo), index_count(other.index_count)
{
    // Handle ownership transfer: null out the source so it does not delete the resources when destroyed
    other.gfx = nullptr;
    other.vao = 0;
    other.vbo = 0;
    other.ebo = 0;
    other.index_count = 0;
}

Mesh &Mesh::operator=(Mesh &&other) noexcept
{
    if (this != &other) {
        destroy();  // release our own resources first

        gfx = other.gfx;
        vao = other.vao;
        vbo = other.vbo;
        ebo = other.ebo;
        index_count = other.index_count;

        other.gfx = nullptr;
        other.vao = 0;
        other.vbo = 0;
        other.ebo = 0;
        other.index_count = 0;
    }
    return *this;
}

// ---------------- create / destroy ----------------

void Mesh::create(GLFunctions &gfx, const std::vector<Vertex> &vertices, const std::vector<GLuint> &indices)
{
    if (vertices.empty() || indices.empty()) {
        return;  // empty mesh: create no GL objects at all
    }

    if (!gfx.isLoaded()) {
        LOG_ERROR() << "cannot create the mesh: the OpenGL function table is not loaded";
        return;
    }

    this->gfx = &gfx;
    destroy();  // wipe the old objects first when create() is called again

    // Under the core profile every buffer binding requires a bound VAO first
    this->gfx->glGenVertexArrays(1, &vao);
    this->gfx->glBindVertexArray(vao);

    this->gfx->glGenBuffers(1, &vbo);
    this->gfx->glBindBuffer(GL_ARRAY_BUFFER, vbo);
    this->gfx->glBufferData(GL_ARRAY_BUFFER,
                            static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
                            vertices.data(),
                            GL_STATIC_DRAW);

    this->gfx->glGenBuffers(1, &ebo);
    this->gfx->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    this->gfx->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                            static_cast<GLsizeiptr>(indices.size() * sizeof(GLuint)),
                            indices.data(),
                            GL_STATIC_DRAW);

    // (1) Attribute format: attribute number, component count, type, normalized flag, byte offset
    this->gfx->glVertexAttribFormat(kAttribPos, 3, GL_FLOAT, GL_FALSE,
                                    static_cast<GLuint>(offsetof(Vertex, position)));
    this->gfx->glVertexAttribFormat(kAttribColor, 3, GL_FLOAT, GL_FALSE,
                                    static_cast<GLuint>(offsetof(Vertex, color)));

    // (2) Attribute -> binding index
    this->gfx->glVertexAttribBinding(kAttribPos, kBindingInterleaved);
    this->gfx->glVertexAttribBinding(kAttribColor, kBindingInterleaved);

    this->gfx->glEnableVertexAttribArray(kAttribPos);
    this->gfx->glEnableVertexAttribArray(kAttribColor);

    // (3) Binding index -> concrete buffer + start offset + stride
    this->gfx->glBindVertexBuffer(kBindingInterleaved, vbo, 0, vertexStride());

    index_count = static_cast<GLsizei>(indices.size());

    this->gfx->glBindVertexArray(0);
    this->gfx->glBindBuffer(GL_ARRAY_BUFFER, 0);
    this->gfx->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void Mesh::destroy()
{
    if (gfx == nullptr) {
        // Already released, or never created: keep the handles at 0 so a repeated call stays a no-op
        vao = 0;
        vbo = 0;
        ebo = 0;
        index_count = 0;
        return;
    }

    if (ebo != 0) {
        gfx->glDeleteBuffers(1, &ebo);
        ebo = 0;
    }
    if (vbo != 0) {
        gfx->glDeleteBuffers(1, &vbo);
        vbo = 0;
    }
    if (vao != 0) {
        gfx->glDeleteVertexArrays(1, &vao);
        vao = 0;
    }
    index_count = 0;
}

// ---------------- drawing ----------------

void Mesh::draw()
{
    if (!isValid() || gfx == nullptr) {
        return;
    }

    // Bind explicitly all state needed for drawing, without relying on the copy recorded in the VAO
    gfx->glBindVertexArray(vao);
    gfx->glBindBuffer(GL_ARRAY_BUFFER, vbo);
    gfx->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    gfx->glBindVertexBuffer(kBindingInterleaved, vbo, 0, vertexStride());

    gfx->glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_INT, nullptr);

    gfx->glBindVertexArray(0);
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
