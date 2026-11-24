# Kotonoha encodings on macOS and iOS — no iconv

Apple builds use **lib/src/localization/Encoding.mm** as the single
implementation of `Kotonoha::ConvertEncoding`, `ShiftJisToUtf8`,
`Utf8ToUtf16` and `Utf16ToUtf8`. The backend combines CoreFoundation with
explicit Unicode scalar conversions to UTF-16/UTF-32 and **does not**
call `iconv`, `SDL_iconv` or `SDL_iconv_string`.

`Encoding.cpp` still implements the other platforms, but its conversion
code is excluded under `__APPLE__`. The Xcode project includes
`Encoding.mm` in the engine targets for macOS and iOS; CMake also compiles
the `.mm` files on Apple and links CoreFoundation.

## Covered conversions

| Encoding / aliases | Usage |
| --- | --- |
| UTF-8 | Scripts, translations, UI and text reading |
| CP932, Windows-31J, Shift_JIS, SJIS | Japanese games, Windows punctuation, NEC/IBM extension and SCF text |
| Windows-1252, CP1252 | Western RealLive/rlBabel text |
| ISO-8859-1, LATIN1 | Legacy assets and text |
| UTF-16LE / UTF-16BE | School Days, LucaSystem, ISO/Joliet files and BOM-marked assets |
| UTF-32LE / UTF-32BE | BOM-marked assets, including supplementary characters |
| EUC-JP, ISO-2022-JP | Debug Overlay text viewer |
| GBK, GB18030, Big5 | Chinese text viewer |
| EUC-KR, KOI8-R | Korean and Cyrillic text viewer |

Output conversions for the same names are tested where applicable.
The API can query other IANA encodings available in CoreFoundation.
An unknown name raises an exception instead of silently interpreting the
bytes as UTF-8.

## Compatibility and invalid data

- CP932 uses the Japanese Windows/DOS table and normalizes six Unicode
  punctuation differences between Shift-JIS and Windows-31J. Invalid
  sequences become `?` without dropping adjacent ASCII characters.
- Characters that the target encoding cannot represent become a single
  `?` per Unicode scalar, including characters outside the BMP.
- UTF-16LE/BE detects surrogate pairs, preserves supplementary characters
  and replaces isolated sequences with `?`.
- UTF-32LE/BE preserves byte order, zero bytes and valid Unicode scalars;
  invalid scalars are replaced with `?`.
- Generic conversions handle binary input with an explicit size,
  avoiding truncation at NUL. The native `Utf8ToUtf16` and
  `Utf16ToUtf8` functions keep their previous APIs.
- BOM detection in `AssetProvider::decodeTextAsset` still selects
  UTF-8, UTF-16LE/BE and UTF-32LE/BE before conversion.

## Build and validation

`Common.xcconfig` **keeps `-liconv`** for every Xcode target that shares
that configuration. Third-party libraries may still reference
`_iconv_open`, `_iconv` and `_iconv_close`. The engine's text conversion
remains independent of iconv via `Encoding.mm` / CoreFoundation.

The macOS and iOS `xcconfig` files also add `-framework CoreFoundation`.
CMake explicitly links both `CoreFoundation` and the `libiconv` supplied
by the Apple SDK into `KotonohaEngine`, and keeps the transitive
`libiconv` dependency for consuming applications. No separate `libiconv`
vcpkg package is needed for macOS/iOS. The absence of `libiconv` from the
Apple manifest **does not** mean the complete applications are
iconv-independent.

The `.github/workflows/apple-encoding-no-iconv.yml` suite runs on macOS
with C++17, compiles `Encoding.cpp` and `Encoding.mm` together, checks
symbols with `nm`, checks linked frameworks with `otool` and runs
`lib/tests/EncodingAppleTests.cpp`, without SDL, fonts or commercial
assets. It separately verifies that the macOS SDK exposes `_iconv_close`
in an executable linked with `-liconv`. The no-iconv check applies
**only to the isolated encoding-test executable**, not to final game
binaries.

This validates the **engine text-conversion layer**; it does not guarantee
that third-party libraries or the complete file set of every game avoid
`iconv` internally. To claim that for a distributed application, the final
binaries and the actually packaged dependencies must be audited as well.
