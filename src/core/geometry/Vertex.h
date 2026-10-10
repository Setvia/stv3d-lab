#pragma once

#ifndef CORE_GEOMETRY_VERTEX_H
#define CORE_GEOMETRY_VERTEX_H

#include "core/math/vec2.h"
#include "core/math/vec3.h"

// The CPU-side vertex format.
//
// A generator, a loader and every render backend agree on this one layout. A backend describes how
// these fields map onto shader attributes (see rhi::VertexAttribute), so the struct itself stays
// API-agnostic.
struct Vertex
{
    vec3 position;  // object space
    vec3 color;     // linear RGB; what the demo shader reads from attribute 1
    vec3 normal;    // object space, expected normalized (lighting)
    vec2 uv;        // texture coordinates (texturing)
};

#endif  // CORE_GEOMETRY_VERTEX_H
