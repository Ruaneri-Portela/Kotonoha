// -*- Mode: C++; tab-width:2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi:tw=80:et:ts=2:sts=2
//
// -----------------------------------------------------------------------
//
// This file is part of libreallive, a dependency of RLVM.
//
// -----------------------------------------------------------------------
//
// Copyright (c) 2006, 2007 Peter Jolly
//
// Permission is hereby granted, free of charge, to any person
// obtaining a copy of this software and associated documentation
// files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// -----------------------------------------------------------------------

#ifndef SRC_LIBREALLIVE_FILEMAP_H_
#define SRC_LIBREALLIVE_FILEMAP_H_

#include <cstddef>
#include <cstdio>
#include <string>
#include <sys/types.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "libreallive/defs.h"

namespace libreallive {

enum Mode { Read, Write };

class Mapping {
public:
  Mapping(string filename, Mode mode, off_t min_size = 0);
  ~Mapping();

  // Prevent copying a memory mapping.
  Mapping(const Mapping &) = delete;
  Mapping &operator=(const Mapping &) = delete;

  // Return a pointer to the mapped memory.
  char *get() { return static_cast<char *>(mem); }

  const char *get() const { return static_cast<const char *>(mem); }

  // Return mapping size in bytes.
  size_t size() const { return len; }

private:
  void mopen();
  void mclose();

#ifdef _WIN32
  HANDLE fp;
#else
  int fp;
#endif

  void *mem = nullptr;
  bool mapped = false;
  size_t len = 0;

  string fn_;
  Mode mode_;
  off_t msz_;
};

} // namespace libreallive

#endif // SRC_LIBREALLIVE_FILEMAP_H_
