// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include "Air/System.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace Air {
std::string systemName(RLMachine &m, bool global, int index) {
  if (index < 0 || index > 65535)
    throw std::out_of_range("AIR name index");
  return m.GetStringValue(global ? 26 : 27, index);
}
bool systemCommand(RLMachine &m, const libreallive::CommandElement &c,
                   const std::function<int(const std::string &, int)> &config) {
  if (c.modtype() != 1 || c.module() != 4)
    return false;
  auto num = [&](int i) { return IntegerParameter(m, c, i); };
  auto param = [&](int i) { return Parameter(c, i); };
  auto state = [&](int index, int fallback) {
    auto it = m.memory().integers.find({26, index});
    return it == m.memory().integers.end() ? fallback : it->second;
  };
  auto set = [&](int index, int value) {
    m.memory().integers[{26, index}] = value;
  };
  auto cfg = [&](const std::string &key, int fallback) {
    return config ? config(key, fallback) : fallback;
  };
  const int op = c.opcode();
  if (op >= 462 && op <= 469) {
    int window = num(0);
    if (window < 0 || window > 65535)
      throw std::out_of_range("AIR animation window");
    const int field = (op - 462) / 2;
    int key = 10000 + window * 4 + field;
    if (op % 2)
      set(key, num(1));
    else {
      static const char *names[] = {"OPEN_ANM_MOD", "OPEN_ANM_TIME",
                                    "CLOSE_ANM_MOD", "CLOSE_ANM_TIME"};
      std::ostringstream path;
      path << "WINDOW." << std::setw(3) << std::setfill('0') << window << '.'
           << names[field];
      m.set_store_register(state(key, cfg(path.str(), field % 2 ? 300 : 1)));
    }
    return true;
  }
  if (op == 1300 || op == 1301 || op == 1310 || op == 1311) {
    int index = num(0);
    if (index < 0 || index > 65535)
      throw std::out_of_range("AIR name index");
    int bank = op < 1310 ? 26 : 27;
    if (op % 2)
      m.SetStringValue(bank, index, StringParameter(m, c, 1));
    else
      param(1).SetStringValue(m, m.GetStringValue(bank, index));
    return true;
  }
  if (op == 1710 || op == 1711) {
    int id = num(0);
    if (id < 0 || id > 65535)
      throw std::out_of_range("AIR edit field");
    if (op == 1710)
      m.SetStringValue(28, id, StringParameter(m, c, 1));
    else
      param(1).SetStringValue(m, m.GetStringValue(28, id));
    return true;
  }
  if (op == 800) {
    int index = num(0), offset = num(1), initial = num(2), value = initial;
    bool completed = false;
    for (std::size_t i = 3; i < c.GetParamCount(); ++i) {
      auto raw = c.GetParam(static_cast<int>(i));
      const char *source = raw.c_str();
      auto special = libreallive::GetData(source);
      if (!special.IsSpecialParameter())
        throw std::runtime_error("AIR index_series requires tagged terms");
      int tag = special.GetOverloadTag();
      const auto &parts = special.GetContainedPieces();
      if (tag == 0) {
        if (parts.size() != 1)
          throw std::runtime_error("AIR malformed series constant");
        if (completed)
          value = initial = parts[0].GetIntegerValue(m);
        continue;
      }
      if ((tag != 1 && tag != 2) ||
          parts.size() != static_cast<std::size_t>(tag == 1 ? 3 : 4))
        throw std::runtime_error("AIR malformed series interval");
      long long start = static_cast<long long>(parts[0].GetIntegerValue(m)) +
                        offset,
                end = static_cast<long long>(parts[1].GetIntegerValue(m)) +
                      offset;
      int target = parts[2].GetIntegerValue(m),
          mode = tag == 2 ? parts[3].GetIntegerValue(m) : 0;
      if (mode < 0 || mode > 2)
        throw std::runtime_error("AIR unsupported series interpolation");
      if (index >= end) {
        value = initial = target;
        completed = true;
      } else if (index > start) {
        double fraction = double(index - start) / double(end - start);
        if (mode)
          fraction = std::log1p(fraction) / std::log(2.0);
        value += static_cast<int>(fraction *
                                  (static_cast<double>(target) - initial));
        completed = false;
      }
    }
    m.set_store_register(value);
    return true;
  }
  // State-backed settings. Changes affecting sound/fullscreen still need the
  // presentation runtime to apply the corresponding external side effect.
  struct Setting {
    int setter, getter;
    const char *key;
    int fallback;
  };
  static constexpr Setting settings[] = {
      {2051, 2001, "SKIP_ANIMATIONS", 0},
      {2052, 2002, "LOW_PRIORITY", 0},
      {2221, 2321, "INIT_ORIGINALSETING1_MOD", 0},
      {2222, 2322, "INIT_ORIGINALSETING2_MOD", 0},
      {2223, 2323, "INIT_MESSAGE_SPEED", 30},
      {2224, 2324, "INIT_MESSAGE_SPEED_MOD", 0},
      {2225, 2325, "KOE_MODE", 0},
      {2226, 2326, "BGM_KOE_FADE_VOLUME", 128},
      {2227, 2327, "BGM_KOE_FADE", 0},
      {2230, 2330, "BGM_VOLUME", 255},
      {2231, 2331, "KOE_VOLUME", 255},
      {2232, 2332, "PCM_VOLUME", 255},
      {2233, 2333, "SE_VOLUME", 255},
      {2240, 2340, "BGM_ENABLED", 1},
      {2241, 2341, "KOE_ENABLED", 1},
      {2242, 2342, "PCM_ENABLED", 1},
      {2243, 2343, "SE_ENABLED", 1},
      {2250, 2350, "MESSAGE_KEY_WAIT_USE", 0},
      {2251, 2351, "AUTO_CHAR_TIME", 30},
      {2252, 2352, "MESSAGE_KEY_WAIT_TIME", 1000},
      {2256, 2356, "FONT_WEIGHT", 0},
      {2275, 2375, "SCREEN_MODE", 0}};
  for (const auto &setting : settings) {
    if (op == setting.setter) {
      set(setting.getter, num(0));
      return true;
    }
    if (op == setting.getter) {
      m.set_store_register(
          state(setting.getter, cfg(setting.key, setting.fallback)));
      return true;
    }
  }
  switch (op) {
  case 1002: {
    auto v = static_cast<long long>(num(0));
    m.set_store_register(
        static_cast<int>(std::min(v < 0 ? -v : v, 2147483647LL)));
    return true;
  }
  case 1007:
    m.set_store_register(std::min(num(0), num(1)));
    return true;
  case 1008:
    m.set_store_register(std::max(num(0), num(1)));
    return true;
  case 1009: {
    int v = num(1), lo = num(0), hi = num(2);
    if (lo > hi)
      std::swap(lo, hi);
    m.set_store_register(std::clamp(v, lo, hi));
    return true;
  }
  default:
    return false;
  }
}
} // namespace Air
