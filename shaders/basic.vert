#version 430 core

// ---------------------------------------------------------------------------
// Attribute locations must match the VertexAttribute list in Sandbox::createResources:
//   location 0 = Vertex::position, location 1 = Vertex::color
// The backend describes the layout explicitly (glVertexAttribFormat + glVertexAttribBinding +
// glBindVertexBuffer), so nothing here relies on attribute names being matched up automatically.
// ---------------------------------------------------------------------------
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aColor;

// One constant block per draw, std140, bound to slot 0.
// The same block shape is what the Vulkan (uniform buffer) and D3D11 (cbuffer) backends bind.
layout(std140, binding = 0) uniform Scene
{
    mat4 uMvp;
} scene;

// Location on an output is not required by OpenGL, but SPIR-V (Vulkan) demands one for every user
// input/output, so both backends consume this same source.
layout(location = 0) out vec3 vColor;

void main()
{
    vColor = aColor;
    gl_Position = scene.uMvp * vec4(aPosition, 1.0);
}
