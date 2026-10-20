#include <Kotonoha/speciais/Upscaling.h>
#include <SDL3/SDL.h>

#if defined(KOTONOHA_HAS_ANIME4K)
#include <Kotonoha/speciais/OpenGL.h>

#include <string>
namespace {

static bool g_anime4kInitialized = false;

// GL Objects
static GLuint g_program = 0;
static GLuint g_fbo = 0;
static GLuint g_vbo = 0;

static GLint g_uTextureLoc = -1;
static GLint g_uTextureSizeLoc = -1;
static GLint g_aPosLoc = -1;
static GLint g_aTexLoc = -1;

struct VertexAttributeState {
  GLint index;
  GLint enabled;
  GLint size;
  GLint type;
  GLint normalized;
  GLint stride;
  GLint buffer;
  void *pointer;
};

static VertexAttributeState SaveVertexAttribute(GLint index) {
  VertexAttributeState state = {index, 0, 0, 0, 0, 0, 0, nullptr};
  if (index < 0) {
    return state;
  }

  glGetVertexAttribiv((GLuint)index, GL_VERTEX_ATTRIB_ARRAY_ENABLED,
                      &state.enabled);
  glGetVertexAttribiv((GLuint)index, GL_VERTEX_ATTRIB_ARRAY_SIZE,
                      &state.size);
  glGetVertexAttribiv((GLuint)index, GL_VERTEX_ATTRIB_ARRAY_TYPE,
                      &state.type);
  glGetVertexAttribiv((GLuint)index, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED,
                      &state.normalized);
  glGetVertexAttribiv((GLuint)index, GL_VERTEX_ATTRIB_ARRAY_STRIDE,
                      &state.stride);
  glGetVertexAttribiv((GLuint)index, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,
                      &state.buffer);
  glGetVertexAttribPointerv((GLuint)index, GL_VERTEX_ATTRIB_ARRAY_POINTER,
                            &state.pointer);
  return state;
}

static void RestoreVertexAttribute(const VertexAttributeState &state) {
  if (state.index < 0) {
    return;
  }

  glBindBuffer(GL_ARRAY_BUFFER, (GLuint)state.buffer);
  glVertexAttribPointer((GLuint)state.index, state.size, (GLenum)state.type,
                        (GLboolean)state.normalized, state.stride,
                        state.pointer);
  if (state.enabled) {
    glEnableVertexAttribArray((GLuint)state.index);
  } else {
    glDisableVertexAttribArray((GLuint)state.index);
  }
}

static std::string LoadShaderSourceFromAssets(const char *filename) {
  std::string path = std::string("assets/Anime4K/") + filename;
  size_t dataSize = 0;
  void *buffer = SDL_LoadFile(path.c_str(), &dataSize);

  if (buffer == nullptr) {
    const char *basePath = SDL_GetBasePath();
    if (basePath != nullptr) {
      path = std::string(basePath) + "assets/Anime4K/" + filename;
      buffer = SDL_LoadFile(path.c_str(), &dataSize);
    }
  }

  if (buffer == nullptr) {
    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                "Anime4K: Could not load shader file '%s' from assets",
                filename);
    return "";
  }

  std::string source(static_cast<const char *>(buffer), dataSize);
  SDL_free(buffer);

  return source;
}

