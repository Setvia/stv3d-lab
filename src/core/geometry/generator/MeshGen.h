#pragma once

#ifndef CORE_GEOMETRY_GENERATOR_MESHGEN_H
#define CORE_GEOMETRY_GENERATOR_MESHGEN_H

#include "core/geometry/MeshData.h"

// CPU-side geometry generators.
//
// Generators live in core on purpose: they produce plain MeshData and know nothing about which
// graphics API will upload it. That also makes them unit-testable without a window (see
// tests/core_geometry_test.cpp).
namespace MeshGen
{

// Axis-aligned cube centred on the origin, edge length `size`: 8 vertices, 36 indices (12 triangles).
//
// One vertex per corner is a deliberate trade-off: the cube stays tiny (8 vertices instead of 24) and
// the corner colours stay smooth, at the cost of per-corner normals (a corner is shared by three
// faces, so its normal is the normalized corner direction). A cube with per-face normals needs the
// vertices split - that is what a real loader would do.
MeshData makeCube(float size = 1.0f);

}  // namespace MeshGen

#endif  // CORE_GEOMETRY_GENERATOR_MESHGEN_H
