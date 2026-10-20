#include <Kotonoha/speciais/OpenGL.h>

#define GL_LOAD_FUNC(type, name) name = (type)SDL_GL_GetProcAddress(#name)
#define GL_LOAD_FUNC_EXT(type, name)                                         \
  name = (type)SDL_GL_GetProcAddress(#name);                                 \
  if (name == NULL) {                                                        \
    name = (type)SDL_GL_GetProcAddress(#name "EXT");                         \
  }

PFNGLCREATESHADERPROC glCreateShader = NULL;
PFNGLSHADERSOURCEPROC glShaderSource = NULL;
PFNGLCOMPILESHADERPROC glCompileShader = NULL;
PFNGLGETSHADERIVPROC glGetShaderiv = NULL;
PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog = NULL;
PFNGLDELETESHADERPROC glDeleteShader = NULL;
PFNGLCREATEPROGRAMPROC glCreateProgram = NULL;
PFNGLDELETEPROGRAMPROC glDeleteProgram = NULL;
PFNGLATTACHSHADERPROC glAttachShader = NULL;
PFNGLLINKPROGRAMPROC glLinkProgram = NULL;
PFNGLGETPROGRAMIVPROC glGetProgramiv = NULL;
PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog = NULL;
PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation = NULL;
PFNGLGETATTRIBLOCATIONPROC glGetAttribLocation = NULL;
PFNGLGETVERTEXATTRIBIVPROC glGetVertexAttribiv = NULL;
PFNGLGETVERTEXATTRIBPOINTERVPROC glGetVertexAttribPointerv = NULL;
PFNGLACTIVETEXTUREPROC kotonoha_glActiveTexture = NULL;
PFNGLUSEPROGRAMPROC glUseProgram = NULL;
PFNGLUNIFORM1IPROC glUniform1i = NULL;
PFNGLUNIFORM2FPROC glUniform2f = NULL;
PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray = NULL;
PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer = NULL;
PFNGLDISABLEVERTEXATTRIBARRAYPROC glDisableVertexAttribArray = NULL;
PFNGLGENBUFFERSPROC glGenBuffers = NULL;
PFNGLBINDBUFFERPROC glBindBuffer = NULL;
PFNGLBUFFERDATAPROC glBufferData = NULL;
PFNGLDELETEBUFFERSPROC glDeleteBuffers = NULL;
PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers = NULL;
PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer = NULL;
PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D = NULL;
PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus = NULL;
PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers = NULL;

static bool g_glLoaded = false;

void KotonohaOpenGLReset(void) {
  g_glLoaded = false;
  glCreateShader = NULL;
  glShaderSource = NULL;
  glCompileShader = NULL;
  glGetShaderiv = NULL;
  glGetShaderInfoLog = NULL;
  glDeleteShader = NULL;
  glCreateProgram = NULL;
  glDeleteProgram = NULL;
  glAttachShader = NULL;
  glLinkProgram = NULL;
  glGetProgramiv = NULL;
  glGetProgramInfoLog = NULL;
  glGetUniformLocation = NULL;
  glGetAttribLocation = NULL;
  glGetVertexAttribiv = NULL;
  glGetVertexAttribPointerv = NULL;
  kotonoha_glActiveTexture = NULL;
  glUseProgram = NULL;
  glUniform1i = NULL;
  glUniform2f = NULL;
  glEnableVertexAttribArray = NULL;
  glVertexAttribPointer = NULL;
  glDisableVertexAttribArray = NULL;
  glGenBuffers = NULL;
  glBindBuffer = NULL;
  glBufferData = NULL;
  glDeleteBuffers = NULL;
  glGenFramebuffers = NULL;
  glBindFramebuffer = NULL;
  glFramebufferTexture2D = NULL;
  glCheckFramebufferStatus = NULL;
  glDeleteFramebuffers = NULL;
}

bool KotonohaOpenGLIsLoaded(void) { return g_glLoaded; }

bool KotonohaOpenGLInit(void) {
  KotonohaOpenGLReset();
  if (SDL_GL_GetCurrentContext() == NULL) {
    SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                 "OpenGL init failed: no current SDL OpenGL context");
    return false;
  }

  GL_LOAD_FUNC(PFNGLCREATESHADERPROC, glCreateShader);
  GL_LOAD_FUNC(PFNGLSHADERSOURCEPROC, glShaderSource);
  GL_LOAD_FUNC(PFNGLCOMPILESHADERPROC, glCompileShader);
  GL_LOAD_FUNC(PFNGLGETSHADERIVPROC, glGetShaderiv);
  GL_LOAD_FUNC(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog);
  GL_LOAD_FUNC(PFNGLDELETESHADERPROC, glDeleteShader);
  GL_LOAD_FUNC(PFNGLCREATEPROGRAMPROC, glCreateProgram);
  GL_LOAD_FUNC(PFNGLDELETEPROGRAMPROC, glDeleteProgram);
  GL_LOAD_FUNC(PFNGLATTACHSHADERPROC, glAttachShader);
  GL_LOAD_FUNC(PFNGLLINKPROGRAMPROC, glLinkProgram);
  GL_LOAD_FUNC(PFNGLGETPROGRAMIVPROC, glGetProgramiv);
  GL_LOAD_FUNC(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog);
  GL_LOAD_FUNC(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation);
  GL_LOAD_FUNC(PFNGLGETATTRIBLOCATIONPROC, glGetAttribLocation);
  GL_LOAD_FUNC(PFNGLGETVERTEXATTRIBIVPROC, glGetVertexAttribiv);
  GL_LOAD_FUNC(PFNGLGETVERTEXATTRIBPOINTERVPROC,
               glGetVertexAttribPointerv);
  kotonoha_glActiveTexture =
      (PFNGLACTIVETEXTUREPROC)SDL_GL_GetProcAddress("glActiveTexture");
  GL_LOAD_FUNC(PFNGLUSEPROGRAMPROC, glUseProgram);
  GL_LOAD_FUNC(PFNGLUNIFORM1IPROC, glUniform1i);
  GL_LOAD_FUNC(PFNGLUNIFORM2FPROC, glUniform2f);
  GL_LOAD_FUNC(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray);
  GL_LOAD_FUNC(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer);
  GL_LOAD_FUNC(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray);
  GL_LOAD_FUNC(PFNGLGENBUFFERSPROC, glGenBuffers);
  GL_LOAD_FUNC(PFNGLBINDBUFFERPROC, glBindBuffer);
  GL_LOAD_FUNC(PFNGLBUFFERDATAPROC, glBufferData);
  GL_LOAD_FUNC(PFNGLDELETEBUFFERSPROC, glDeleteBuffers);
  GL_LOAD_FUNC_EXT(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers);
  GL_LOAD_FUNC_EXT(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer);
  GL_LOAD_FUNC_EXT(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D);
  GL_LOAD_FUNC_EXT(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus);
  GL_LOAD_FUNC_EXT(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers);

  if (!glCreateShader || !glShaderSource || !glCompileShader ||
      !glGetShaderiv || !glGetShaderInfoLog || !glDeleteShader ||
      !glCreateProgram || !glDeleteProgram || !glAttachShader ||
      !glLinkProgram || !glGetProgramiv || !glGetProgramInfoLog ||
      !glGetUniformLocation || !glGetAttribLocation || !glUseProgram ||
      !glGetVertexAttribiv || !glGetVertexAttribPointerv ||
      !kotonoha_glActiveTexture ||
      !glUniform1i || !glUniform2f || !glEnableVertexAttribArray ||
      !glVertexAttribPointer || !glDisableVertexAttribArray || !glGenBuffers ||
      !glBindBuffer || !glBufferData || !glDeleteBuffers ||
      !glGenFramebuffers || !glBindFramebuffer || !glFramebufferTexture2D ||
      !glCheckFramebufferStatus || !glDeleteFramebuffers) {
    SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                 "OpenGL init failed: one or more required GL functions are "
                 "unavailable");
    KotonohaOpenGLReset();
    return false;
  }

  g_glLoaded = true;
  return true;
}

#undef GL_LOAD_FUNC
#undef GL_LOAD_FUNC_EXT