#include <Kotonoha/core/Config.hpp>
#include <Kotonoha/localization/Encoding.hpp>

#include <algorithm>
#include <cstdint>
#include <stdexcept>

// -----------------------------------------------------------------------------
// SEÇÃO DE SELEÇÃO DA PLATAFORMA DE CONVERSÃO
// -----------------------------------------------------------------------------

#if defined(__ANDROID__)

#include <SDL3/SDL_system.h>
#include <jni.h>

namespace Kotonoha {
namespace {

// Conversão resiliente via JNI (java.lang.String) no Android
std::string ConvertViaJNI(std::string_view text, const char *fromEncoding) {
  if (text.empty())
    return {};

  JNIEnv *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
  if (!env) {
    throw std::runtime_error(
        "JNIEnv indisponível para conversão de texto no Android");
  }

  // Garante o mapeamento do alias de encoding para o formato aceito pelo Java
  std::string encodingName = fromEncoding;
  if (encodingName == "CP932" || encodingName == "WINDOWS-31J" ||
      encodingName == "SJIS") {
    encodingName = "Shift_JIS";
  }

  jbyteArray byteArray = env->NewByteArray(static_cast<jsize>(text.size()));
  if (!byteArray) {
    throw std::runtime_error("Falha ao alocar jbyteArray no JNI");
  }

  env->SetByteArrayRegion(byteArray, 0, static_cast<jsize>(text.size()),
                          reinterpret_cast<const jbyte *>(text.data()));

  jstring encodingStr = env->NewStringUTF(encodingName.c_str());
  jclass stringClass = env->FindClass("java/lang/String");
  jmethodID ctor =
      env->GetMethodID(stringClass, "<init>", "([BLjava/lang/String;)V");

  jstring javaString = static_cast<jstring>(
      env->NewObject(stringClass, ctor, byteArray, encodingStr));

  // Trata falha na decodificação pelo Java (ex: encoding inválido)
  if (env->ExceptionCheck() || !javaString) {
    env->ExceptionClear();
    env->DeleteLocalRef(encodingStr);
    env->DeleteLocalRef(byteArray);
    env->DeleteLocalRef(stringClass);
    throw std::runtime_error(
        "Falha na conversão de caracteres via java.nio.charset: " +
        encodingName);
  }

  const char *utf8Bytes = env->GetStringUTFChars(javaString, nullptr);
  const jsize utf8Length = env->GetStringUTFLength(javaString);

  std::string result(utf8Bytes, static_cast<size_t>(utf8Length));

  env->ReleaseStringUTFChars(javaString, utf8Bytes);
  env->DeleteLocalRef(javaString);
  env->DeleteLocalRef(encodingStr);
  env->DeleteLocalRef(byteArray);
  env->DeleteLocalRef(stringClass);

  return result;
}

} // namespace
} // namespace Kotonoha

#else

// Configuração para iOS e Desktop (SDL3)
#include <SDL3/SDL_stdinc.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
#include <cerrno>
#include <iconv.h>
#define KOTONOHA_USE_SYSTEM_ICONV 1
#else
#define KOTONOHA_USE_SYSTEM_ICONV 0
#endif

namespace Kotonoha {
namespace {

class Converter {
#if KOTONOHA_USE_SYSTEM_ICONV
  iconv_t handle;
#else
  SDL_iconv_t handle;
#endif

public:
  Converter(const char *from, const char *to)
#if KOTONOHA_USE_SYSTEM_ICONV
      : handle(iconv_open(to, from)){}
#else
      : handle(SDL_iconv_open(to, from)) {
  }
#endif

        ~Converter() {
    if (valid()) {
#if KOTONOHA_USE_SYSTEM_ICONV
      iconv_close(handle);
#else
      SDL_iconv_close(handle);
#endif
    }
  }

  Converter(const Converter &) = delete;
  Converter &operator=(const Converter &) = delete;

  bool valid() const {
#if KOTONOHA_USE_SYSTEM_ICONV
    return handle != reinterpret_cast<iconv_t>(-1);
#else
    return handle != reinterpret_cast<SDL_iconv_t>(-1);
#endif
  }

