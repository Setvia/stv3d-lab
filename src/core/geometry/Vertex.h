#pragma once

#ifndef CORE_GEOMETRY_VERTEX_H
#define CORE_GEOMETRY_VERTEX_H

#include "../math/vec2.h"
#include "../math/vec3.h"

// A single vertex of CPU-side geometry. The render backend maps these fields onto a
// vertex layout (attribute locations/formats) when it creates the GPU buffers.
struct Vertex
{
    vec3 pos;   // object-space position
    vec3 norm;  // object-space normal (expected to be normalized)
    vec2 uv;    // texture coordinates
};

#endif  // CORE_GEOMETRY_VERTEX_H
