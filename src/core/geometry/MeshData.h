#pragma once

#ifndef CORE_GEOMETRY_MESH_DATA_H
#define CORE_GEOMETRY_MESH_DATA_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Vertex.h"

// CPU-side geometry only: this is what a generator or a loader produces and what a
// render backend consumes. It deliberately holds NO GPU handles - the backend owns
// those (see render/Mesh.h for the OpenGL implementation, and the planned Vulkan one).
//
// Keeping this type GPU-agnostic is what allows the same generated/loaded mesh to be
// uploaded to OpenGL today and to Vulkan later without touching the generators.
struct MeshData
{
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;

    [[nodiscard]] bool empty() const noexcept
    {
        return vertices.empty() || indices.empty();
    }

    [[nodiscard]] std::size_t triangleCount() const noexcept
    {
        return indices.size() / 3u;
    }

    void clear() noexcept
    {
        vertices.clear();
        indices.clear();
    }

    // Bounding box helpers are intentionally left to geometry utilities: a mesh is a
    // plain data container, not a scene object.
};

#endif  // CORE_GEOMETRY_MESH_DATA_H
