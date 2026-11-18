#include <Kotonoha/localization/Encoding.hpp>

#if defined(__APPLE__)
#import <CoreFoundation/CoreFoundation.h>
#import <Foundation/Foundation.h>
#include <SDL3/SDL_stdinc.h>
#include <algorithm>
#include <stdexcept>
#endif

namespace Kotonoha {

#if defined(__APPLE__)
namespace {
std::string bytesFromData(NSData *data) {
  if (data == nil)
    throw std::runtime_error("Foundation text encoding conversion failed");
  return std::string(static_cast<const char *>(data.bytes),
                     static_cast<size_t>(data.length));
}
} // namespace

// CP1252 must work even when SDL3 was built without its system iconv backend.
std::string Windows1252ToUtf8(std::string_view text) {
  if (text.empty())
    return {};

  // CP1252 leaves these five byte values undefined; use the same
  // replacement character as the generic converter's invalid-byte policy.
  std::string input(text);
  for (char &ch : input) {
    switch (static_cast<unsigned char>(ch)) {
    case 0x81:
    case 0x8d:
    case 0x8f:
    case 0x90:
    case 0x9d:
      ch = '?';
      break;
    default:
      break;
    }
  }

  NSString *decoded =
      [[NSString alloc] initWithBytes:input.data()
                               length:input.size()
                             encoding:NSWindowsCP1252StringEncoding];
  if (decoded == nil)
    throw std::runtime_error("Windows-1252 to UTF-8 conversion failed");
  return bytesFromData([decoded dataUsingEncoding:NSUTF8StringEncoding]);
}

std::string Utf8ToWindows1252(std::string_view text) {
  if (text.empty())
    return {};
  NSString *decoded = [[NSString alloc] initWithBytes:text.data()
                                               length:text.size()
                                             encoding:NSUTF8StringEncoding];
  if (decoded == nil)
    throw std::runtime_error("Invalid UTF-8 input for Windows-1252 conversion");
  return bytesFromData([decoded dataUsingEncoding:NSWindowsCP1252StringEncoding
                             allowLossyConversion:YES]);
}

std::u16string Utf8ToUtf16(std::string_view text) {
  if (text.empty())
    return {};
  NSString *ns = [[NSString alloc] initWithBytes:text.data()
                                          length:text.size()
                                        encoding:NSUTF8StringEncoding];
  if (!ns)
    return {};
  std::u16string result(ns.length, 0);
  [ns getCharacters:reinterpret_cast<unichar *>(&result[0])
              range:NSMakeRange(0, ns.length)];
  return result;
}

std::string Utf16ToUtf8(std::u16string_view text) {
  if (text.empty())
    return {};
  NSString *ns = [[NSString alloc]
      initWithCharacters:reinterpret_cast<const unichar *>(text.data())
                  length:text.size()];
  if (!ns)
    return {};
  const char *utf8 = [ns UTF8String];
  return utf8 ? std::string(utf8) : std::string();
}

std::string ShiftJisToUtf8(std::string_view text) {
  if (text.empty())
    return {};
  for (const char *from : {"CP932", "WINDOWS-31J", "SHIFT_JIS", "SJIS"}) {
    SDL_iconv_t cd = SDL_iconv_open("UTF-8", from);
    if (cd == reinterpret_cast<SDL_iconv_t>(-1))
      continue;
    std::string out;
    out.resize(text.size() * 3 + 8);
    const char *in = text.data();
    size_t inLeft = text.size();
    char *dst = out.data();
    size_t outLeft = out.size();
    while (inLeft > 0) {
      const size_t rc = SDL_iconv(cd, &in, &inLeft, &dst, &outLeft);
      if (rc == SDL_ICONV_E2BIG) {
        const size_t used = out.size() - outLeft;
        out.resize(out.size() * 2);
        dst = out.data() + used;
        outLeft = out.size() - used;
      } else if (rc == SDL_ICONV_EILSEQ || rc == SDL_ICONV_EINVAL) {
        if (outLeft == 0) {
          const size_t used = out.size() - outLeft;
          out.resize(out.size() * 2);
          dst = out.data() + used;
          outLeft = out.size() - used;
        }
        const auto first = static_cast<unsigned char>(*in);
        const bool lead =
            first >= 0x81 && first <= 0xFC && !(first >= 0xA1 && first <= 0xDF);
        const size_t skip = (lead && inLeft > 1) ? 2 : 1;
        in += skip;
        inLeft -= skip;
        *dst++ = '?';
        --outLeft;
        SDL_iconv(cd, nullptr, nullptr, nullptr, nullptr);
      } else if (rc == SDL_ICONV_ERROR) {
        SDL_iconv_close(cd);
        throw std::runtime_error("CP932 to UTF-8 conversion failed");
      }
    }
    out.resize(out.size() - outLeft);
    SDL_iconv_close(cd);
    return out;
  }
  const NSStringEncoding cp932 =
      CFStringConvertEncodingToNSStringEncoding(kCFStringEncodingDOSJapanese);
  std::string out;
  out.reserve(text.size() * 3);
  for (size_t i = 0; i < text.size();) {
    const auto first = static_cast<unsigned char>(text[i]);
    if (first < 0x80) {
      out.push_back(static_cast<char>(first));
      ++i;
      continue;
    }
    const bool lead =
        first >= 0x81 && first <= 0xFC && !(first >= 0xA1 && first <= 0xDF);
    const size_t len = (lead && i + 1 < text.size()) ? 2 : 1;
    NSString *ch = [[NSString alloc] initWithBytes:text.data() + i
                                            length:len
                                          encoding:cp932];
    if (ch != nil) {
      out += bytesFromData([ch dataUsingEncoding:NSUTF8StringEncoding]);
    } else {
      out.push_back('?');
    }
    i += len;
  }
  return out;
}
#endif

} // namespace Kotonoha
