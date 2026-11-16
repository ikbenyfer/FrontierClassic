// One include for OpenGL on every platform.
// iOS / Linux: OpenGL ES 2.0. Windows: desktop OpenGL 2.1, which runs the same
// shader and calls; the functions newer than OpenGL 1.1 are loaded at start-up.
#pragma once

#if defined(_WIN32)
#include "SDL_opengl.h"

namespace fcgl {
extern PFNGLCREATESHADERPROC CreateShader;
extern PFNGLSHADERSOURCEPROC ShaderSource;
extern PFNGLCOMPILESHADERPROC CompileShader;
extern PFNGLGETSHADERIVPROC GetShaderiv;
extern PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog;
extern PFNGLDELETESHADERPROC DeleteShader;
extern PFNGLCREATEPROGRAMPROC CreateProgram;
extern PFNGLATTACHSHADERPROC AttachShader;
extern PFNGLBINDATTRIBLOCATIONPROC BindAttribLocation;
extern PFNGLLINKPROGRAMPROC LinkProgram;
extern PFNGLGETPROGRAMIVPROC GetProgramiv;
extern PFNGLDELETEPROGRAMPROC DeleteProgram;
extern PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation;
extern PFNGLUSEPROGRAMPROC UseProgram;
extern PFNGLUNIFORM4FPROC Uniform4f;
extern PFNGLUNIFORM2FPROC Uniform2f;
extern PFNGLUNIFORM1IPROC Uniform1i;
extern PFNGLUNIFORM1FPROC Uniform1f;
extern PFNGLACTIVETEXTUREPROC ActiveTexture;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC EnableVertexAttribArray;
extern PFNGLVERTEXATTRIBPOINTERPROC VertexAttribPointer;
bool load();  // after the GL context exists
}  // namespace fcgl

#define glCreateShader fcgl::CreateShader
#define glShaderSource fcgl::ShaderSource
#define glCompileShader fcgl::CompileShader
#define glGetShaderiv fcgl::GetShaderiv
#define glGetShaderInfoLog fcgl::GetShaderInfoLog
#define glDeleteShader fcgl::DeleteShader
#define glCreateProgram fcgl::CreateProgram
#define glAttachShader fcgl::AttachShader
#define glBindAttribLocation fcgl::BindAttribLocation
#define glLinkProgram fcgl::LinkProgram
#define glGetProgramiv fcgl::GetProgramiv
#define glDeleteProgram fcgl::DeleteProgram
#define glGetUniformLocation fcgl::GetUniformLocation
#define glUseProgram fcgl::UseProgram
#define glUniform4f fcgl::Uniform4f
#define glUniform2f fcgl::Uniform2f
#define glUniform1i fcgl::Uniform1i
#define glUniform1f fcgl::Uniform1f
#define glActiveTexture fcgl::ActiveTexture
#define glEnableVertexAttribArray fcgl::EnableVertexAttribArray
#define glVertexAttribPointer fcgl::VertexAttribPointer

#else
#include "SDL_opengles2.h"
namespace fcgl {
inline bool load() { return true; }
}
#endif
