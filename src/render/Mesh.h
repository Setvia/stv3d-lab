#ifndef RENDER_MESH_H
#define RENDER_MESH_H

#include <QOpenGLFunctions_4_3_Core>

#include <cstddef>
#include <vector>

// Vertex: position (3 floats) + color (3 floats), stored interleaved.
// POD so that offsetof can spell out the attribute offsets explicitly
// (matching the shader's layout(location=N)).
struct MyVertex
{
    float position[3];
    float color[3];
};

// A mesh = one VAO/VBO/EBO set plus the index count, i.e. "the geometry itself".
//
// Lifetime rules:
//   * Every GL call requires a current valid context: call create() in initializeGL(),
//     and destroy() after makeCurrent() (MyGLWidget's destructor already follows this pattern).
//   * Copying is disabled (two objects would hold the same GLuint values -> a double glDelete);
//     moving is allowed (so a mesh can live in a std::vector).
//   * The destructor calls destroy(), so a current context is required when destroying too.
class MyMesh : protected QOpenGLFunctions_4_3_Core
{
public:
    // Explicit attribute numbers and binding index (one-to-one with the shader's
    // layout(location = N) in my3d.cpp)
    static constexpr GLuint kAttribPos = 0;
    static constexpr GLuint kAttribColor = 1;
    static constexpr GLuint kBindingInterleaved = 0;  // position+color interleaved in one VBO, sharing a single binding index

    MyMesh() = default;
    ~MyMesh();

    MyMesh(const MyMesh &) = delete;
    MyMesh &operator=(const MyMesh &) = delete;
    MyMesh(MyMesh &&other) noexcept;
    MyMesh &operator=(MyMesh &&other) noexcept;

    // Upload the geometry and set up the explicit "attribute number <-> binding index <-> buffer" wiring
    void create(const std::vector<MyVertex> &vertices, const std::vector<GLuint> &indices);

    // Release the GL objects (safe to call repeatedly; requires a current context)
    void destroy();

    bool isValid() const { return m_vao != 0 && m_index_count > 0; }
    GLsizei indexCount() const { return m_index_count; }
    static constexpr GLsizei vertexStride() { return static_cast<GLsizei>(sizeof(MyVertex)); }

    // Draw: explicitly replay all binding state (without relying on what the VAO "remembers")
    // The caller is responsible for glUseProgram and for setting the uniforms
    void draw();

private:
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_ebo = 0;
    GLsizei m_index_count = 0;
};

// Geometry factories: build the common shapes, so vertex arrays are not hand-written everywhere
namespace MyMeshFactory
{
// Cube: 8 vertices (one color per corner) + 36 indices (12 triangles); size is the edge length
void makeCube(std::vector<MyVertex> &vertices, std::vector<GLuint> &indices, float size = 1.0f);
}  // namespace MyMeshFactory

#endif  // RENDER_MESH_H
