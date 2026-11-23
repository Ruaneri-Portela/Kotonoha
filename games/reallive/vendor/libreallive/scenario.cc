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

#include "libreallive/scenario.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <memory>
#include <sstream>
#include <string>

#include "libreallive/compression.h"

namespace libreallive {

Metadata::Metadata() : encoding_(0) {}

void Metadata::Assign(const char* input, size_t available) {
  if (available < 8)
    throw Error("Truncated RealLive metadata header");
  const long meta_len = read_i32(input);
  const long identifier_len = read_i32(input + 4);
  if (meta_len < 0 || identifier_len < 0)
    throw Error("Invalid RealLive metadata size");
  if (static_cast<size_t>(meta_len) > available)
    throw Error("RealLive metadata exceeds scenario bounds");
  // The stored identifier length excludes its terminator; the encoding byte
  // follows it by 17 more bytes. Preserve legacy handling of short metadata.
  if (static_cast<std::uint64_t>(identifier_len) + 18 >
      static_cast<std::uint64_t>(meta_len))
    return;
  as_string_.assign(input, static_cast<size_t>(meta_len));
  encoding_ = static_cast<unsigned char>(input[identifier_len + 17]);
}

Header::Header(const char* data, const size_t length) {
  if (length < 0x1d0)
    throw Error("not a RealLive bytecode file");

  string compiler = string(data, 4);

  // Check the version of the compiler.
  if (read_i32(data + 4) == 10002) {
    use_xor_2_ = false;
  } else if (read_i32(data + 4) == 110002) {
    use_xor_2_ = true;
  } else if (read_i32(data + 4) == 1110002) {
    use_xor_2_ = true;
  } else {
    // New xor key?
    std::ostringstream oss;
    oss << "Unsupported compiler version: " << read_i32(data + 4);
    throw Error(oss.str());
  }

  if (read_i32(data) != 0x1d0)
    throw Error("unsupported bytecode version");

  // Debug entrypoints
  z_minus_one_ = read_i32(data + 0x2c);
  z_minus_two_ = read_i32(data + 0x30);

  // Misc settings
  savepoint_message_ = read_i32(data + 0x1c4);
  savepoint_selcom_ = read_i32(data + 0x1c8);
  savepoint_seentop_ = read_i32(data + 0x1cc);

  // Dramatis personae
  const long dplen = read_i32(data + 0x18);
  const long raw_dramatis_offset = read_i32(data + 0x14);
  if (dplen < 0 || static_cast<size_t>(dplen) > length / 5)
    throw Error("Invalid RealLive character-name count");
  if (raw_dramatis_offset < 0 ||
      static_cast<size_t>(raw_dramatis_offset) > length)
    throw Error("Invalid RealLive character-name offset");
  dramatis_personae_.reserve(static_cast<size_t>(dplen));
  size_t offs = static_cast<size_t>(raw_dramatis_offset);
  for (long i = 0; i < dplen; ++i) {
    if (length - offs < 4)
      throw Error("Truncated RealLive character-name length");
    const long elen = read_i32(data + offs);
    if (elen < 1 || static_cast<size_t>(elen) > length - offs - 4)
      throw Error("Invalid RealLive character-name length");
    dramatis_personae_.emplace_back(data + offs + 4,
                                   static_cast<size_t>(elen - 1));
    offs += 4 + static_cast<size_t>(elen);
  }

  // RLdev metadata, when present, lies between the dramatis personae
  // region and the compressed bytecode. Validate its offset and length.
  const std::int64_t raw_meta_offset =
      static_cast<std::int64_t>(raw_dramatis_offset) +
      static_cast<std::int64_t>(read_i32(data + 0x1c));
  const long raw_code_offset = read_i32(data + 0x20);
  if (raw_meta_offset != 0 && raw_meta_offset != raw_code_offset) {
    if (raw_meta_offset < 0 ||
        static_cast<std::uint64_t>(raw_meta_offset) > length)
      throw Error("Invalid RealLive metadata offset");
    const size_t meta_offset = static_cast<size_t>(raw_meta_offset);
    rldev_metadata_.Assign(data + meta_offset, length - meta_offset);
  }
}

Header::~Header() {}

Script::Script(const Header& hdr,
               const char* data,
               const size_t length,
               const std::string& regname,
               bool use_xor_2,
               const compression::XorKey* second_level_xor_key) {
  // Kidoku/entrypoint table
  // All section offsets are untrusted bytecode input. Validate using
  // subtraction, avoiding signed overflow and out-of-bounds pointer math.
  const auto sectionOffset = [length](long offset, size_t bytes) {
    if (offset < 0 || static_cast<size_t>(offset) > length ||
        bytes > length - static_cast<size_t>(offset))
      throw Error("RealLive scenario section exceeds file bounds");
    return static_cast<size_t>(offset);
  };
  const long kidoku_count = read_i32(data + 0x0c);
  if (kidoku_count < 0 ||
      static_cast<size_t>(kidoku_count) > length / 4)
    throw Error("Invalid RealLive kidoku table length");
  const size_t kidoku_length = static_cast<size_t>(kidoku_count);
  const size_t kidoku_offs =
      sectionOffset(read_i32(data + 0x08), kidoku_length * 4);
  ConstructionData cdat(kidoku_length, elts_.end());
  for (size_t i = 0; i < kidoku_length; ++i)
    cdat.kidoku_table[i] = read_i32(data + kidoku_offs + i * 4);

  // A corrupt header must not request gigabytes of script allocation.
  const long decoded_length = read_i32(data + 0x24);
  constexpr long kMaxScenarioBytes = 256L * 1024L * 1024L;
  if (decoded_length < 0 || decoded_length > kMaxScenarioBytes)
    throw Error("Invalid RealLive decoded script length");
  const size_t dlen = static_cast<size_t>(decoded_length);
  const long compressed_length = read_i32(data + 0x28);
  if (compressed_length < 0)
    throw Error("Invalid RealLive compressed script length");
  const size_t compressed_offs = sectionOffset(
      read_i32(data + 0x20), static_cast<size_t>(compressed_length));

  const compression::XorKey* key = NULL;
  if (use_xor_2) {
    if (second_level_xor_key) {
      key = second_level_xor_key;
    } else {
      // Probably safe to assume that any game we don't know about has a
      // Japanese encoding.
      throw Error("Unknown script XOR key: " + regname);
    }
  }

  // RAII keeps the decoded allocation alive during bytecode parsing and
  // releases it if either decoding or an opcode constructor throws.
  std::unique_ptr<char[]> uncompressed(new char[dlen]);
  compression::Decompress(data + compressed_offs,
                          static_cast<size_t>(compressed_length),
                          uncompressed.get(), dlen, key);
  // Read bytecode
  const char* stream = uncompressed.get();
  const char* end = stream + dlen;
  size_t pos = 0;
  pointer_t it = elts_.before_begin();
  while (pos < dlen) {
    // Read element
    it = elts_.emplace_after(it, BytecodeElement::Read(stream, end, cdat));
    cdat.offsets[pos] = it;

    // Keep track of the entrypoints
    int entrypoint = (*it)->GetEntrypoint();
    if (entrypoint != BytecodeElement::kInvalidEntrypoint)
      entrypoint_associations_.emplace(entrypoint, it);

    // Advance
    size_t l = (*it)->GetBytecodeLength();
    if (l == 0)
      l = 1;  // Failsafe: always advance at least one byte.
    if (l > dlen - pos)
      throw Error("RealLive bytecode element exceeds decoded script");
    stream += l;
    pos += l;
  }

  // Resolve pointers
  for (auto& element : elts_) {
    element->SetPointers(cdat);
  }

}

Script::~Script() {}

const pointer_t Script::GetEntrypoint(int entrypoint) const {
  pointernumber::const_iterator it = entrypoint_associations_.find(entrypoint);
  if (it == entrypoint_associations_.end())
    throw Error("Unknown entrypoint");

  return it->second;
}

Scenario::Scenario(const char* data, const size_t length, int sn,
                   const std::string& regname,
                   const compression::XorKey* second_level_xor_key)
  : header(data, length),
    script(header, data, length, regname,
           header.use_xor_2_, second_level_xor_key),
    scenario_number_(sn) {
}

Scenario::Scenario(const FilePos& fp, int sn,
                   const std::string& regname,
                   const compression::XorKey* second_level_xor_key)
  : header(fp.data, fp.length),
    script(header, fp.data, fp.length, regname,
           header.use_xor_2_, second_level_xor_key),
    scenario_number_(sn) {
}

Scenario::~Scenario() {}

Scenario::const_iterator Scenario::FindEntrypoint(int entrypoint) const {
  return script.GetEntrypoint(entrypoint);
}

}  // namespace libreallive
