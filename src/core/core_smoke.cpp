// Compile-time guard for the `stv3d_core` target.
//
// This translation unit includes the whole core layer and is built into a target that
// links NEITHER Qt NOR OpenGL. If somebody adds `#include <Q...>` or `#include <GL...>`
// to a core header, this file stops compiling: the layering violation is caught by the
// build instead of by review.
//
// It also pins down the layout of the math types, because the render backends rely on
// them being tightly packed float arrays that can be uploaded to the GPU as-is.

#include "geometry/MeshData.h"
#include "geometry/Triangle.h"
#include "geometry/Vertex.h"
#include "log/LogManager.h"
#include "math/conventions.h"
#include "math/mat3.h"
#include "math/mat4.h"
#include "math/quat.h"
#include "math/vec2.h"
#include "math/vec3.h"
#include "math/vec4.h"

static_assert(sizeof(vec2) == 2 * sizeof(float), "vec2 must stay tightly packed");
static_assert(sizeof(vec3) == 3 * sizeof(float), "vec3 must stay tightly packed");
static_assert(sizeof(vec4) == 4 * sizeof(float), "vec4 must stay tightly packed");
static_assert(sizeof(mat3) == 9 * sizeof(float), "mat3 must stay tightly packed");
static_assert(sizeof(mat4) == 16 * sizeof(float), "mat4 must stay tightly packed");
static_assert(sizeof(quat) == 4 * sizeof(float), "quat must stay tightly packed");
static_assert(sizeof(Vertex) == sizeof(vec3) + sizeof(vec3) + sizeof(vec2),
              "Vertex layout is described to the GPU by the render backend");
static_assert(static_cast<int>(LogLevel::Fatal) == 4,
              "LogLevel stays a plain 0..4 enum: the level order is relied upon by the log and the Qt bridge");

namespace
{
// The core math stays usable in constant expressions: a model matrix is T * R * S,
// so the translation ends up in the last column of the product.
constexpr mat4 kSmokeTRS = mat4::translate(vec3{1.0f, 2.0f, 3.0f}) * mat4::scale(vec3{2.0f, 2.0f, 2.0f});
static_assert(kSmokeTRS.m[12] == 1.0f && kSmokeTRS.m[13] == 2.0f && kSmokeTRS.m[14] == 3.0f,
              "T * S must place the translation in the last column");

constexpr vec3 kSmokeScaled = kSmokeTRS.transformDirection(vec3{1.0f, 1.0f, 1.0f});
static_assert(kSmokeScaled.x == 2.0f && kSmokeScaled.y == 2.0f && kSmokeScaled.z == 2.0f,
              "direction transforms must ignore translation and apply the scale");
}  // namespace
