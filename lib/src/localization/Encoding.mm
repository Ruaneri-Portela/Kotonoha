#include <Kotonoha/localization/Encoding.hpp>

#if defined(__APPLE__)
#import <Foundation/Foundation.h>
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
  NSString *ns = [[NSString alloc] initWithBytes:text.data()
                                          length:text.size()
                                        encoding:NSShiftJISStringEncoding];
  if (!ns)
    return {};
  NSData *data = [ns dataUsingEncoding:NSUTF8StringEncoding];
  if (data == nil)
    return {};
  return std::string(static_cast<const char *>(data.bytes),
                     static_cast<size_t>(data.length));
}
#endif

} // namespace Kotonoha
