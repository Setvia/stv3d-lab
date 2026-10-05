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
// The same block shape is what Vulkan (uniform buffer) and D3D11 (cbuffer) will consume, which is
// why the demo no longer uses individual uniforms.
layout(std140, binding = 0) uniform Scene
{
    mat4 uMvp;
} scene;

out vec3 vColor;

void main()
{
    vColor = aColor;
    gl_Position = scene.uMvp * vec4(aPosition, 1.0);
}
