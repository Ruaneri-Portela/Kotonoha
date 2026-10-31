#include <Kotonoha/core/FileSystem.hpp>
#include <Kotonoha/core/Log.hpp>
#include <Kotonoha/core/OpenGL.hpp>
#include <Kotonoha/media/upscaling/OpenGLUpscaler.hpp>

#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef GL_RGBA16F
#define GL_RGBA16F 0x881A
#endif

// Executes mpv/libplacebo-style fragment shader chains over floating-point FBOs.
// The backend is intentionally model-agnostic: Anime4K remains available as a
// legacy preset and ArtCNN uses the same runner through its LUMA hook.
namespace Kotonoha {
namespace {

enum Kotonoha_UpscalerPreset {
  KOTONOHA_UPSCALER_PRESET_LEGACY_ANIME4K,
  KOTONOHA_UPSCALER_PRESET_ARTCNN_C4F16,
  KOTONOHA_UPSCALER_PRESET_ARTCNN_C4F16_DS,
  KOTONOHA_UPSCALER_PRESET_ARTCNN_C4F32,
  KOTONOHA_UPSCALER_PRESET_COUNT
};

struct BindRef {
  std::string name;
  GLint raw = -1;
  GLint size = -1;
  GLint pt = -1;
};

struct Pass {
  std::string hook;
  std::string save;
  std::vector<std::string> binds;
  std::vector<std::string> width;
  std::vector<std::string> height;
  std::vector<std::string> when;
  std::string body;
  bool hasDirectives = false;
  GLuint program = 0;
  GLint aPos = -1;
  GLint aTex = -1;
  std::vector<BindRef> refs;
};

struct TexRef {
  GLuint id = 0;
  int w = 0;
  int h = 0;
  bool owned = false;
};

struct PoolTex {
  GLuint id;
  int w;
  int h;
  bool inUse;
  Uint64 lastUsed;
};

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

constexpr int kSavedAttribs = 4;
constexpr int kMaxUnits = 16;

bool g_initialized = false;
bool g_initFailed = false;
Uint64 g_nextInitRetryMs = 0;
SDL_GLContext g_context = nullptr;
std::string g_filesSpec;
bool g_filesDirty = false;
bool g_isGLES = false;
Kotonoha_UpscalerPreset g_preset = KOTONOHA_UPSCALER_PRESET_LEGACY_ANIME4K;
std::vector<Pass> g_passes;
GLuint g_blitProgram = 0;
GLint g_blitMainLoc = -1;
GLint g_blitOrigLoc = -1;
GLint g_blitLumaLoc = -1;
GLint g_blitPos = -1;
GLint g_blitTex = -1;
GLuint g_fbo = 0;
GLuint g_vbo = 0;
std::vector<PoolTex> g_pool;
Uint64 g_poolUseSerial = 0;

constexpr Uint64 kRgba16fBytesPerPixel = 8;
constexpr Uint64 kPoolBudgetBytes = 256ull * 1024ull * 1024ull;

Uint64 PoolTextureBytes(const PoolTex &t) {
  return static_cast<Uint64>(std::max(t.w, 0)) *
         static_cast<Uint64>(std::max(t.h, 0)) * kRgba16fBytesPerPixel;
}

void TrimPoolToBudget() {
  Uint64 total = 0;
  for (const PoolTex &t : g_pool) total += PoolTextureBytes(t);
  while (total > kPoolBudgetBytes) {
    auto victim = g_pool.end();
    for (auto it = g_pool.begin(); it != g_pool.end(); ++it) {
      if (it->inUse) continue;
      if (victim == g_pool.end() || it->lastUsed < victim->lastUsed) victim = it;
    }
    if (victim == g_pool.end()) break;
    total -= PoolTextureBytes(*victim);
    glDeleteTextures(1, &victim->id);
    g_pool.erase(victim);
  }
}

const char *PresetLogName(Kotonoha_UpscalerPreset preset) {
  switch (preset) {
  case KOTONOHA_UPSCALER_PRESET_ARTCNN_C4F16:
    return "ArtCNN C4F16";
  case KOTONOHA_UPSCALER_PRESET_ARTCNN_C4F16_DS:
    return "ArtCNN C4F16 DS";
  case KOTONOHA_UPSCALER_PRESET_ARTCNN_C4F32:
    return "ArtCNN C4F32";
  case KOTONOHA_UPSCALER_PRESET_LEGACY_ANIME4K:
  default:
    return "Anime4K B-S";
  }
}

VertexAttributeState SaveVertexAttribute(GLint index) {
  VertexAttributeState state = {index, 0, 0, 0, 0, 0, 0, nullptr};
  glGetVertexAttribiv((GLuint)index, GL_VERTEX_ATTRIB_ARRAY_ENABLED,
                      &state.enabled);
  glGetVertexAttribiv((GLuint)index, GL_VERTEX_ATTRIB_ARRAY_SIZE, &state.size);
  glGetVertexAttribiv((GLuint)index, GL_VERTEX_ATTRIB_ARRAY_TYPE, &state.type);
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

void RestoreVertexAttribute(const VertexAttributeState &state) {
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

std::string LoadAsset(const std::string &filename) {
  const char *assetFolder =
      g_preset == KOTONOHA_UPSCALER_PRESET_LEGACY_ANIME4K ? "Anime4K"
                                                          : "ArtCNN";
  const std::filesystem::path supplied(filename);
  std::string path = supplied.has_parent_path()
                         ? supplied.string()
                         : (Kotonoha::engineAssetDirectory() / assetFolder / supplied).string();
  size_t size = 0;
  void *buffer = SDL_LoadFile(path.c_str(), &size);
  if (buffer == nullptr) {
    path = (Kotonoha::engineAssetDirectory() / assetFolder / filename)
               .string();
    buffer = SDL_LoadFile(path.c_str(), &size);
  }
  if (buffer == nullptr) {
    SDL_LogWarn(LogUpscaler, "MPVCompatibleUpscaler: could not load shader '%s'",
              filename.c_str());
    return "";
  }
  std::string text(static_cast<const char *>(buffer), size);
  SDL_free(buffer);
  return text;
}

std::vector<std::string> Tokenize(const std::string &text) {
  std::vector<std::string> tokens;
  size_t i = 0;
  while (i < text.size()) {
    while (i < text.size() && SDL_isspace((unsigned char)text[i]))
      i++;
    size_t start = i;
    while (i < text.size() && !SDL_isspace((unsigned char)text[i]))
      i++;
    if (i > start)
      tokens.push_back(text.substr(start, i - start));
  }
  return tokens;
}

void ParsePasses(const std::string &source, std::vector<Pass> &out) {
  Pass cur;
  auto flush = [&]() {
    if (cur.hasDirectives && !cur.body.empty())
      out.push_back(cur);
    cur = Pass();
  };

  size_t pos = 0;
  while (pos <= source.size()) {
    size_t end = source.find('\n', pos);
    if (end == std::string::npos)
      end = source.size();
    std::string line = source.substr(pos, end - pos);
    pos = end + 1;
    if (!line.empty() && line.back() == '\r')
      line.pop_back();

    if (line.compare(0, 3, "//!") == 0) {
      if (!cur.body.empty())
        flush();
      cur.hasDirectives = true;
      std::vector<std::string> t = Tokenize(line.substr(3));
      if (t.empty())
        continue;
      const std::string &key = t[0];
      std::vector<std::string> args(t.begin() + 1, t.end());
      if (key == "HOOK" && !args.empty() && cur.hook.empty())
        cur.hook = args[0];
      else if (key == "BIND" && !args.empty())
        cur.binds.push_back(args[0]);
      else if (key == "SAVE" && !args.empty())
        cur.save = args[0];
      else if (key == "WHEN")
        cur.when = args;
      else if (key == "WIDTH")
        cur.width = args;
      else if (key == "HEIGHT")
        cur.height = args;
    } else if (cur.hasDirectives) {
      cur.body += line;
      cur.body += '\n';
    }
  }
  flush();
}

struct SizeContext {
  int outW = 0;
  int outH = 0;
  int nativeW = 0;
  int nativeH = 0;
};

// Avalia expressões RPN do formato libplacebo (WIDTH/HEIGHT/WHEN).
bool EvalRpn(const std::vector<std::string> &rpn, const TexRef &mainTex,
             const std::map<std::string, TexRef> &reg, const SizeContext &ctx,
             float &result) {
  if (rpn.empty())
    return false;
  std::vector<float> stack;
  for (const std::string &tok : rpn) {
    const bool numeric = SDL_isdigit((unsigned char)tok[0]) || tok[0] == '.' ||
                         (tok.size() > 1 && tok[0] == '-');
    const size_t dot = tok.find('.');
    if (!numeric && dot != std::string::npos) {
      const std::string name = tok.substr(0, dot);
      const bool isW = tok.substr(dot + 1) == "w";
      if (name == "OUTPUT") {
        stack.push_back((float)(isW ? ctx.outW : ctx.outH));
      } else if (name == "NATIVE") {
        stack.push_back((float)(isW ? ctx.nativeW : ctx.nativeH));
      } else if (name == "MAIN" || name == "HOOKED") {
        stack.push_back((float)(isW ? mainTex.w : mainTex.h));
      } else if (name == "LUMA") {
        stack.push_back((float)(isW ? ctx.nativeW : ctx.nativeH));
      } else {
        auto it = reg.find(name);
        if (it == reg.end())
          return false;
        stack.push_back((float)(isW ? it->second.w : it->second.h));
      }
    } else if (tok == "*" || tok == "/" || tok == "+" || tok == "-" ||
               tok == "<" || tok == ">") {
      if (stack.size() < 2)
        return false;
      const float b = stack.back();
      stack.pop_back();
      const float a = stack.back();
      stack.pop_back();
      stack.push_back(tok == "*"   ? a * b
                      : tok == "/" ? (b != 0.0f ? a / b : 0.0f)
                      : tok == "+" ? a + b
                      : tok == "-" ? a - b
                      : tok == "<" ? (a < b ? 1.0f : 0.0f)
                                   : (a > b ? 1.0f : 0.0f));
    } else {
      stack.push_back((float)SDL_atof(tok.c_str()));
    }
  }
  if (stack.size() != 1)
    return false;
  result = stack.back();
  return true;
}

int EvalSize(const std::vector<std::string> &rpn, const TexRef &mainTex,
             const std::map<std::string, TexRef> &reg, const SizeContext &ctx) {
  float value = 0.0f;
  if (!EvalRpn(rpn, mainTex, reg, ctx, value))
    return -1;
  return std::max(1, (int)(value + 0.5f));
}

GLuint CompileShader(GLenum type, const std::string &source) {
  GLuint shader = glCreateShader(type);
  if (!shader)
    return 0;
  const char *src = source.c_str();
  glShaderSource(shader, 1, &src, NULL);
  glCompileShader(shader);

  GLint compiled = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (!compiled) {
    GLint len = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
    if (len > 1) {
      std::string log((size_t)len, '\0');
      glGetShaderInfoLog(shader, len, NULL, &log[0]);
      SDL_LogError(LogUpscaler, "MPVCompatibleUpscaler shader error: %s", log.c_str());
    }
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

GLuint LinkProgram(GLuint vertex, GLuint fragment) {
  GLuint program = glCreateProgram();
  if (!program)
    return 0;
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  glLinkProgram(program);
  GLint linked = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  if (!linked) {
    GLint len = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &len);
    if (len > 1) {
      std::string log((size_t)len, '\0');
      glGetProgramInfoLog(program, len, NULL, &log[0]);
      SDL_LogError(LogUpscaler, "MPVCompatibleUpscaler link error: %s", log.c_str());
    }
    glDeleteProgram(program);
    return 0;
  }
  return program;
}

// macOS só expõe GLSL 1.20 no contexto legado do renderer SDL.
bool UseLegacyGLSL() {
#if defined(__APPLE__)
  return !g_isGLES;
#else
  return false;
#endif
}

std::string Header() {
  if (UseLegacyGLSL())
    return "#version 120\n"
           "#define texture texture2D\n";
  return g_isGLES ? "#version 300 es\nprecision highp float;\nprecision highp "
                    "sampler2D;\n"
                  : "#version 130\n";
}

std::string BuildFragment(Pass &pass) {
  std::vector<std::string> names = pass.binds;
  for (const char *implicit : {"MAIN", "HOOKED"}) {
    if (std::find(names.begin(), names.end(), implicit) == names.end())
      names.push_back(implicit);
  }

  std::string src = Header();
  src += UseLegacyGLSL()
             ? "varying vec2 vTex;\n#define fragColor gl_FragColor\n"
             : "in vec2 vTex;\nout vec4 fragColor;\n";
  pass.refs.clear();
  for (const std::string &n : names) {
    BindRef ref;
    ref.name = n;
    pass.refs.push_back(ref);
    src += "uniform sampler2D " + n + "_raw;\n";
    src += "uniform vec2 " + n + "_size;\n";
    src += "uniform vec2 " + n + "_pt;\n";
    src += "#define " + n + "_pos vTex\n";
    if (n == "LUMA") {
      // ArtCNN's fragment models operate on BT.709 luma. Keep the source RGB
      // untouched and expose only luma through the mpv LUMA binding.
      src += "#define LUMA_tex(pos) vec4(dot(texture(LUMA_raw, pos).rgb, "
             "vec3(0.2126, 0.7152, 0.0722)))\n";
    } else {
      src += "#define " + n + "_tex(pos) texture(" + n + "_raw, pos)\n";
    }
    src += "#define " + n + "_texOff(off) " + n + "_tex(" + n + "_pos + " + n +
           "_pt * vec2(off))\n";
  }
  src += pass.body;
  src += "\nvoid main() { fragColor = hook(); }\n";
  return src;
}

std::string VertexSource() {
  if (UseLegacyGLSL())
    return Header() +
           "attribute vec2 aPos;\nattribute vec2 aTex;\nvarying vec2 vTex;\n"
           "void main() { vTex = aTex; gl_Position = vec4(aPos, 0.0, 1.0); }\n";
  return Header() +
         "in vec2 aPos;\nin vec2 aTex;\nout vec2 vTex;\n"
         "void main() { vTex = aTex; gl_Position = vec4(aPos, 0.0, 1.0); }\n";
}

std::string BlitSource() {
  return Header() +
         (UseLegacyGLSL()
              ? "varying vec2 vTex;\n#define fragColor gl_FragColor\n"
              : "in vec2 vTex;\nout vec4 fragColor;\n") +
         "uniform sampler2D uMain;\nuniform sampler2D uOrig;\n"
         "uniform int uLumaOutput;\n"
         "void main() {\n"
         "  vec4 orig = texture(uOrig, vTex);\n"
         "  vec3 rgb;\n"
         "  if (uLumaOutput != 0) {\n"
         "    float oldY = dot(orig.rgb, vec3(0.2126, 0.7152, 0.0722));\n"
         "    float newY = texture(uMain, vTex).r;\n"
         "    rgb = clamp(orig.rgb + vec3(newY - oldY), 0.0, 1.0);\n"
         "  } else {\n"
         "    rgb = clamp(texture(uMain, vTex).rgb, 0.0, 1.0);\n"
         "  }\n"
         "  fragColor = vec4(rgb, orig.a);\n"
         "}\n";
}

void ForgetPipelineState() {
  g_passes.clear();
  g_blitProgram = 0;
  g_blitMainLoc = -1;
  g_blitOrigLoc = -1;
  g_blitLumaLoc = -1;
  g_blitPos = -1;
  g_blitTex = -1;
  g_pool.clear();
  g_fbo = 0;
  g_vbo = 0;
  g_initialized = false;
  g_context = nullptr;
}

void DestroyPipeline() {
  for (Pass &p : g_passes) {
    if (p.program)
      glDeleteProgram(p.program);
  }
  g_passes.clear();
  if (g_blitProgram) {
    glDeleteProgram(g_blitProgram);
    g_blitProgram = 0;
  }
  for (PoolTex &t : g_pool)
    glDeleteTextures(1, &t.id);
  g_pool.clear();
  if (g_fbo) {
    glDeleteFramebuffers(1, &g_fbo);
    g_fbo = 0;
  }
  if (g_vbo) {
    glDeleteBuffers(1, &g_vbo);
    g_vbo = 0;
  }
  g_initialized = false;
  g_context = nullptr;
}

bool InitGLPipeline(SDL_Renderer *renderer) {
  const SDL_GLContext currentContext = SDL_GL_GetCurrentContext();
  if (g_initialized) {
    if (currentContext != nullptr && currentContext == g_context)
      return true;

    // GL object names are context-owned. Never delete the old pipeline while
    // another context is current: the same numeric names can refer to unrelated
    // objects there. Rebuild against the current context instead.
    SDL_LogWarn(LogUpscaler, "MPVCompatibleUpscaler OpenGL context changed; rebuilding pipeline.");
    ForgetPipelineState();
  }

  if (!KotonohaOpenGLInit()) {
    SDL_LogWarn(LogUpscaler, "MPVCompatibleUpscaler requires a current OpenGL renderer context.");
    return false;
  }

  const char *rendererName =
      renderer != nullptr ? SDL_GetRendererName(renderer) : nullptr;
  g_isGLES = rendererName != nullptr &&
             SDL_strcasestr(rendererName, "gles") != nullptr;

  std::vector<std::string> files = Kotonoha::UpscaleShaderFiles(g_filesSpec);
  if (files.empty()) {
    if (g_preset == KOTONOHA_UPSCALER_PRESET_ARTCNN_C4F16) {
      files.push_back("ArtCNN_C4F16.glsl");
    } else if (g_preset == KOTONOHA_UPSCALER_PRESET_ARTCNN_C4F16_DS) {
      files.push_back("ArtCNN_C4F16_DS.glsl");
    } else if (g_preset == KOTONOHA_UPSCALER_PRESET_ARTCNN_C4F32) {
      files.push_back("ArtCNN_C4F32.glsl");
    } else {
      // Legacy Anime4K remains the compatibility/fallback preset. Keep its
      // previous Mode B + S chain exactly as shipped before preset support.
      files.push_back("Anime4K_Clamp_Highlights.glsl");
      files.push_back("Anime4K_Restore_CNN_Soft_S.glsl");
      files.push_back("Anime4K_Upscale_CNN_x2_S.glsl");
    }
  }

  std::vector<Pass> mainPasses;
  std::vector<Pass> kernelPasses;
  for (const std::string &file : files) {
    const std::string source = LoadAsset(file);
    if (source.empty())
      return false;
    std::vector<Pass> parsed;
    ParsePasses(source, parsed);
    for (Pass &p : parsed) {
      if (p.hook == "MAIN" || p.hook == "LUMA")
        mainPasses.push_back(p);
      else if (p.hook == "PREKERNEL")
        kernelPasses.push_back(p);
    }
  }
  if (mainPasses.empty() && kernelPasses.empty()) {
    SDL_LogError(LogUpscaler, "MPVCompatibleUpscaler: no usable shader passes were loaded.");
    return false;
  }
  g_passes = mainPasses;
  g_passes.insert(g_passes.end(), kernelPasses.begin(), kernelPasses.end());

  GLuint vertex = CompileShader(GL_VERTEX_SHADER, VertexSource());
  if (!vertex) {
    g_passes.clear();
    return false;
  }

  bool ok = true;
  for (Pass &p : g_passes) {
    GLuint frag = CompileShader(GL_FRAGMENT_SHADER, BuildFragment(p));
    if (!frag) {
      ok = false;
      break;
    }
    p.program = LinkProgram(vertex, frag);
    glDeleteShader(frag);
    if (!p.program) {
      ok = false;
      break;
    }
    p.aPos = glGetAttribLocation(p.program, "aPos");
    p.aTex = glGetAttribLocation(p.program, "aTex");
    if (p.aPos < 0 || p.aPos >= kSavedAttribs || p.aTex < 0 ||
        p.aTex >= kSavedAttribs) {
      ok = false;
      break;
    }
    for (BindRef &r : p.refs) {
      r.raw = glGetUniformLocation(p.program, (r.name + "_raw").c_str());
      r.size = glGetUniformLocation(p.program, (r.name + "_size").c_str());
      r.pt = glGetUniformLocation(p.program, (r.name + "_pt").c_str());
    }
  }

  if (ok) {
    GLuint blitFrag = CompileShader(GL_FRAGMENT_SHADER, BlitSource());
    if (blitFrag) {
      g_blitProgram = LinkProgram(vertex, blitFrag);
      glDeleteShader(blitFrag);
    }
    if (g_blitProgram) {
      g_blitMainLoc = glGetUniformLocation(g_blitProgram, "uMain");
      g_blitOrigLoc = glGetUniformLocation(g_blitProgram, "uOrig");
      g_blitLumaLoc = glGetUniformLocation(g_blitProgram, "uLumaOutput");
      g_blitPos = glGetAttribLocation(g_blitProgram, "aPos");
      g_blitTex = glGetAttribLocation(g_blitProgram, "aTex");
      ok = g_blitPos >= 0 && g_blitPos < kSavedAttribs && g_blitTex >= 0 &&
           g_blitTex < kSavedAttribs;
    } else {
      ok = false;
    }
  }
  glDeleteShader(vertex);

  if (!ok) {
    SDL_LogWarn(LogUpscaler, "MPVCompatibleUpscaler pipeline setup failed; disabling MPVCompatibleUpscaler scaler.");
    DestroyPipeline();
    return false;
  }

  glGenFramebuffers(1, &g_fbo);

  static const float quadData[] = {
      -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, -1.0f, 1.0f, 0.0f,
      -1.0f, 1.0f,  0.0f, 1.0f, 1.0f, 1.0f,  1.0f, 1.0f,
  };
  GLint prevBuffer = 0;
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevBuffer);
  glGenBuffers(1, &g_vbo);
  glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quadData), quadData, GL_STATIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, (GLuint)prevBuffer);

  g_context = SDL_GL_GetCurrentContext();
  g_initialized = g_context != nullptr;
  if (!g_initialized) {
    ForgetPipelineState();
    return false;
  }
  SDL_LogInfo(LogUpscaler, "MPVCompatibleUpscaler preset '%s' initialized (%d passes)",
            PresetLogName(g_preset), (int)g_passes.size());
  return true;
}

GLuint AcquireTexture(int w, int h) {
  const Uint64 useSerial = ++g_poolUseSerial;
  for (PoolTex &t : g_pool) {
    if (!t.inUse && t.w == w && t.h == h) {
      t.inUse = true;
      t.lastUsed = useSerial;
      return t.id;
    }
  }
  GLuint id = 0;
  glGenTextures(1, &id);
  glBindTexture(GL_TEXTURE_2D, id);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  g_pool.push_back({id, w, h, true, useSerial});
  return id;
}

void ReleaseTexture(GLuint id) {
  for (PoolTex &t : g_pool) {
    if (t.id == id)
      t.inUse = false;
  }
}

void BindQuad(GLint pos, GLint tex) {
  glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
  glEnableVertexAttribArray((GLuint)pos);
  glVertexAttribPointer((GLuint)pos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                        (void *)0);
  glEnableVertexAttribArray((GLuint)tex);
  glVertexAttribPointer((GLuint)tex, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                        (void *)(2 * sizeof(float)));
}

bool AttachTarget(GLuint tex) {
  glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         tex, 0);
  return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

void DebugProbe(const char *label, GLuint tex, int w, int h) {
  static int budget = 400;
  if (budget <= 0 || SDL_getenv("KOTONOHA_MPV_UPSCALER_DEBUG") == nullptr)
    return;
  budget--;
  GLint prevFbo = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
  if (AttachTarget(tex)) {
    float px[4] = {0, 0, 0, 0};
    glReadPixels(w / 3, h / 3, 1, 1, GL_RGBA, GL_FLOAT, px);
    SDL_LogInfo(LogUpscaler, "MPVCompatibleUpscaler probe %-28s %4dx%-4d rgba=%.3f %.3f %.3f %.3f",
              label, w, h, px[0], px[1], px[2], px[3]);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
}

bool RunChain(GLuint inTex, int inW, int inH, GLuint outTex, int outW,
              int outH) {
  DebugProbe("input", inTex, inW, inH);
  std::map<std::string, TexRef> reg;
  TexRef mainTex;
  mainTex.id = inTex;
  mainTex.w = inW;
  mainTex.h = inH;
  bool mainIsLuma = false;

  std::map<std::string, int> remainingUses;
  for (const Pass &pass : g_passes) {
    for (const BindRef &ref : pass.refs) {
      if (ref.name == "MAIN" || ref.name == "HOOKED" ||
          ref.name == "NATIVE" || ref.name == "LUMA") {
        continue;
      }
      ++remainingUses[ref.name];
    }
  }

  auto cleanup = [&]() {
    for (PoolTex &t : g_pool)
      t.inUse = false;
    TrimPoolToBudget();
  };

  SizeContext ctx;
  ctx.outW = outW;
  ctx.outH = outH;
  ctx.nativeW = inW;
  ctx.nativeH = inH;

  for (const Pass &pass : g_passes) {
    if (!pass.when.empty()) {
      float condition = 1.0f;
      if (EvalRpn(pass.when, mainTex, reg, ctx, condition) && condition == 0.0f)
        continue;
    }
    int w = EvalSize(pass.width, mainTex, reg, ctx);
    int h = EvalSize(pass.height, mainTex, reg, ctx);
    if (w < 0)
      w = mainTex.w;
    if (h < 0)
      h = mainTex.h;

    std::vector<GLuint> sources;
    for (const BindRef &ref : pass.refs) {
      TexRef src;
      if (ref.name == "MAIN" || ref.name == "HOOKED") {
        src = mainTex;
      } else if (ref.name == "NATIVE" || ref.name == "LUMA") {
        src.id = inTex;
        src.w = inW;
        src.h = inH;
      } else {
        auto it = reg.find(ref.name);
        if (it == reg.end()) {
          cleanup();
          return false;
        }
        src = it->second;
      }
      sources.push_back(src.id);
    }

    GLuint dest = AcquireTexture(w, h);
    if (!AttachTarget(dest) || glGetError() != GL_NO_ERROR) {
      cleanup();
      return false;
    }

    glViewport(0, 0, w, h);
    glUseProgram(pass.program);
    for (size_t i = 0; i < pass.refs.size() && i < (size_t)kMaxUnits; i++) {
      const BindRef &ref = pass.refs[i];
      TexRef src;
      if (ref.name == "MAIN" || ref.name == "HOOKED") {
        src = mainTex;
      } else if (ref.name == "NATIVE" || ref.name == "LUMA") {
        src.id = inTex;
        src.w = inW;
        src.h = inH;
      } else {
        src = reg[ref.name];
      }
      kotonoha_glActiveTexture(GL_TEXTURE0 + (GLenum)i);
      glBindTexture(GL_TEXTURE_2D, sources[i]);
      if (ref.raw >= 0)
        glUniform1i(ref.raw, (GLint)i);
      if (ref.size >= 0)
        glUniform2f(ref.size, (float)src.w, (float)src.h);
      if (ref.pt >= 0)
        glUniform2f(ref.pt, 1.0f / (float)src.w, 1.0f / (float)src.h);
    }
    BindQuad(pass.aPos, pass.aTex);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    DebugProbe(pass.save.empty() ? "MAIN" : pass.save.c_str(), dest, w, h);

    // Saved shader outputs can be very large RGBA16F textures.  Release each
    // saved register as soon as its last consumer has drawn instead of keeping
    // every ArtCNN layer resident until the end of the chain.
    for (const BindRef &ref : pass.refs) {
      if (ref.name == "MAIN" || ref.name == "HOOKED" ||
          ref.name == "NATIVE" || ref.name == "LUMA") {
        continue;
      }
      auto use = remainingUses.find(ref.name);
      if (use == remainingUses.end()) continue;
      --use->second;
      if (use->second <= 0) {
        auto saved = reg.find(ref.name);
        if (saved != reg.end()) {
          if (saved->second.owned) ReleaseTexture(saved->second.id);
          reg.erase(saved);
        }
      }
    }

    const std::string target = pass.save.empty() ? "MAIN" : pass.save;
    TexRef updated;
    updated.id = dest;
    updated.w = w;
    updated.h = h;
    updated.owned = true;
    TexRef *old = target == "MAIN" ? &mainTex : nullptr;
    if (old == nullptr) {
      auto it = reg.find(target);
      if (it != reg.end())
        old = &it->second;
    }
    if (old != nullptr && old->owned)
      ReleaseTexture(old->id);
    if (target == "MAIN") {
      mainTex = updated;
      mainIsLuma = pass.hook == "LUMA";
    } else
      reg[target] = updated;
  }

  if (!AttachTarget(outTex)) {
    cleanup();
    return false;
  }
  glViewport(0, 0, outW, outH);
  glUseProgram(g_blitProgram);
  kotonoha_glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, mainTex.id);
  kotonoha_glActiveTexture(GL_TEXTURE0 + 1);
  glBindTexture(GL_TEXTURE_2D, inTex);
  glUniform1i(g_blitMainLoc, 0);
  glUniform1i(g_blitOrigLoc, 1);
  if (g_blitLumaLoc >= 0)
    glUniform1i(g_blitLumaLoc, mainIsLuma ? 1 : 0);
  BindQuad(g_blitPos, g_blitTex);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  DebugProbe("output", outTex, outW, outH);

  cleanup();
  return true;
}

GLuint TextureNumber(SDL_Texture *texture) {
  SDL_PropertiesID props = SDL_GetTextureProperties(texture);
  GLuint id = (GLuint)SDL_GetNumberProperty(
      props, SDL_PROP_TEXTURE_OPENGL_TEXTURE_NUMBER, 0);
  if (id == 0) {
    id = (GLuint)SDL_GetNumberProperty(
        props, SDL_PROP_TEXTURE_OPENGLES2_TEXTURE_NUMBER, 0);
  }
  return id;
}

void KotonohaMPVUpscalerSetPreset(int preset) {
  Kotonoha_UpscalerPreset next = KOTONOHA_UPSCALER_PRESET_LEGACY_ANIME4K;
  if (preset >= KOTONOHA_UPSCALER_PRESET_LEGACY_ANIME4K &&
      preset < KOTONOHA_UPSCALER_PRESET_COUNT) {
    next = static_cast<Kotonoha_UpscalerPreset>(preset);
  }
  if (next != g_preset) {
    g_preset = next;
    g_filesDirty = true;
  }
}

void KotonohaMPVUpscalerSetFiles(const char *files) {
  const std::string next = files != NULL ? files : "";
  if (next != g_filesSpec) {
    g_filesSpec = next;
    g_filesDirty = true;
  }
}

void KotonohaMPVUpscalerShutdown(void) {
  g_initFailed = false;
  g_nextInitRetryMs = 0;
  g_filesDirty = false;
  if (!g_initialized)
    return;
  if (SDL_GL_GetCurrentContext() == g_context)
    DestroyPipeline();
  else
    ForgetPipelineState();
}

Uint64 KotonohaMPVUpscalerRetryDeadlineMs(void) {
  return g_initFailed ? g_nextInitRetryMs : 0;
}

bool KotonohaMPVUpscalerUpscaleSDLTextureInto(SDL_Renderer *renderer,
                                          SDL_Texture *inputTexture,
                                          SDL_Texture *outputTexture) {
  if (renderer == NULL || inputTexture == NULL || outputTexture == NULL) {
    return false;
  }

  SDL_Texture *previousTarget = SDL_GetRenderTarget(renderer);
  auto fallbackCopy = [&]() {
    SDL_BlendMode previousBlend = SDL_BLENDMODE_BLEND;
    Uint8 previousR = 0, previousG = 0, previousB = 0, previousA = 0;
    float previousModR = 1.0f, previousModG = 1.0f, previousModB = 1.0f;
    float previousModA = 1.0f;
    const bool stateSaved =
        SDL_GetTextureBlendMode(inputTexture, &previousBlend) &&
        SDL_GetTextureColorModFloat(inputTexture, &previousModR, &previousModG,
                                    &previousModB) &&
        SDL_GetTextureAlphaModFloat(inputTexture, &previousModA) &&
        SDL_GetRenderDrawColor(renderer, &previousR, &previousG, &previousB,
                               &previousA);
    if (!stateSaved) {
      return false;
    }
    if (!SDL_SetRenderTarget(renderer, outputTexture)) {
      SDL_LogError(LogUpscaler, "MPVCompatibleUpscaler fallback could not activate output texture: %s",
                 SDL_GetError());
      return false;
    }
    // The input may be a transparent source composite (for example a base
    // image plus an animated overlay patch).  Copy with blending disabled and
    // clear the reused output first; otherwise transparent pixels can blend
    // over the previous upscaled frame and expose stale rectangles/ghosting.
    const bool prepared = SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0) &&
                          SDL_SetTextureColorModFloat(inputTexture, 1.0f, 1.0f,
                                                     1.0f) &&
                          SDL_SetTextureAlphaModFloat(inputTexture, 1.0f) &&
                          SDL_SetTextureBlendMode(inputTexture,
                                                  SDL_BLENDMODE_NONE) &&
                          SDL_RenderClear(renderer);
    const bool copied =
        prepared && SDL_RenderTexture(renderer, inputTexture, NULL, NULL);
    const bool blendRestored =
        SDL_SetTextureBlendMode(inputTexture, previousBlend);
    const bool modRestored =
        SDL_SetTextureColorModFloat(inputTexture, previousModR, previousModG,
                                    previousModB) &&
        SDL_SetTextureAlphaModFloat(inputTexture, previousModA);
    const bool colorRestored = SDL_SetRenderDrawColor(
        renderer, previousR, previousG, previousB, previousA);
    const bool restored = SDL_SetRenderTarget(renderer, previousTarget);
    if (!prepared || !copied || !blendRestored || !modRestored ||
        !colorRestored || !restored) {
      SDL_LogError(LogUpscaler, "MPVCompatibleUpscaler fallback render failed: %s",
                 SDL_GetError());
    }
    return prepared && copied && blendRestored && modRestored && colorRestored &&
           restored;
  };