static GLuint CompileShader(GLenum type, const char *source) {
  GLuint shader = glCreateShader(type);
  if (!shader)
    return 0;

  glShaderSource(shader, 1, &source, NULL);
  glCompileShader(shader);

  GLint compiled = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (!compiled) {
    GLint infoLen = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
    if (infoLen > 1) {
      char *infoLog = (char *)SDL_malloc(infoLen);
      glGetShaderInfoLog(shader, infoLen, NULL, infoLog);
      SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                   "Anime4K Shader compilation error: %s", infoLog);
      SDL_free(infoLog);
    }
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

static std::string PrepareShaderSource(std::string source, bool isGLES) {
  if (isGLES) {
    return source;
  }

  const std::string esVersion = "#version 100";
  const size_t versionPosition = source.find(esVersion);
  if (versionPosition != std::string::npos) {
    source.replace(versionPosition, esVersion.size(), "#version 110");
  }

  const std::string precision = "precision mediump float;";
  const size_t precisionPosition = source.find(precision);
  if (precisionPosition != std::string::npos) {
    source.erase(precisionPosition, precision.size());
  }
  return source;
}

static bool InitGLPipeline(SDL_Renderer *renderer) {
  if (g_anime4kInitialized)
    return true;

  if (!KotonohaOpenGLInit()) {
    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                "Anime4K requires a current OpenGL renderer context.");
    return false;
  }

  const std::string vertexSource =
      LoadShaderSourceFromAssets("Anime4K_Vertex.glsl");
  const std::string fragSource =
      LoadShaderSourceFromAssets("Anime4K_ModeA_A.glsl");
  if (vertexSource.empty() || fragSource.empty()) {
    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                "Anime4K shader files are missing from assets/Anime4K.");
    return false;
  }

  const char *rendererName =
      renderer != nullptr ? SDL_GetRendererName(renderer) : nullptr;
  const bool isGLES =
      rendererName != nullptr &&
      (SDL_strcasestr(rendererName, "gles") != nullptr ||
       SDL_strcasestr(rendererName, "opengles") != nullptr);
  const std::string preparedVertex = PrepareShaderSource(vertexSource, isGLES);
  const std::string preparedFragment = PrepareShaderSource(fragSource, isGLES);
  GLuint vertShader = CompileShader(GL_VERTEX_SHADER, preparedVertex.c_str());
  GLuint fragShader =
      CompileShader(GL_FRAGMENT_SHADER, preparedFragment.c_str());

  if (!vertShader || !fragShader) {
    if (vertShader)
      glDeleteShader(vertShader);
    if (fragShader)
      glDeleteShader(fragShader);
    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                "Anime4K shader compilation failed; disabling Anime4K scaler.");
    return false;
  }

  g_program = glCreateProgram();
  if (!g_program) {
    glDeleteShader(vertShader);
    glDeleteShader(fragShader);
    return false;
  }

  glAttachShader(g_program, vertShader);
  glAttachShader(g_program, fragShader);
  glLinkProgram(g_program);

  GLint linked = 0;
  glGetProgramiv(g_program, GL_LINK_STATUS, &linked);
  if (!linked) {
    GLint infoLen = 0;
    glGetProgramiv(g_program, GL_INFO_LOG_LENGTH, &infoLen);
    if (infoLen > 1) {
      char *infoLog = (char *)SDL_malloc(infoLen);
      glGetProgramInfoLog(g_program, infoLen, NULL, infoLog);
      SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Anime4K Program link error: %s",
                   infoLog);
      SDL_free(infoLog);
    }
    glDeleteProgram(g_program);
    g_program = 0;
    glDeleteShader(vertShader);
    glDeleteShader(fragShader);
    return false;
  }

  glDeleteShader(vertShader);
  glDeleteShader(fragShader);

  g_uTextureLoc = glGetUniformLocation(g_program, "uTexture");
  g_uTextureSizeLoc = glGetUniformLocation(g_program, "uTextureSize");
  g_aPosLoc = glGetAttribLocation(g_program, "aPos");
  g_aTexLoc = glGetAttribLocation(g_program, "aTex");

  glGenFramebuffers(1, &g_fbo);

  static const float quadData[] = {
      // x,     y,    s,    t
      -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, -1.0f, 1.0f, 0.0f,
      -1.0f, 1.0f,  0.0f, 1.0f, 1.0f, 1.0f,  1.0f, 1.0f,
  };

  glGenBuffers(1, &g_vbo);
  glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quadData), quadData, GL_STATIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  g_anime4kInitialized = true;
  SDL_Log("Anime4K OpenGL/GLES Upscaler initialized successfully from "
          "assets/Anime4K/");
  return true;
}

} // namespace

