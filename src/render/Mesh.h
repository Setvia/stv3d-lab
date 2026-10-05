#ifndef RENDER_MESH_H
#define RENDER_MESH_H

#include "render/gl/GLFunctions.h"

#include <cstddef>
#include <vector>

// Vertex: position (3 floats) + color (3 floats), stored interleaved.
// POD so that offsetof can spell out the attribute offsets explicitly
// (matching the shader's layout(location=N)).
struct Vertex
{
    float position[3];
    float color[3];
};

// A mesh = one VAO/VBO/EBO set plus the index count, i.e. "the geometry itself".
//
// Lifetime rules:
//   * The GL function table is passed in at create() time. There is no global GL state to reach for
//     and no Qt object owning the pointers, so the dependency is visible in the signature.
//   * Every GL call needs a current context: create() after GLContext::makeCurrent(), and destroy()
//     while it is still current (the destructor calls destroy()).
//   * Copying is disabled (two objects would hold the same GLuint values -> a double glDelete);
//     moving is allowed (so a mesh can live in a std::vector).
class Mesh
{
public:
    // Explicit attribute numbers and binding index (one-to-one with the shader's
    // layout(location = N) in shaders/basic.vert)
    static constexpr GLuint kAttribPos = 0;
    static constexpr GLuint kAttribColor = 1;
    static constexpr GLuint kBindingInterleaved = 0;  // position+color interleaved in one VBO

    Mesh() = default;
    ~Mesh();

    Mesh(const Mesh &) = delete;
    Mesh &operator=(const Mesh &) = delete;
    Mesh(Mesh &&other) noexcept;
    Mesh &operator=(Mesh &&other) noexcept;

    // Upload the geometry and set up the explicit "attribute number <-> binding index <-> buffer" wiring
    void create(GLFunctions &gfx, const std::vector<Vertex> &vertices, const std::vector<GLuint> &indices);

    // Release the GL objects (safe to call repeatedly; requires a current context)
    void destroy();

    bool isValid() const { return vao != 0 && index_count > 0; }
    GLsizei getIndexCount() const { return index_count; }
    static constexpr GLsizei vertexStride() { return static_cast<GLsizei>(sizeof(Vertex)); }

    // Draw: explicitly replay all binding state (without relying on what the VAO "remembers")
    // The caller is responsible for binding the program and for setting the uniforms
    void draw();

private:
    GLFunctions *gfx = nullptr;  // not owned: the table belongs to the render backend
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    GLsizei index_count = 0;
};

// Geometry factories: build the common shapes, so vertex arrays are not hand-written everywhere
namespace MeshFactory
{
// Cube: 8 vertices (one color per corner) + 36 indices (12 triangles); size is the edge length
void makeCube(std::vector<Vertex> &vertices, std::vector<GLuint> &indices, float size = 1.0f);
}  // namespace MeshFactory

#endif  // RENDER_MESH_H
