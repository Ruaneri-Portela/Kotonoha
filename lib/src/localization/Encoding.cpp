#include <Kotonoha/localization/Encoding.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

// -----------------------------------------------------------------------------
// SELECAO DA PLATAFORMA DE CONVERSAO
// -----------------------------------------------------------------------------

#if defined(__ANDROID__)

#include <SDL3/SDL_system.h>
#include <jni.h>

namespace Kotonoha {
namespace {

// Conversao resiliente via JNI (java.lang.String) no Android
std::string ConvertViaJNI(std::string_view text, const char *fromEncoding,
                          const char *toEncoding = "UTF-8") {
  if (text.empty())
    return {};

  JNIEnv *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
  if (!env) {
    throw std::runtime_error(
        "JNIEnv indisponivel para conversao de texto no Android");
  }

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

  if (env->ExceptionCheck() || !javaString) {
    env->ExceptionClear();
    env->DeleteLocalRef(encodingStr);
    env->DeleteLocalRef(byteArray);
    env->DeleteLocalRef(stringClass);
    throw std::runtime_error(
        "Falha na conversao de caracteres via java.nio.charset: " +
        encodingName);
  }

  const auto getBytes =
      env->GetMethodID(stringClass, "getBytes", "(Ljava/lang/String;)[B");
  jstring target = env->NewStringUTF(toEncoding);
  auto encoded = static_cast<jbyteArray>(
      env->CallObjectMethod(javaString, getBytes, target));
  if (env->ExceptionCheck() || !encoded) {
    env->ExceptionClear();
    env->DeleteLocalRef(target);
    env->DeleteLocalRef(javaString);
    env->DeleteLocalRef(encodingStr);
    env->DeleteLocalRef(byteArray);
    env->DeleteLocalRef(stringClass);
    throw std::runtime_error("Unsupported target text encoding");
  }
  std::string result(static_cast<size_t>(env->GetArrayLength(encoded)), '\0');
  env->GetByteArrayRegion(encoded, 0, static_cast<jsize>(result.size()),
                          reinterpret_cast<jbyte *>(result.data()));
  env->DeleteLocalRef(encoded);
  env->DeleteLocalRef(target);
  env->DeleteLocalRef(javaString);
  env->DeleteLocalRef(encodingStr);
  env->DeleteLocalRef(byteArray);
  env->DeleteLocalRef(stringClass);

  return result;
}

} // namespace
} // namespace Kotonoha

#else

// Configuracao para iOS e Desktop (SDL3)
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

#include <algorithm>
#include <stdexcept>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace Kotonoha {
namespace {

#ifdef _WIN32
std::string WindowsCp932ToUtf8(std::string_view text) {
  std::string out;
  out.reserve(text.size() * 3);

  const auto append = [&](const char *bytes, int count) -> bool {
    wchar_t wide[2]{};
    const int wideCount =
        MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, bytes, count, wide, 2);
    if (wideCount <= 0)
      return false;
    char utf8[8]{};
    const int utf8Count =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, wideCount,
                            utf8, int(sizeof utf8), nullptr, nullptr);
    if (utf8Count <= 0)
      return false;
    out.append(utf8, size_t(utf8Count));
    return true;
  };

  for (size_t i = 0; i < text.size();) {
    const unsigned char first = static_cast<unsigned char>(text[i]);
    if (first < 0x80) {
      out.push_back(char(first));
      ++i;
      continue;
    }
    const bool lead =
        (first >= 0x81 && first <= 0x9f) || (first >= 0xe0 && first <= 0xfc);
    const size_t count = lead && i + 1 < text.size() ? 2 : 1;
    if (!append(text.data() + i, int(count)))
      out.push_back('?');
    i += count;
  }
  return out;
}
#endif

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
// API PUBLICA DA ENGINE
// -----------------------------------------------------------------------------

