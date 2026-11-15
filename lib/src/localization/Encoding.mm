#include <Kotonoha/localization/Encoding.hpp>

#if defined(__APPLE__)
#import <Foundation/Foundation.h>
#endif

namespace Kotonoha {

#if defined(__APPLE__)
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
