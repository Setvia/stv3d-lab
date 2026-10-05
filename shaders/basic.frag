#version 430 core

// Output the interpolated vertex color directly (no lighting or textures yet).
// The location matches the vertex shader's output - again, required by SPIR-V.
layout(location = 0) in vec3 vColor;

layout(location = 0) out vec4 FragColor;

void main()
{
    FragColor = vec4(vColor, 1.0);
}
