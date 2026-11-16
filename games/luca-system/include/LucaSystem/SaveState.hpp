// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#pragma once

// LucaSystem save payload. Version 3 is length-prefixed and preserves binary
// and whitespace inside string variables, plus VM call/wait/dialogue state.
// This codec deliberately has no SDL, PAK or game-data dependency so that
// malformed saves can be tested without any commercial assets.
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace LucaSystem {

struct SaveState {
  std::string scenario;
  std::size_t instructionIndex = 0;
  std::size_t dialogueIndex = 0;
  bool dialogueVisible = false;
  std::string dialogueText;
  bool waiting = false;
  std::uint32_t waitRemainingMs = 0;
  std::map<std::uint32_t, std::int32_t> numbers;
  std::map<std::uint32_t, std::string> strings;
  std::map<std::uint32_t, bool> flags;
  std::vector<std::size_t> callStack;
};

namespace SaveCodec {
constexpr std::size_t MaxPayload = 16u << 20;
constexpr std::uint32_t MaxEntries = 65536;
constexpr std::uint32_t MaxString = 1u << 20;
constexpr std::uint32_t MaxWaitMs = 60u * 60u * 1000u;
constexpr std::string_view Magic = "LUCA_SYSTEM_SAVE_3\n";

inline void u32(std::string &out, std::uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    out.push_back(static_cast<char>((value >> (8 * i)) & 0xff));
}
inline void u64(std::string &out, std::uint64_t value) {
  for (unsigned i = 0; i < 8; ++i)
    out.push_back(static_cast<char>((value >> (8 * i)) & 0xff));
}
inline void string(std::string &out, std::string_view value) {
  if (value.size() > MaxString)
    throw std::runtime_error("LucaSystem save string exceeds limit");
  u32(out, static_cast<std::uint32_t>(value.size()));
  if (!value.empty())
    out.append(value.data(), value.size());
}
inline void count(std::string &out, std::size_t value) {
  if (value > MaxEntries)
    throw std::runtime_error("LucaSystem save collection exceeds limit");
  u32(out, static_cast<std::uint32_t>(value));
}
struct Reader {
  std::string_view src;
  std::size_t pos;
  std::uint32_t get32() {
    if (src.size() - pos < 4)
      throw std::runtime_error("Truncated LucaSystem save integer");
    std::uint32_t result = 0;
    for (unsigned i = 0; i < 4; ++i)
      result |= std::uint32_t(static_cast<unsigned char>(src[pos++])) << (8 * i);
    return result;
  }
  std::uint64_t get64() {
    if (src.size() - pos < 8)
      throw std::runtime_error("Truncated LucaSystem save offset");
    std::uint64_t result = 0;
    for (unsigned i = 0; i < 8; ++i)
      result |= std::uint64_t(static_cast<unsigned char>(src[pos++])) << (8 * i);
    return result;
  }
  std::size_t offset() {
    const auto v = get64();
    if (v > std::numeric_limits<std::size_t>::max())
      throw std::runtime_error("LucaSystem save offset exceeds host limit");
    return static_cast<std::size_t>(v);
  }
  std::uint32_t getCount() {
    const auto n = get32();
    if (n > MaxEntries)
      throw std::runtime_error("LucaSystem save collection exceeds limit");
    return n;
  }
  bool boolean() {
    const auto value = get32();
    if (value > 1)
      throw std::runtime_error("Invalid LucaSystem save boolean");
    return value != 0;
  }
  std::string getString() {
    const auto n = get32();
    if (n > MaxString || n > src.size() - pos)
      throw std::runtime_error("Invalid LucaSystem save string");
    auto result = std::string(src.substr(pos, n));
    pos += n;
    return result;
  }
  void finish() const {
    if (pos != src.size())
      throw std::runtime_error("Trailing LucaSystem save bytes");
  }
};
inline std::int32_t signed32(std::uint32_t value) {
  std::int32_t result;
  std::memcpy(&result, &value, sizeof result);
  return result;
}

inline SaveState decodeLegacy(std::string_view bytes) {
  SaveState state;
  std::istringstream in{std::string(bytes)};
  std::string header;
  if (!std::getline(in, header))
    throw std::runtime_error("Empty LucaSystem save");
  if (header != "LUCA_SYSTEM_SAVE_2") {
    if (header.rfind("LUCA_SYSTEM_SAVE_", 0) == 0)
      throw std::runtime_error("Unsupported LucaSystem save version");
    if (header.empty() || header.size() > MaxString)
      throw std::runtime_error("Invalid LucaSystem legacy scenario");
    // Original version stored only the scenario name in the payload.
    state.scenario = std::move(header);
    return state;
  }
  if (!std::getline(in, state.scenario) || state.scenario.empty() ||
      state.scenario.size() > MaxString ||
      !(in >> state.instructionIndex >> state.dialogueIndex >>
        state.dialogueVisible))
    throw std::runtime_error("Invalid LucaSystem V2 save header");
  auto getCount = [&]() -> std::size_t {
    std::size_t n = 0;
    if (!(in >> n) || n > MaxEntries)
      throw std::runtime_error("Invalid LucaSystem V2 save count");
    return n;
  };
  for (std::size_t i = getCount(); i > 0; --i) {
    std::uint32_t key;
    std::int32_t value;
    if (!(in >> key >> value) || !state.numbers.emplace(key, value).second)
      throw std::runtime_error("Invalid LucaSystem V2 number");
  }
  for (std::size_t i = getCount(); i > 0; --i) {
    std::uint32_t key;
    std::string value;
    // V2 separated strings on whitespace. Whitespace already lost in
    // existing saves cannot be reconstructed; retain readable V2 entries.
    if (!(in >> key >> value) || value.size() > MaxString ||
        !state.strings.emplace(key, std::move(value)).second)
      throw std::runtime_error("Invalid LucaSystem V2 string");
  }
  for (std::size_t i = getCount(); i > 0; --i) {
    std::uint32_t key;
    int value;
    if (!(in >> key >> value) || (value != 0 && value != 1) ||
        !state.flags.emplace(key, value != 0).second)
      throw std::runtime_error("Invalid LucaSystem V2 flag");
  }
  in >> std::ws;
  if (!in.eof())
    throw std::runtime_error("Trailing LucaSystem V2 save data");
  return state;
}
} // namespace SaveCodec

inline std::string encodeSaveState(const SaveState &state) {
  using namespace SaveCodec;
  if (state.scenario.empty())
    throw std::runtime_error("Cannot save LucaSystem without a scenario");
  std::string out(Magic);
  string(out, state.scenario);
  u64(out, state.instructionIndex);
  u64(out, state.dialogueIndex);
  u32(out, state.dialogueVisible ? 1u : 0u);
  string(out, state.dialogueText);
  u32(out, state.waiting ? 1u : 0u);
  if (state.waitRemainingMs > MaxWaitMs)
    throw std::runtime_error("LucaSystem save wait exceeds limit");
  u32(out, state.waitRemainingMs);
  count(out, state.numbers.size());
  for (const auto &[key, value] : state.numbers) {
    u32(out, key);
    u32(out, static_cast<std::uint32_t>(value));
  }
  count(out, state.strings.size());
  for (const auto &[key, value] : state.strings) {
    u32(out, key);
    string(out, value);
  }
  count(out, state.flags.size());
  for (const auto &[key, value] : state.flags) {
    u32(out, key);
    u32(out, value ? 1u : 0u);
  }
  count(out, state.callStack.size());
  for (const auto pc : state.callStack)
    u64(out, pc);
  if (out.size() > MaxPayload)
    throw std::runtime_error("LucaSystem save exceeds size limit");
  return out;
}

inline SaveState decodeSaveState(std::string_view bytes) {
  using namespace SaveCodec;
  if (bytes.empty() || bytes.size() > MaxPayload)
    throw std::runtime_error("Invalid LucaSystem save payload size");
  if (bytes.substr(0, Magic.size()) != Magic)
    return decodeLegacy(bytes);
  Reader in{bytes, Magic.size()};
  SaveState state;
  state.scenario = in.getString();
  if (state.scenario.empty())
    throw std::runtime_error("Missing LucaSystem save scenario");
  state.instructionIndex = in.offset();
  state.dialogueIndex = in.offset();
  state.dialogueVisible = in.boolean();
  state.dialogueText = in.getString();
  state.waiting = in.boolean();
  state.waitRemainingMs = in.get32();
  if (state.waitRemainingMs > MaxWaitMs ||
      (!state.waiting && state.waitRemainingMs != 0))
    throw std::runtime_error("Invalid LucaSystem save wait");
  for (std::uint32_t i = in.getCount(); i > 0; --i) {
    const auto key = in.get32();
    const auto value = signed32(in.get32());
    if (!state.numbers.emplace(key, value).second)
      throw std::runtime_error("Duplicate LucaSystem save number");
  }
  for (std::uint32_t i = in.getCount(); i > 0; --i) {
    const auto key = in.get32();
    const auto value = in.getString();
    if (!state.strings.emplace(key, value).second)
      throw std::runtime_error("Duplicate LucaSystem save string");
  }
  for (std::uint32_t i = in.getCount(); i > 0; --i) {
    const auto key = in.get32();
    const auto value = in.boolean();
    if (!state.flags.emplace(key, value).second)
      throw std::runtime_error("Duplicate LucaSystem save flag");
  }
  for (std::uint32_t i = in.getCount(); i > 0; --i)
    state.callStack.push_back(in.offset());
  in.finish();
  return state;
}

} // namespace LucaSystem