extern "C" {

bool KotonohaAnime4KInit(SDL_Renderer *renderer) {
  return InitGLPipeline(renderer);
}

void KotonohaAnime4KShutdown(void) {
  if (!g_anime4kInitialized)
    return;

  if (g_program) {
    glDeleteProgram(g_program);
    g_program = 0;
  }
  if (g_fbo) {
    glDeleteFramebuffers(1, &g_fbo);
    g_fbo = 0;
  }
  if (g_vbo) {
    glDeleteBuffers(1, &g_vbo);
    g_vbo = 0;
  }

  g_anime4kInitialized = false;
}

bool KotonohaAnime4KUpscaleSDLTextureInto(SDL_Renderer *renderer,
                                          SDL_Texture *inputTexture,
                                          SDL_Texture *outputTexture) {
  if (renderer == NULL || inputTexture == NULL || outputTexture == NULL) {
    return false;
  }

  SDL_Texture *previousTarget = SDL_GetRenderTarget(renderer);
  auto fallbackCopy = [&]() {
    if (!SDL_SetRenderTarget(renderer, outputTexture)) {
      SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                   "Anime4K fallback could not activate output texture: %s",
                   SDL_GetError());
      return false;
    }
    const bool copied =
        SDL_RenderTexture(renderer, inputTexture, NULL, NULL);
    const bool restored = SDL_SetRenderTarget(renderer, previousTarget);
    if (!copied || !restored) {
      SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                   "Anime4K fallback render failed: %s", SDL_GetError());
    }
    return copied && restored;
  };

  if (!g_anime4kInitialized) {
    if (!InitGLPipeline(renderer)) {
      return fallbackCopy();
    }
  }

  SDL_PropertiesID inProps = SDL_GetTextureProperties(inputTexture);
  SDL_PropertiesID outProps = SDL_GetTextureProperties(outputTexture);

  GLuint inTex = (GLuint)SDL_GetNumberProperty(
      inProps, SDL_PROP_TEXTURE_OPENGL_TEXTURE_NUMBER, 0);
  if (inTex == 0) {
    inTex = (GLuint)SDL_GetNumberProperty(
        inProps, SDL_PROP_TEXTURE_OPENGLES2_TEXTURE_NUMBER, 0);
  }

  GLuint outTex = (GLuint)SDL_GetNumberProperty(
      outProps, SDL_PROP_TEXTURE_OPENGL_TEXTURE_NUMBER, 0);
  if (outTex == 0) {
    outTex = (GLuint)SDL_GetNumberProperty(
        outProps, SDL_PROP_TEXTURE_OPENGLES2_TEXTURE_NUMBER, 0);
  }

  if (inTex == 0 || outTex == 0) {
    return fallbackCopy();
  }

  GLint prevFBO = 0;
  GLint prevViewport[4] = {0};
  GLint prevProgram = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
  glGetIntegerv(GL_VIEWPORT, prevViewport);
  glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);

  glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         outTex, 0);

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    return fallbackCopy();
  }

  float outputWidth = 0.0f;
  float outputHeight = 0.0f;
  float inputWidth = 0.0f;
  float inputHeight = 0.0f;
  if (!SDL_GetTextureSize(outputTexture, &outputWidth, &outputHeight) ||
      !SDL_GetTextureSize(inputTexture, &inputWidth, &inputHeight)) {
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2],
               prevViewport[3]);
    glUseProgram(prevProgram);
    return fallbackCopy();
  }
  const VertexAttributeState prevPosAttrib = SaveVertexAttribute(g_aPosLoc);
  const VertexAttributeState prevTexAttrib =
      g_aTexLoc == g_aPosLoc ? prevPosAttrib : SaveVertexAttribute(g_aTexLoc);
  GLint prevArrayBuffer = 0;
  GLint prevActiveTexture = GL_TEXTURE0;
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuffer);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexture);
  kotonoha_glActiveTexture(GL_TEXTURE0);
  GLint prevTexture = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexture);
  const GLboolean prevBlend = glIsEnabled(GL_BLEND);
  const GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);
  const GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
  const GLboolean prevStencil = glIsEnabled(GL_STENCIL_TEST);
  const GLboolean prevCull = glIsEnabled(GL_CULL_FACE);
  glViewport(0, 0, (GLsizei)outputWidth, (GLsizei)outputHeight);
  glDisable(GL_BLEND);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_STENCIL_TEST);
  glDisable(GL_CULL_FACE);

  glUseProgram(g_program);

  kotonoha_glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, inTex);
  GLint prevMinFilter = GL_LINEAR;
  GLint prevMagFilter = GL_LINEAR;
  glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &prevMinFilter);
  glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &prevMagFilter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  glUniform1i(g_uTextureLoc, 0);
  glUniform2f(g_uTextureSizeLoc, inputWidth, inputHeight);

  glBindBuffer(GL_ARRAY_BUFFER, g_vbo);

  if (g_aPosLoc >= 0) {
    glEnableVertexAttribArray(g_aPosLoc);
    glVertexAttribPointer(g_aPosLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void *)0);
  }
  if (g_aTexLoc >= 0) {
    glEnableVertexAttribArray(g_aTexLoc);
    glVertexAttribPointer(g_aTexLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void *)(2 * sizeof(float)));
  }

  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  RestoreVertexAttribute(prevPosAttrib);
  if (g_aTexLoc != g_aPosLoc) {
    RestoreVertexAttribute(prevTexAttrib);
  }
  glBindBuffer(GL_ARRAY_BUFFER, (GLuint)prevArrayBuffer);
  glBindTexture(GL_TEXTURE_2D, inTex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, prevMinFilter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, prevMagFilter);
  glBindTexture(GL_TEXTURE_2D, (GLuint)prevTexture);
  kotonoha_glActiveTexture((GLenum)prevActiveTexture);
  glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
  glViewport(prevViewport[0], prevViewport[1], prevViewport[2],
             prevViewport[3]);
  glUseProgram(prevProgram);
  (prevBlend ? glEnable : glDisable)(GL_BLEND);
  (prevScissor ? glEnable : glDisable)(GL_SCISSOR_TEST);
  (prevDepth ? glEnable : glDisable)(GL_DEPTH_TEST);
  (prevStencil ? glEnable : glDisable)(GL_STENCIL_TEST);
  (prevCull ? glEnable : glDisable)(GL_CULL_FACE);

  return true;
}

} // extern "C"
#endif