  if (g_filesDirty) {
    g_filesDirty = false;
    g_initFailed = false;
    g_nextInitRetryMs = 0;
    if (g_initialized) {
      if (SDL_GL_GetCurrentContext() == g_context)
        DestroyPipeline();
      else
        ForgetPipelineState();
    }
  }
  if (g_initialized && SDL_GL_GetCurrentContext() != g_context) {
    // A renderer/context replacement invalidates every cached GL name.  Never
    // delete those names while another context is current; forget and rebuild.
    ForgetPipelineState();
    g_initFailed = false;
    g_nextInitRetryMs = 0;
  }
  if (!g_initialized) {
    const Uint64 now = SDL_GetTicks();
    if (!g_initFailed || now >= g_nextInitRetryMs) {
      g_initFailed = !InitGLPipeline(renderer);
      if (g_initFailed) g_nextInitRetryMs = now + 1000;
    }
    if (g_initFailed) {
      return fallbackCopy();
    }
  }

  const GLuint inTex = TextureNumber(inputTexture);
  const GLuint outTex = TextureNumber(outputTexture);
  float inputWidth = 0.0f, inputHeight = 0.0f;
  float outputWidth = 0.0f, outputHeight = 0.0f;
  if (inTex == 0 || outTex == 0 ||
      !SDL_GetTextureSize(inputTexture, &inputWidth, &inputHeight) ||
      !SDL_GetTextureSize(outputTexture, &outputWidth, &outputHeight)) {
    return fallbackCopy();
  }

