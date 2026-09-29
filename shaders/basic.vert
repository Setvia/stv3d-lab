#version 330 core

// ---------------------------------------------------------------------------
// The vertex attribute numbers must match kAttribPos / kAttribColor in
// src/render/Mesh.h:  position = 0, color = 1
// (the mesh wires them up explicitly with glVertexAttribFormat/glVertexAttribBinding)
// ---------------------------------------------------------------------------
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;

// projection * view * model, written once per model by MyShaderProgram::setMat4("uMvp", ...)
uniform mat4 uMvp;

out vec3 vColor;

void main()
{
    vColor = aColor;
    gl_Position = uMvp * vec4(aPos, 1.0);
}
