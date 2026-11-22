#include <Kotonoha/localization/Encoding.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

// Apple builds use CoreFoundation exclusively, implemented in Encoding.mm.
// Deliberately exclude this SDL/iconv converter (including the iOS system
// iconv branch) so Apple executables have no codec dependency on iconv.
#if !defined(__APPLE__)

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

#if defined(__APPLE__)
// Implemented with Foundation in Encoding.mm. SDL_iconv in some Xcode SDL3
// builds does not expose Windows-1252 even though system iconv does.
std::string Windows1252ToUtf8(std::string_view text);
std::string Utf8ToWindows1252(std::string_view text);
#endif

std::string ConvertEncoding(std::string_view text, const char *from,
                            const char *to) {
#if defined(__ANDROID__)
  return ConvertViaJNI(text, from, to);
#else
#if defined(__APPLE__)
  const auto isWindows1252 = [](std::string_view encoding) {
    return encoding == "CP1252" || encoding == "WINDOWS-1252" ||
           encoding == "windows-1252";
  };
  if (isWindows1252(from) && std::string_view(to) == "UTF-8")
    return Windows1252ToUtf8(text);
  if (std::string_view(from) == "UTF-8" && isWindows1252(to))
    return Utf8ToWindows1252(text);
#endif
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
  std::u16string result;
  result.reserve(text.size());
  const auto appendCodePoint = [&result](uint32_t cp) {
    if (cp <= 0xFFFFu) {
      result.push_back(static_cast<char16_t>(cp));
    } else if (cp <= 0x10FFFFu) {
      cp -= 0x10000u;
      result.push_back(static_cast<char16_t>(0xD800u + (cp >> 10)));
      result.push_back(static_cast<char16_t>(0xDC00u + (cp & 0x3FFu)));
    } else {
      result.push_back(static_cast<char16_t>(0xFFFDu));
    }
  };
  for (size_t i = 0; i < text.size();) {
    const unsigned char b0 = static_cast<unsigned char>(text[i]);
    if (b0 < 0x80u) {
      result.push_back(static_cast<char16_t>(b0));
      ++i;
      continue;
    }
    if ((b0 & 0xE0u) == 0xC0u) {
      if (i + 1 < text.size()) {
        const unsigned char b1 = static_cast<unsigned char>(text[i + 1]);
        if ((b1 & 0xC0u) == 0x80u) {
          const uint32_t cp =
              (static_cast<uint32_t>(b0 & 0x1Fu) << 6) | (b1 & 0x3Fu);
          if (cp >= 0x80u) {
            appendCodePoint(cp);
            i += 2;
            continue;
          }
        }
      }
      result.push_back(static_cast<char16_t>(0xFFFDu));
      ++i;
      continue;
    }
    if ((b0 & 0xF0u) == 0xE0u) {
      if (i + 2 < text.size()) {
        const unsigned char b1 = static_cast<unsigned char>(text[i + 1]);
        const unsigned char b2 = static_cast<unsigned char>(text[i + 2]);
        if ((b1 & 0xC0u) == 0x80u && (b2 & 0xC0u) == 0x80u) {
          const uint32_t cp = (static_cast<uint32_t>(b0 & 0x0Fu) << 12) |
                              (static_cast<uint32_t>(b1 & 0x3Fu) << 6) |
                              (b2 & 0x3Fu);
          if (cp >= 0x800u && (cp < 0xD800u || cp > 0xDFFFu)) {
            appendCodePoint(cp);
            i += 3;
            continue;
          }
        }
      }
      result.push_back(static_cast<char16_t>(0xFFFDu));
      ++i;
      continue;
    }
    if ((b0 & 0xF8u) == 0xF0u) {
      if (i + 3 < text.size()) {
        const unsigned char b1 = static_cast<unsigned char>(text[i + 1]);
        const unsigned char b2 = static_cast<unsigned char>(text[i + 2]);
        const unsigned char b3 = static_cast<unsigned char>(text[i + 3]);
        if ((b1 & 0xC0u) == 0x80u && (b2 & 0xC0u) == 0x80u &&
            (b3 & 0xC0u) == 0x80u) {
          const uint32_t cp = (static_cast<uint32_t>(b0 & 0x07u) << 18) |
                              (static_cast<uint32_t>(b1 & 0x3Fu) << 12) |
                              (static_cast<uint32_t>(b2 & 0x3Fu) << 6) |
                              (b3 & 0x3Fu);
          if (cp >= 0x10000u && cp <= 0x10FFFFu) {
            appendCodePoint(cp);
            i += 4;
            continue;
          }
        }
      }
      result.push_back(static_cast<char16_t>(0xFFFDu));
      ++i;
      continue;
    }
    result.push_back(static_cast<char16_t>(0xFFFDu));
    ++i;
  }
  return result;
}

std::string Utf16ToUtf8(std::u16string_view text) {
  std::string result;
  result.reserve(text.size());
  const auto appendUtf8 = [&result](uint32_t cp) {
    if (cp < 0x80u) {
      result.push_back(static_cast<char>(cp));
    } else if (cp < 0x800u) {
      result.push_back(static_cast<char>(0xC0u | (cp >> 6)));
      result.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp < 0x10000u) {
      result.push_back(static_cast<char>(0xE0u | (cp >> 12)));
      result.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
      result.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else {
      result.push_back(static_cast<char>(0xF0u | (cp >> 18)));
      result.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
      result.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
      result.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
  };
  for (size_t i = 0; i < text.size(); ++i) {
    const uint32_t unit = static_cast<uint32_t>(text[i]);
    if (unit >= 0xD800u && unit <= 0xDBFFu) {
      if (i + 1 < text.size()) {
        const uint32_t low = static_cast<uint32_t>(text[i + 1]);
        if (low >= 0xDC00u && low <= 0xDFFFu) {
          const uint32_t cp =
              0x10000u + (((unit - 0xD800u) << 10) | (low - 0xDC00u));
          appendUtf8(cp);
          ++i;
          continue;
        }
      }
      appendUtf8(0xFFFDu);
    } else if (unit >= 0xDC00u && unit <= 0xDFFFu) {
      appendUtf8(0xFFFDu);
    } else {
      appendUtf8(unit);
    }
  }
  return result;
}
#endif // __APPLE__

} // namespace Kotonoha

#endif // !defined(__APPLE__) -- platform encoder is Encoding.mm on Apple