namespace Kotonoha {

std::string ConvertEncoding(std::string_view text, const char *from,
                            const char *to) {
#if defined(__ANDROID__)
  return ConvertViaJNI(text, from, to);
#else
  if (std::string_view(from) == "LATIN1")
    from = "ISO-8859-1";
  if (std::string_view(to) == "LATIN1")
    to = "ISO-8859-1";
  Converter converter(from, to);
  if (!converter.valid())
    throw std::runtime_error(std::string("Unsupported text encoding ") + from +
                             " -> " + to);
  return converter.run(text, [](const unsigned char *, size_t) { return 1; });
#endif
}
#if !defined(__APPLE__)
std::string ShiftJisToUtf8(std::string_view text) {

#if defined(_WIN32)
  std::string result;
  result.reserve(text.size());
  for (size_t offset = 0; offset < text.size();) {
    const auto first = static_cast<unsigned char>(text[offset]);
    const int length =
        IsDBCSLeadByteEx(932, first) && offset + 1 < text.size() ? 2 : 1;
    wchar_t wide[2];
    const int count = MultiByteToWideChar(
        932, MB_ERR_INVALID_CHARS, text.data() + offset, length, wide, 2);
    offset += length;
    if (!count) {
      result += '?';
      continue;
    }
    char utf8[8];
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, count, utf8,
                                          sizeof(utf8), nullptr, nullptr);
    if (!bytes)
      throw std::runtime_error("CP932 to UTF-8 conversion failed");
    result.append(utf8, bytes);
  }
  return result;
#elif defined(__ANDROID__)
  return ConvertViaJNI(text, "Shift_JIS");
#else
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

std::u16string Utf8ToUtf16(std::string_view text) {
#if defined(_WIN32)
  int wide = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                 int(text.size()), nullptr, 0);
  if (wide <= 0)
    return {};
  std::u16string result(size_t(wide), char16_t{});
  if (MultiByteToWideChar(
          CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()),
          reinterpret_cast<wchar_t *>(result.data()), wide) != wide)
    return {};
  return result;
#elif defined(__ANDROID__)
  std::string converted = ConvertViaJNI(text, "UTF-8", "UTF-16LE");
  if (converted.size() % 2 != 0)
    return {};
  std::u16string result(converted.size() / 2, char16_t{});
  std::memcpy(result.data(), converted.data(), converted.size());
  return result;
#else
  Converter converter("UTF-8", "UTF-16LE");
  if (!converter.valid())
    return {};
  std::string converted =
      converter.run(text, [](const unsigned char *, size_t) { return 1; });
  if (converted.size() % 2 != 0)
    return {};
  std::u16string result(converted.size() / 2, char16_t{});
  std::memcpy(result.data(), converted.data(), converted.size());
  return result;
#endif
}

std::string Utf16ToUtf8(std::u16string_view text) {
#if defined(_WIN32)
  int utf8len =
      WideCharToMultiByte(CP_UTF8, MB_ERR_INVALID_CHARS,
                          reinterpret_cast<const wchar_t *>(text.data()),
                          int(text.size()), nullptr, 0, nullptr, nullptr);
  if (utf8len <= 0)
    return {};
  std::string result(size_t(utf8len), '\0');
  if (WideCharToMultiByte(CP_UTF8, MB_ERR_INVALID_CHARS,
                          reinterpret_cast<const wchar_t *>(text.data()),
                          int(text.size()), result.data(), int(utf8len),
                          nullptr, nullptr) != utf8len)
    return {};
  return result;
#elif defined(__ANDROID__)
  std::string_view sv(reinterpret_cast<const char *>(text.data()),
                      text.size() * sizeof(char16_t));
  return ConvertViaJNI(sv, "UTF-16LE", "UTF-8");
#else
  std::string_view sv(reinterpret_cast<const char *>(text.data()),
                      text.size() * sizeof(char16_t));
  Converter converter("UTF-16LE", "UTF-8");
  if (!converter.valid())
    return {};
  return converter.run(sv, [](const unsigned char *, size_t) { return 1; });
#endif
}
#endif // __APPLE__

} // namespace Kotonoha
