MIT portions of libreallive from https://github.com/eglaysher/rlvm,
commit e38cda7783dc67539ce27901596ed93a2bb5c826. Individual original copyright
and permission notices are retained. This folder excludes upstream GPL
intmemref and utility implementation files. intmemref.h and ../machine are
independently written Kotonoha adapters, licensed MIT.

Local changes: standard-library replacements for Boost, portable C++17 byte
constants, archive bounds checks, and removal of GPL error-formatting utilities.
Compile parser sources with unsigned char (`-funsigned-char`) because the
upstream implementation assumes unsigned bytes.

This is the script parser and expression evaluator only. Rendering, opcodes,
control flow, persistence and rlBabel compatibility belong to the AIR runtime.
