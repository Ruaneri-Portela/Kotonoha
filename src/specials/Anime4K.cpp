#include <SDL3/SDL.h>

#include <Kotonoha/speciais/OpenGL.h>
#include <Kotonoha/speciais/Upscaling.h>
#include <string>
#include <sstream>
#include <vector>

#ifndef SDL_PROP_TEXTURE_OPENGL_TEXTURE_NUMBER
#define SDL_PROP_TEXTURE_OPENGL_TEXTURE_NUMBER "SDL.texture.opengl.texture"
#endif
#ifndef SDL_PROP_TEXTURE_OPENGLES2_TEXTURE_NUMBER
#define SDL_PROP_TEXTURE_OPENGLES2_TEXTURE_NUMBER "SDL.texture.opengles2.texture"
#endif

#if defined(KOTONOHA_HAS_ANIME4K)

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

static const char* kVertexShaderSource = R"(
#version 100
attribute vec2 aPos;
attribute vec2 aTex;
varying vec2 vTexCoord;

void main() {
    vTexCoord = aTex;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* kFallbackFragmentShaderSource = R"(
#version 100
precision mediump float;

varying vec2 vTexCoord;
uniform sampler2D uTexture;
uniform vec2 uTextureSize;

float getLuma(vec3 color) {
    return dot(color, vec3(0.299, 0.587, 0.114));
}

vec4 getSample(vec2 offset) {
    return texture2D(uTexture, vTexCoord + offset / uTextureSize);
}

void main() {
    vec4 c = texture2D(uTexture, vTexCoord);

    vec4 t  = getSample(vec2( 0.0, -1.0));
    vec4 b  = getSample(vec2( 0.0,  1.0));
    vec4 l  = getSample(vec2(-1.0,  0.0));
    vec4 r  = getSample(vec2( 1.0,  0.0));

    vec4 tl = getSample(vec2(-1.0, -1.0));
    vec4 tr = getSample(vec2( 1.0, -1.0));
    vec4 bl = getSample(vec2(-1.0,  1.0));
    vec4 br = getSample(vec2( 1.0,  1.0));

    float lumaC  = getLuma(c.rgb);
    float lumaT  = getLuma(t.rgb);
    float lumaB  = getLuma(b.rgb);
    float lumaL  = getLuma(l.rgb);
    float lumaR  = getLuma(r.rgb);

    float lumaTL = getLuma(tl.rgb);
    float lumaTR = getLuma(tr.rgb);
    float lumaBL = getLuma(bl.rgb);
    float lumaBR = getLuma(br.rgb);

    float gx = (lumaTR + 2.0 * lumaR + lumaBR) - (lumaTL + 2.0 * lumaL + lumaBL);
    float gy = (lumaBL + 2.0 * lumaB + lumaBR) - (lumaTL + 2.0 * lumaT + lumaTR);

    float gradLen = length(vec2(gx, gy));

    if (gradLen < 0.03) {
        gl_FragColor = c;
        return;
    }

    vec2 dir = vec2(gx, gy) / gradLen;

    vec4 minColor = min(c, min(min(t, b), min(l, r)));
    vec4 maxColor = max(c, max(max(t, b), max(l, r)));

    vec4 samplePush1 = getSample(dir * 0.5);
    vec4 pushed = mix(c, samplePush1, 0.5);
    vec4 clamped = clamp(pushed, minColor, maxColor);

    gl_FragColor = mix(c, clamped, 0.7);
}
)";

static std::string LoadShaderSourceFromAssets(const char* filename) {
    std::string path = std::string("assets/Anime4k/") + filename;
    size_t dataSize = 0;
    void* buffer = SDL_LoadFile(path.c_str(), &dataSize);

    if (buffer == nullptr) {
        path = std::string("assets/Anime4K/") + filename;
        buffer = SDL_LoadFile(path.c_str(), &dataSize);
    }

    if (buffer == nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Anime4K: Could not load shader file '%s' from assets", filename);
        return "";
    }

    std::string source(static_cast<const char*>(buffer), dataSize);
    SDL_free(buffer);

    return source;
}