  size_t convert(const char **input, size_t *inputLeft, char **output,
                 size_t *outputLeft) {
#if KOTONOHA_USE_SYSTEM_ICONV
    char *current = input ? const_cast<char *>(*input) : nullptr;
    const size_t result = iconv(handle, input ? &current : nullptr, inputLeft,
                                output, outputLeft);
    if (input)
      *input = current;
    if (result != static_cast<size_t>(-1))
      return result;
    switch (errno) {
    case E2BIG:
      return SDL_ICONV_E2BIG;
    case EILSEQ:
      return SDL_ICONV_EILSEQ;
    case EINVAL:
      return SDL_ICONV_EINVAL;
    default:
      return SDL_ICONV_ERROR;
    }
#else
    return SDL_iconv(handle, input, inputLeft, output, outputLeft);
#endif
  }

  template <typename Skip>
  std::string run(std::string_view text, Skip invalidLength) {
    std::string out;
    out.resize(text.size() * 3 + 8);
    const char *in = text.data();
    size_t inLeft = text.size();
    char *dst = out.data();
    size_t outLeft = out.size();
    while (inLeft > 0) {
      const size_t result = convert(&in, &inLeft, &dst, &outLeft);
      if (result == SDL_ICONV_E2BIG) {
        const size_t used = out.size() - outLeft;
        out.resize(out.size() * 2);
        dst = out.data() + used;
        outLeft = out.size() - used;
      } else if (result == SDL_ICONV_EILSEQ || result == SDL_ICONV_EINVAL) {
        if (outLeft == 0) {
          const size_t used = out.size() - outLeft;
          out.resize(out.size() * 2);
          dst = out.data() + used;
          outLeft = out.size() - used;
        }
        const size_t skip = std::min<size_t>(
            inLeft,
            invalidLength(reinterpret_cast<const unsigned char *>(in), inLeft));
        in += skip;
        inLeft -= skip;
        *dst++ = '?';
        --outLeft;
        convert(nullptr, nullptr, nullptr, nullptr);
      } else if (result == SDL_ICONV_ERROR) {
        throw std::runtime_error("Text conversion failed");
      }
    }
    out.resize(out.size() - outLeft);
    return out;
  }
};

} // namespace
} // namespace Kotonoha

#endif // __ANDROID__

// -----------------------------------------------------------------------------
// API PÚBLICA DA ENGINE
// -----------------------------------------------------------------------------

namespace Kotonoha {

std::string ConvertEncoding(std::string_view text, const char *from,
                            const char *to) {
#if defined(__ANDROID__)
  if (std::string_view(to) == "UTF-8" || std::string_view(to) == "UTF8") {
    return ConvertViaJNI(text, from);
  }
  throw std::runtime_error("Conversão para encodings diferentes de UTF-8 não "
                           "suportada no Android via JNI");
#else
  Converter converter(from, to);
  if (!converter.valid())
    throw std::runtime_error(std::string("Unsupported text encoding ") + from +
                             " -> " + to);
  return converter.run(text, [](const unsigned char *, size_t) { return 1; });
#endif
}

std::string ShiftJisToUtf8(std::string_view text) {
#if defined(__ANDROID__)
  // No Android, chama a conversão via JNI diretamente com o alias
  // CP932/Shift_JIS
  return ConvertViaJNI(text, "Shift_JIS");
#else
  // No iOS (System iconv) e Desktop (SDL_iconv)
  for (const char *name : {"CP932", "WINDOWS-31J", "SHIFT_JIS", "SJIS"}) {
    Converter converter(name, "UTF-8");
    if (!converter.valid())
      continue;
    return converter.run(text, [](const unsigned char *p, size_t left) {
      const bool lead =
          p[0] >= 0x81 && p[0] <= 0xFC && !(p[0] >= 0xA1 && p[0] <= 0xDF);
      return lead && left > 1 ? size_t(2) : size_t(1);
    });
  }
  throw std::runtime_error(
      "Shift-JIS conversion is unavailable (no compatible iconv converter)");
#endif
}

} // namespace Kotonoha