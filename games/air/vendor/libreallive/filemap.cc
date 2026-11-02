// -*- Mode: C++; tab-width:2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi:tw=80:et:ts=2:sts=2
//
// -----------------------------------------------------------------------
//
// This file is part of libreallive, a dependency of RLVM.
//
// -----------------------------------------------------------------------
//
// Copyright (c) 2006 Peter Jolly
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


#include "libreallive/filemap.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace libreallive {

#ifdef _WIN32

void Mapping::mclose() {
  if (mem) {
    if (mapped)
      UnmapViewOfFile(mem);
    else
      delete[] static_cast<char*>(mem);
  }

  mem = nullptr;
  mapped = false;

  if (fp != INVALID_HANDLE_VALUE) {
    CloseHandle(fp);
    fp = INVALID_HANDLE_VALUE;
  }
}

void Mapping::mopen() {
  const bool readonly = (mode_ == Read);

  fp = CreateFileA(
      fn_.c_str(),
      readonly ? GENERIC_READ : (GENERIC_READ | GENERIC_WRITE),
      FILE_SHARE_READ | FILE_SHARE_WRITE,
      nullptr,
      readonly ? OPEN_EXISTING : OPEN_ALWAYS,
      FILE_ATTRIBUTE_NORMAL,
      nullptr);

  if (fp == INVALID_HANDLE_VALUE) {
    if (readonly && GetLastError() == ERROR_FILE_NOT_FOUND)
      throw Error("File not found");
    throw Error("Could not open file");
  }

  LARGE_INTEGER fileSize;
  if (!GetFileSizeEx(fp, &fileSize)) {
    mclose();
    throw Error("Could not get file size");
  }

  const unsigned long long existingSize =
      static_cast<unsigned long long>(fileSize.QuadPart);

  const unsigned long long requestedSize =
      static_cast<unsigned long long>(msz_);

  const unsigned long long targetSize =
      std::max(existingSize, requestedSize);

  if (targetSize == 0 ||
      targetSize > std::numeric_limits<size_t>::max()) {
    mclose();
    throw Error("Invalid mapping size");
  }

  len = static_cast<size_t>(targetSize);

  if (readonly && targetSize > existingSize) {
    mclose();
    throw Error("Read mapping exceeds file size");
  }

  if (!readonly && targetSize > existingSize) {
    LARGE_INTEGER newSize;
    newSize.QuadPart = static_cast<LONGLONG>(targetSize);

    if (!SetFilePointerEx(fp, newSize, nullptr, FILE_BEGIN) ||
        !SetEndOfFile(fp)) {
      mclose();
      throw Error("Could not resize file");
    }
  }

  HANDLE mapping = CreateFileMappingA(
      fp,
      nullptr,
      readonly ? PAGE_READONLY : PAGE_READWRITE,
      0,
      0,
      nullptr);

  if (!mapping) {
    mclose();
    throw Error("Could not create file mapping");
  }

  mem = MapViewOfFile(
      mapping,
      readonly ? FILE_MAP_READ : FILE_MAP_WRITE,
      0,
      0,
      0);

  CloseHandle(mapping);

  if (!mem) {
    mclose();
    throw Error("Could not map memory");
  }

  mapped = true;
}

#else

void Mapping::mclose() {
  if (mem) {
    if (mapped)
      munmap(mem, len);
    else
      delete[] static_cast<char*>(mem);
  }

  mem = nullptr;
  mapped = false;

  if (fp != -1) {
    close(fp);
    fp = -1;
  }
}

void Mapping::mopen() {
  const bool readonly = (mode_ == Read);

  fp = open(
      fn_.c_str(),
      readonly ? O_RDONLY : (O_RDWR | O_CREAT),
      0644);

  if (fp == -1) {
    if (readonly && errno == ENOENT)
      throw Error("File not found");
    throw Error("Could not open file");
  }

  struct stat st;
  if (fstat(fp, &st) != 0) {
    mclose();
    throw Error("Could not stat file");
  }

  const off_t targetSize =
      std::max(static_cast<off_t>(msz_), st.st_size);

  if (targetSize <= 0 ||
      static_cast<unsigned long long>(targetSize) >
          std::numeric_limits<size_t>::max()) {
    mclose();
    throw Error("Invalid mapping size");
  }

  len = static_cast<size_t>(targetSize);

  if (readonly && targetSize > st.st_size) {
    mclose();
    throw Error("Read mapping exceeds file size");
  }

  if (!readonly && targetSize > st.st_size) {
    if (ftruncate(fp, targetSize) != 0) {
      mclose();
      throw Error("Could not resize file");
    }
  }

  mem = mmap(
      nullptr,
      len,
      readonly ? PROT_READ : (PROT_READ | PROT_WRITE),
      MAP_SHARED,
      fp,
      0);

  if (mem == MAP_FAILED) {
    mem = nullptr;

    if (!readonly) {
      mclose();
      throw Error("Could not map memory");
    }

    // Fallback for read-only mapping.
    mem = new char[len];

    size_t total = 0;
    while (total < len) {
      ssize_t count = pread(
          fp,
          static_cast<char*>(mem) + total,
          len - total,
          static_cast<off_t>(total));

      if (count <= 0) {
        mclose();
        throw Error("Could not read file");
      }

      total += static_cast<size_t>(count);
    }

    mapped = false;
  } else {
    mapped = true;
  }
}

#endif

Mapping::Mapping(string filename, Mode mode, off_t min_size)
#ifdef _WIN32
    : fp(INVALID_HANDLE_VALUE),
#else
    : fp(-1),
#endif
      mem(nullptr),
      mapped(false),
      len(0),
      fn_(filename),
      mode_(mode),
      msz_(min_size) {
  if (min_size < 0)
    throw Error("Invalid minimum size");

  mopen();
}

Mapping::~Mapping() {
  mclose();
}

}  // namespace libreallive
