// X-macro list of every OpenGL entry point this project uses.
//
// Included twice on purpose:
//   * GLFunctions.h  - to declare one member per entry point
//   * GLFunctions.cpp - to resolve them through wglGetProcAddress
// Keeping a single list means the table can never drift out of sync with the members.
//
// X(function pointer type, name)
//
// The types come from <GL/glcorearb.h>, i.e. the core profile only: no legacy entry point is
// reachable through this table by construction.

// ---- shaders and programs ----
X(PFNGLCREATESHADERPROC, glCreateShader)
X(PFNGLSHADERSOURCEPROC, glShaderSource)
X(PFNGLCOMPILESHADERPROC, glCompileShader)
X(PFNGLGETSHADERIVPROC, glGetShaderiv)
X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)
X(PFNGLDELETESHADERPROC, glDeleteShader)
X(PFNGLCREATEPROGRAMPROC, glCreateProgram)
X(PFNGLATTACHSHADERPROC, glAttachShader)
X(PFNGLDETACHSHADERPROC, glDetachShader)
X(PFNGLBINDATTRIBLOCATIONPROC, glBindAttribLocation)
X(PFNGLLINKPROGRAMPROC, glLinkProgram)
X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)
X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)
X(PFNGLUSEPROGRAMPROC, glUseProgram)
X(PFNGLDELETEPROGRAMPROC, glDeleteProgram)

// ---- uniforms ----
X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)
X(PFNGLUNIFORM1FPROC, glUniform1f)
X(PFNGLUNIFORM3FPROC, glUniform3f)
X(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv)

// ---- buffers and vertex arrays ----
X(PFNGLGENBUFFERSPROC, glGenBuffers)
X(PFNGLBINDBUFFERPROC, glBindBuffer)
X(PFNGLBUFFERDATAPROC, glBufferData)
X(PFNGLBUFFERSUBDATAPROC, glBufferSubData)
X(PFNGLBINDBUFFERBASEPROC, glBindBufferBase)
X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers)
X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)
X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)
X(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays)

// ---- explicit vertex attribute wiring (OpenGL 4.3 core) ----
X(PFNGLVERTEXATTRIBFORMATPROC, glVertexAttribFormat)
X(PFNGLVERTEXATTRIBBINDINGPROC, glVertexAttribBinding)
X(PFNGLBINDVERTEXBUFFERPROC, glBindVertexBuffer)
X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)

// ---- drawing and state ----
X(PFNGLDRAWELEMENTSPROC, glDrawElements)
X(PFNGLVIEWPORTPROC, glViewport)
X(PFNGLCLEARPROC, glClear)
X(PFNGLCLEARCOLORPROC, glClearColor)
X(PFNGLENABLEPROC, glEnable)
X(PFNGLDISABLEPROC, glDisable)
X(PFNGLDEPTHMASKPROC, glDepthMask)
X(PFNGLFINISHPROC, glFinish)
X(PFNGLGETERRORPROC, glGetError)
X(PFNGLGETSTRINGPROC, glGetString)
X(PFNGLGETINTEGERVPROC, glGetIntegerv)
