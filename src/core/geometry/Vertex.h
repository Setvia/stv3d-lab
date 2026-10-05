#pragma once

#ifndef CORE_GEOMETRY_VERTEX_H
#define CORE_GEOMETRY_VERTEX_H

#include "core/math/vec2.h"
#include "core/math/vec3.h"

// The one and only CPU-side vertex format.
//
// There used to be two of these (a core one with pos/norm/uv and a render one with position/color);
// they are merged here so that a generator, a loader and any render backend all agree on the layout.
// A backend describes how these fields map onto shader attributes (see rhi::VertexAttribute), so the
// struct itself stays API-agnostic.
struct Vertex
{
    vec3 position;  // object space
    vec3 color;     // linear RGB; what the demo shader reads from attribute 1
    vec3 normal;    // object space, expected normalized (lighting)
    vec2 uv;        // texture coordinates (texturing)
};

#endif  // CORE_GEOMETRY_VERTEX_H
