#pragma once
#include <SDL3/SDL.h>
#if defined(__ANDROID__) || defined(ANDROID)
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#define kotonoha_glActiveTexture glActiveTexture
#else
#include <SDL3/SDL_opengl.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Loads the OpenGL entry points used by Anime4K.
   Call only after an OpenGL context is current on this thread. */
bool KotonohaOpenGLInit(void);
void KotonohaOpenGLReset(void);
bool KotonohaOpenGLIsLoaded(void);

#if !defined(__ANDROID__) && !defined(ANDROID)
/* OpenGL 2.0 */
extern PFNGLCREATESHADERPROC glCreateShader;
extern PFNGLSHADERSOURCEPROC glShaderSource;
extern PFNGLCOMPILESHADERPROC glCompileShader;
extern PFNGLGETSHADERIVPROC glGetShaderiv;
extern PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog;
extern PFNGLDELETESHADERPROC glDeleteShader;
extern PFNGLCREATEPROGRAMPROC glCreateProgram;
extern PFNGLDELETEPROGRAMPROC glDeleteProgram;
extern PFNGLATTACHSHADERPROC glAttachShader;
extern PFNGLLINKPROGRAMPROC glLinkProgram;
extern PFNGLGETPROGRAMIVPROC glGetProgramiv;
extern PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog;
extern PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation;
extern PFNGLGETATTRIBLOCATIONPROC glGetAttribLocation;
extern PFNGLGETVERTEXATTRIBIVPROC glGetVertexAttribiv;
extern PFNGLGETVERTEXATTRIBPOINTERVPROC glGetVertexAttribPointerv;
extern PFNGLACTIVETEXTUREPROC kotonoha_glActiveTexture;
extern PFNGLUSEPROGRAMPROC glUseProgram;
extern PFNGLUNIFORM1IPROC glUniform1i;
extern PFNGLUNIFORM2FPROC glUniform2f;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray;
extern PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer;
extern PFNGLDISABLEVERTEXATTRIBARRAYPROC glDisableVertexAttribArray;

/* OpenGL 1.5 buffer objects */
extern PFNGLGENBUFFERSPROC glGenBuffers;
extern PFNGLBINDBUFFERPROC glBindBuffer;
extern PFNGLBUFFERDATAPROC glBufferData;
extern PFNGLDELETEBUFFERSPROC glDeleteBuffers;

/* OpenGL 3.0 framebuffer objects */
extern PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers;
extern PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer;
extern PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D;
extern PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus;
extern PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers;
#endif

#ifdef __cplusplus
}
#endif