static GLuint CompileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    if (!shader) return 0;

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint infoLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 1) {
            char* infoLog = (char*)SDL_malloc(infoLen);
            glGetShaderInfoLog(shader, infoLen, NULL, infoLog);
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Anime4K Shader compilation error: %s", infoLog);
            SDL_free(infoLog);
        }
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static bool InitGLPipeline() {
    Kotonoha_OpenGLInit();

    if (g_anime4kInitialized) return true;

    std::string fragSource = LoadShaderSourceFromAssets("Anime4K_ModeA_A.glsl");
    if (fragSource.empty()) {
        fragSource = LoadShaderSourceFromAssets("Anime4K_Upscale.glsl");
    }

    if (fragSource.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Anime4K shader files not found in assets/Anime4k/; disabling Anime4K scaler.");
        return false;
    }

    GLuint vertShader = CompileShader(GL_VERTEX_SHADER, kVertexShaderSource);
    GLuint fragShader = CompileShader(GL_FRAGMENT_SHADER, fragSource.c_str());

    if (!vertShader || !fragShader) {
        if (vertShader) glDeleteShader(vertShader);
        if (fragShader) glDeleteShader(fragShader);
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Anime4K shader compilation failed; disabling Anime4K scaler.");
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
            char* infoLog = (char*)SDL_malloc(infoLen);
            glGetProgramInfoLog(g_program, infoLen, NULL, infoLog);
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Anime4K Program link error: %s", infoLog);
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
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
    };

    glGenBuffers(1, &g_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadData), quadData, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    g_anime4kInitialized = true;
    SDL_Log("Anime4K OpenGL/GLES Upscaler initialized successfully from assets/Anime4k/");
    return true;
}

} // namespace

extern "C" {

bool Kotonoha_Anime4KInit(SDL_Renderer *renderer) {
    (void)renderer;
    return InitGLPipeline();
}

void Kotonoha_Anime4KShutdown(void) {
    if (!g_anime4kInitialized) return;

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

bool Kotonoha_Anime4KUpscaleSDLTextureInto(
    SDL_Renderer *renderer,
    SDL_Texture *inputTexture,
    SDL_Texture *outputTexture)
{
    if (renderer == NULL || inputTexture == NULL || outputTexture == NULL) {
        return false;
    }

    if (!g_anime4kInitialized) {
        if (!InitGLPipeline()) {
            SDL_SetRenderTarget(renderer, outputTexture);
            SDL_RenderTexture(renderer, inputTexture, NULL, NULL);
            SDL_SetRenderTarget(renderer, NULL);
            return true;
        }
    }

    SDL_PropertiesID inProps = SDL_GetTextureProperties(inputTexture);
    SDL_PropertiesID outProps = SDL_GetTextureProperties(outputTexture);

    GLuint inTex = (GLuint)SDL_GetNumberProperty(inProps, SDL_PROP_TEXTURE_OPENGL_TEXTURE_NUMBER, 0);
    if (inTex == 0) {
        inTex = (GLuint)SDL_GetNumberProperty(inProps, SDL_PROP_TEXTURE_OPENGLES2_TEXTURE_NUMBER, 0);
    }

    GLuint outTex = (GLuint)SDL_GetNumberProperty(outProps, SDL_PROP_TEXTURE_OPENGL_TEXTURE_NUMBER, 0);
    if (outTex == 0) {
        outTex = (GLuint)SDL_GetNumberProperty(outProps, SDL_PROP_TEXTURE_OPENGLES2_TEXTURE_NUMBER, 0);
    }

    if (inTex == 0 || outTex == 0) {
        SDL_SetRenderTarget(renderer, outputTexture);
        SDL_RenderTexture(renderer, inputTexture, NULL, NULL);
        SDL_SetRenderTarget(renderer, NULL);
        return true;
    }

    GLint prevFBO = 0;
    GLint prevViewport[4] = {0};
    GLint prevProgram = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);

    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outTex, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
        SDL_SetRenderTarget(renderer, outputTexture);
        SDL_RenderTexture(renderer, inputTexture, NULL, NULL);
        SDL_SetRenderTarget(renderer, NULL);
        return true;
    }

    glViewport(0, 0, (GLsizei)outputTexture->w, (GLsizei)outputTexture->h);

    glUseProgram(g_program);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glUniform1i(g_uTextureLoc, 0);
    glUniform2f(g_uTextureSizeLoc, (float)inputTexture->w, (float)inputTexture->h);

    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);

    if (g_aPosLoc >= 0) {
        glEnableVertexAttribArray(g_aPosLoc);
        glVertexAttribPointer(g_aPosLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    }
    if (g_aTexLoc >= 0) {
        glEnableVertexAttribArray(g_aTexLoc);
        glVertexAttribPointer(g_aTexLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    }

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    if (g_aPosLoc >= 0) glDisableVertexAttribArray(g_aPosLoc);
    if (g_aTexLoc >= 0) glDisableVertexAttribArray(g_aTexLoc);

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glUseProgram(prevProgram);

    return true;
}

} // extern "C"
#endif