#ifndef RENDER_GL_GLFUNCTIONS_H
#define RENDER_GL_GLFUNCTIONS_H

#include <GL/glcorearb.h>

// Hand-written OpenGL entry point table.
//
// Why hand-written: on Windows opengl32.dll only exports OpenGL 1.1, so every newer entry point has to
// be fetched at runtime through wglGetProcAddress. Declaring the ones this project calls (and only
// those) keeps the table at one screen and reviewable.
//
// Only core profile types are usable through this table: the declarations come from <GL/glcorearb.h>,
// so a legacy call cannot even be spelled.
class GLFunctions
{
public:
    // Resolve every entry point in GLFunctions.inc; false when one of them is missing
    bool load();

    bool isLoaded() const { return loaded; }

    // Name of the first entry point that could not be resolved (nullptr when load() succeeded)
    const char *missingFunction() const { return missing; }

#define X(type, name) type name = nullptr;
#include "GLFunctions.inc"
#undef X

private:
    bool loaded = false;
    const char *missing = nullptr;
};

#endif  // RENDER_GL_GLFUNCTIONS_H
