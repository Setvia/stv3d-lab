#pragma once

#ifndef CORE_GEOMETRY_MESH_DATA_H
#define CORE_GEOMETRY_MESH_DATA_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Vertex.h"

// CPU-side geometry: what a generator or a loader produces and what a render backend consumes.
// GPU handles stay in the render layer, so the same generated or loaded mesh can be uploaded by any
// backend without touching the generators.
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
