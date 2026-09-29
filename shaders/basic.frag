#version 330 core

// Output the interpolated vertex color directly (no lighting or textures yet)
in vec3 vColor;

out vec4 FragColor;

void main()
{
    FragColor = vec4(vColor, 1.0);
}
