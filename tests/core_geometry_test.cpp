// Unit tests for the CPU-side geometry layer (core/geometry).
//
// This target links only stv3d_core: no window, no GPU. Generators produce
// plain MeshData - that is exactly what makes them testable here, and what lets the same cube be
// uploaded to OpenGL today and to Vulkan or D3D11 later.

#include "core/geometry/MeshData.h"
#include "core/geometry/Vertex.h"
#include "core/geometry/generator/MeshGen.h"

#include <cmath>
#include <cstdio>
#include <cstdint>

namespace
{
int g_failures = 0;

void check(bool condition, const char* name)
{
    std::printf("%s %s\n", condition ? "[PASS]" : "[FAIL]", name);
    if (!condition)
    {
        ++g_failures;
    }
}

bool near(float a, float b, float eps = 1e-4f)
{
    return std::fabs(a - b) < eps;
}

}  // namespace

int main()
{
    // ======================= MeshData container =======================
    {
        MeshData empty;
        check(empty.empty(), "mesh: a default MeshData is empty");
        check(empty.triangleCount() == 0, "mesh: an empty mesh has no triangles");

        MeshData scratch = MeshGen::makeCube(1.0f);
        check(!scratch.empty(), "mesh: a generated mesh is not empty");
        scratch.clear();
        check(scratch.empty() && scratch.triangleCount() == 0,
              "mesh: clear() drops both vertices and indices");
    }

    // ========================== cube generator ==========================
    {
        const MeshData cube = MeshGen::makeCube(2.0f);

        check(cube.vertices.size() == 8, "cube: 8 corner vertices");
        check(cube.indices.size() == 36, "cube: 36 indices");
        check(cube.triangleCount() == 12, "cube: 12 triangles");
        check(cube.vertices.size() * sizeof(Vertex) == 8 * 44,
              "cube: the vertex stride the pipeline is told is sizeof(Vertex) (44 bytes)");

        bool in_range = true;
        for (const std::uint32_t index : cube.indices)
        {
            if (index >= cube.vertices.size())
            {
                in_range = false;
            }
        }
        check(in_range, "cube: every index points inside the vertex array");

        bool non_degenerate = true;
        bool consistent_winding = true;
        for (std::size_t i = 0; i + 2 < cube.indices.size(); i += 3)
        {
            const std::uint32_t a = cube.indices[i];
            const std::uint32_t b = cube.indices[i + 1];
            const std::uint32_t c = cube.indices[i + 2];
            if (a == b || b == c || a == c)
            {
                non_degenerate = false;
            }

            // Every face must be wound consistently: the geometric normal has to point away from the
            // centre (the cube is centred on the origin), otherwise the faces would be inside-out.
            const vec3 pa = cube.vertices[a].position;
            const vec3 pb = cube.vertices[b].position;
            const vec3 pc = cube.vertices[c].position;
            const vec3 face_normal = (pb - pa).cross(pc - pa);
            const vec3 center_to_face = (pa + pb + pc) / 3.0f;
            if (face_normal.dot(center_to_face) <= 0.0f)
            {
                consistent_winding = false;
            }
        }
        check(non_degenerate, "cube: no degenerate triangle (no repeated index)");
        check(consistent_winding, "cube: all 12 triangles face outwards (consistent winding)");
    }

    // ============================ size and colour ============================
    {
        const MeshData big = MeshGen::makeCube(2.0f);
        const MeshData unit = MeshGen::makeCube(1.0f);

        bool on_box = true;
        bool within = true;
        for (const Vertex& vertex : big.vertices)
        {
            if (std::fabs(std::fabs(vertex.position.x) - 1.0f) > 1e-6f
                || std::fabs(std::fabs(vertex.position.y) - 1.0f) > 1e-6f
                || std::fabs(std::fabs(vertex.position.z) - 1.0f) > 1e-6f)
            {
                on_box = false;
            }
            if (std::fabs(vertex.position.x) > 1.0f + 1e-6f
                || std::fabs(vertex.position.y) > 1.0f + 1e-6f
                || std::fabs(vertex.position.z) > 1.0f + 1e-6f)
            {
                within = false;
            }
        }
        check(on_box && within, "cube: a size of 2 puts every corner on the +-1 box");

        bool unit_corners = true;
        for (const Vertex& vertex : unit.vertices)
        {
            if (std::fabs(std::fabs(vertex.position.x) - 0.5f) > 1e-6f)
            {
                unit_corners = false;
            }
        }
        check(unit_corners, "cube: a size of 1 gives half-extent 0.5");

        check(near(big.vertices[6].color.x, 0.9f) && near(big.vertices[6].color.y, 0.9f)
                  && near(big.vertices[6].color.z, 0.9f),
              "cube: corner 6 keeps the bright colour the demo shader shows");
        check(big.vertices[6].color.x == unit.vertices[6].color.x,
              "cube: the edge length does not change the colours");

        bool unit_normals = true;
        bool outward_normals = true;
        for (const Vertex& vertex : big.vertices)
        {
            if (!near(vertex.normal.length(), 1.0f))
            {
                unit_normals = false;
            }
            if (vertex.normal.dot(vertex.position) <= 0.0f)
            {
                outward_normals = false;
            }
        }
        check(unit_normals, "cube: every normal is unit length");
        check(outward_normals, "cube: every normal points away from the centre");
    }

    std::printf("\n%s (%d failure(s))\n", g_failures == 0 ? "all checks passed" : "FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