  // Garante que os comandos pendentes do SDL cheguem ao contexto antes de
  // usar o GL diretamente.
  SDL_FlushRenderer(renderer);

  GLint prevFBO = 0;
  GLint prevViewport[4] = {0};
  GLint prevProgram = 0;
  GLint prevArrayBuffer = 0;
  GLint prevActiveTexture = GL_TEXTURE0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
  glGetIntegerv(GL_VIEWPORT, prevViewport);
  glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuffer);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexture);

  GLint prevTextures[kMaxUnits] = {0};
  for (int i = 0; i < kMaxUnits; i++) {
    kotonoha_glActiveTexture(GL_TEXTURE0 + (GLenum)i);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTextures[i]);
  }
  VertexAttributeState prevAttribs[kSavedAttribs];
  for (int i = 0; i < kSavedAttribs; i++)
    prevAttribs[i] = SaveVertexAttribute(i);

  const GLboolean prevBlend = glIsEnabled(GL_BLEND);
  const GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);
  const GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
  const GLboolean prevStencil = glIsEnabled(GL_STENCIL_TEST);
  const GLboolean prevCull = glIsEnabled(GL_CULL_FACE);
  glDisable(GL_BLEND);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_STENCIL_TEST);
  glDisable(GL_CULL_FACE);

  kotonoha_glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, inTex);
  GLint prevMinFilter = GL_LINEAR;
  GLint prevMagFilter = GL_LINEAR;
  glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &prevMinFilter);
  glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &prevMagFilter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  while (glGetError() != GL_NO_ERROR) {
  }
  const bool ok = RunChain(inTex, (int)inputWidth, (int)inputHeight, outTex,
                           (int)outputWidth, (int)outputHeight);

  kotonoha_glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, inTex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, prevMinFilter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, prevMagFilter);
  for (int i = 0; i < kMaxUnits; i++) {
    kotonoha_glActiveTexture(GL_TEXTURE0 + (GLenum)i);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prevTextures[i]);
  }
  kotonoha_glActiveTexture((GLenum)prevActiveTexture);
  for (int i = 0; i < kSavedAttribs; i++)
    RestoreVertexAttribute(prevAttribs[i]);
  glBindBuffer(GL_ARRAY_BUFFER, (GLuint)prevArrayBuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFBO);
  glViewport(prevViewport[0], prevViewport[1], prevViewport[2],
             prevViewport[3]);
  glUseProgram((GLuint)prevProgram);
  (prevBlend ? glEnable : glDisable)(GL_BLEND);
  (prevScissor ? glEnable : glDisable)(GL_SCISSOR_TEST);
  (prevDepth ? glEnable : glDisable)(GL_DEPTH_TEST);
  (prevStencil ? glEnable : glDisable)(GL_STENCIL_TEST);
  (prevCull ? glEnable : glDisable)(GL_CULL_FACE);

  if (!ok) {
    SDL_LogWarn(LogUpscaler, "MPVCompatibleUpscaler pass failed; falling back to plain copy.");
    // A stale/lost GL context or poisoned shader chain must not be retried on
    // every frame.  Drop the pipeline only from its owning context, latch the
    // failure briefly, and then allow a clean rebuild.
    if (SDL_GL_GetCurrentContext() == g_context)
      DestroyPipeline();
    else
      ForgetPipelineState();
    g_initFailed = true;
    g_nextInitRetryMs = SDL_GetTicks() + 1000;
    return fallbackCopy();
  }
  return true;
}

} // namespace

