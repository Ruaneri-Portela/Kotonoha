#include <Kotonoha/localization/Encoding.hpp>

#include <SDL3/SDL_stdinc.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
#include <cerrno>
#include <iconv.h>
#endif

#include <algorithm>
#include <stdexcept>

namespace Kotonoha {
namespace {

class Converter {
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
  iconv_t handle;
#else
  SDL_iconv_t handle;
#endif

public:
  Converter(const char *from, const char *to)
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
      : handle(iconv_open(to, from)) {}
#else
      : handle(SDL_iconv_open(to, from)) {}
#endif
  ~Converter() {
    if (valid()) {
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
      iconv_close(handle);
#else
      SDL_iconv_close(handle);
#endif
    }
  }
  Converter(const Converter &) = delete;
  Converter &operator=(const Converter &) = delete;
  bool valid() const {
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
    return handle != reinterpret_cast<iconv_t>(-1);
#else
    return handle != reinterpret_cast<SDL_iconv_t>(-1);
#endif
  }

  size_t convert(const char **input, size_t *inputLeft, char **output,
                 size_t *outputLeft) {
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
    char *current = input ? const_cast<char *>(*input) : nullptr;
    const size_t result =
        iconv(handle, input ? &current : nullptr, inputLeft, output, outputLeft);
    if (input)
      *input = current;
    if (result != static_cast<size_t>(-1))
      return result;
    switch (errno) {
    case E2BIG: return SDL_ICONV_E2BIG;
    case EILSEQ: return SDL_ICONV_EILSEQ;
    case EINVAL: return SDL_ICONV_EINVAL;
    default: return SDL_ICONV_ERROR;
    }
#else
    return SDL_iconv(handle, input, inputLeft, output, outputLeft);
#endif
  }

  // `invalidLength` tells how many input bytes an undecodable sequence spans.
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
        const size_t skip = std::min<size_t>(inLeft, invalidLength(
            reinterpret_cast<const unsigned char *>(in), inLeft));
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

std::string ConvertEncoding(std::string_view text, const char *from,
                            const char *to) {
  Converter converter(from, to);
  if (!converter.valid())
    throw std::runtime_error(std::string("Unsupported text encoding ") + from +
                             " -> " + to);
  return converter.run(text, [](const unsigned char *, size_t) { return 1; });
}

std::string ShiftJisToUtf8(std::string_view text) {
  // CP932 first: plain SHIFT_JIS lacks the NEC row 13 symbols (①, Ⅰ...).
  for (const char *name : {"CP932", "WINDOWS-31J", "SHIFT_JIS", "SJIS"}) {
    Converter converter(name, "UTF-8");
    if (!converter.valid())
      continue;
    // A bad lead byte consumes its trail byte, as Shift-JIS decoders do.
    return converter.run(text, [](const unsigned char *p, size_t left) {
      const bool lead = p[0] >= 0x81 && p[0] <= 0xFC &&
                        !(p[0] >= 0xA1 && p[0] <= 0xDF);
      return lead && left > 1 ? size_t(2) : size_t(1);
    });
  }
  throw std::runtime_error(
      "Shift-JIS conversion is unavailable (no compatible iconv converter)");
}

} // namespace Kotonoha
