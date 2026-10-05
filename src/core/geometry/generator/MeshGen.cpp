#include "core/geometry/generator/MeshGen.h"

namespace MeshGen
{

MeshData makeCube(float size)
{
    const float h = size * 0.5f;  // half edge length

    // The corner colours are what the demo shader shows; keeping them per corner (instead of per
    // face) is why the cube looks like it does.
    const vec3 corners[8] = {
        {-h, -h, -h}, {h, -h, -h}, {h, h, -h}, {-h, h, -h},
        {-h, -h, h},  {h, -h, h},  {h, h, h},  {-h, h, h},
    };
    const vec3 colors[8] = {
        {0.2f, 0.3f, 0.8f}, {0.2f, 0.8f, 0.3f}, {0.8f, 0.8f, 0.2f}, {0.8f, 0.2f, 0.3f},
        {0.3f, 0.8f, 0.8f}, {0.8f, 0.3f, 0.8f}, {0.9f, 0.9f, 0.9f}, {0.3f, 0.3f, 0.3f},
    };

    MeshData mesh;
    mesh.vertices.reserve(8);
    for (int i = 0; i < 8; ++i) {
        Vertex vertex;
        vertex.position = corners[i];
        vertex.color = colors[i];
        vertex.normal = corners[i].normalized();  // corner direction: shared by three faces
        vertex.uv = vec2{0.0f, 0.0f};
        mesh.vertices.push_back(vertex);
    }

    // Every face lists its four corners counter-clockwise as seen from OUTSIDE, split into two
    // triangles that share the 1-3 diagonal. Winding correctness matters as soon as face culling is
    // switched on - and the inherited version of this list had three faces (back, right, top) wound
    // the wrong way round, which the geometry unit test caught.
    mesh.indices = {
        0, 3, 2, 2, 1, 0,  // back   (z = -h)
        4, 5, 6, 6, 7, 4,  // front  (z = +h)
        0, 4, 7, 7, 3, 0,  // left   (x = -h)
        1, 2, 6, 6, 5, 1,  // right  (x = +h)
        3, 7, 6, 6, 2, 3,  // top    (y = +h)
        0, 1, 5, 5, 4, 0,  // bottom (y = -h)
    };
    return mesh;
}

}  // namespace MeshGen