OpenGLUpscaler::OpenGLUpscaler(RendererRef value) : Upscaler(value) {
  if (!isAvaliable())
    throw std::invalid_argument(
        "OpenGLUpscaler requires an SDL OpenGL renderer");
}

OpenGLUpscaler::~OpenGLUpscaler() { KotonohaMPVUpscalerShutdown(); }

UpscaleBackend OpenGLUpscaler::backend() const { return UpscaleBackend::MPV; }
bool OpenGLUpscaler::isAvaliable() const { return isAvaliable(renderer); }
bool OpenGLUpscaler::isAvaliable(RendererRef renderer) {
  const auto *name = SDL_GetRendererName(&renderer);
  return name && SDL_strcasestr(name, "opengl") &&
         SDL_GL_GetCurrentContext() != nullptr;
}

void OpenGLUpscaler::configureBackend(const UpscaleSettings &value) {
  KotonohaMPVUpscalerSetPreset(static_cast<int>(value.preset));
  KotonohaMPVUpscalerSetFiles(value.shaderFiles.c_str());
}

bool OpenGLUpscaler::renderInto(TextureRef input, TextureRef output) {
  return KotonohaMPVUpscalerUpscaleSDLTextureInto(&renderer, &input, &output);
}

Uint64 OpenGLUpscaler::retryDeadline() const {
  return KotonohaMPVUpscalerRetryDeadlineMs();
}
} // namespace Kotonoha
