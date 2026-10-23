#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace Kotonoha {
/* Independent analysis decode. Does not alter the float playback decoder. */
bool DecodeSchoolDaysVoicePcm(const std::string& path,
                             std::vector<int16_t>& monoS16At44100,
                             const std::atomic_bool* cancelled = nullptr);
}
