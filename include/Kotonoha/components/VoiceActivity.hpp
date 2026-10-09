#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Kotonoha {

struct VoiceActivityRecord {
	std::uint64_t tick = 0;
	bool active = false;
};

struct MouthAnimationState {
	std::uint32_t counter = 0;
	std::uint8_t index = 0;
	std::uint64_t lastTick = 0;
	bool initialized = false;
};

inline bool SampleVoiceActivity(
	const std::vector<std::int16_t>& samples,
	std::size_t processedBytes,
	std::size_t chunkBytes,
	std::uint64_t recordIndex) {
	const std::int64_t localIndex =
		static_cast<std::int64_t>((chunkBytes * recordIndex) / 23) -
		static_cast<std::int64_t>(processedBytes / 2);
	if (localIndex < 0 ||
		localIndex >= static_cast<std::int64_t>(chunkBytes / 2)) {
		return false;
	}
	const std::size_t index = processedBytes / 2 +
		static_cast<std::size_t>(localIndex);
	if (index >= samples.size()) {
		return false;
	}
	const std::int16_t value = samples[index];
	return value > 60 || value <= -60;
}

inline std::vector<VoiceActivityRecord> AnalyzeVoiceActivity(
	const std::vector<std::int16_t>& samples) {
	constexpr std::size_t chunkBytes = 44100;
	const std::size_t totalBytes = samples.size() * sizeof(std::int16_t);
	std::vector<VoiceActivityRecord> records;
	std::uint64_t recordIndex = 0;

	for (std::size_t processed = 0; processed < totalBytes;
		processed += chunkBytes) {
		const std::size_t bytes =
			std::min(chunkBytes, totalBytes - processed);
		const float limit = static_cast<float>(
			(static_cast<double>(processed + bytes) / 44100.0) * 11.5);
		while (static_cast<float>(recordIndex) <= limit) {
			records.push_back({ recordIndex,
				SampleVoiceActivity(samples, processed, bytes, recordIndex) });
			++recordIndex;
		}
	}
	return records;
}

inline bool VoiceActivityAt(
	const std::vector<VoiceActivityRecord>& records,
	std::uint64_t relativeTick) {
	if (records.empty()) {
		return false;
	}

	const auto it = std::upper_bound(records.begin(), records.end(),
		relativeTick,
		[](std::uint64_t tick, const VoiceActivityRecord& record) {
			return tick < record.tick;
		});
	return it != records.begin() && relativeTick <= records.back().tick &&
		(it - 1)->active;
}

inline void AdvanceMouthAnimation(
	MouthAnimationState& state,
	const std::vector<VoiceActivityRecord>& records,
	std::uint64_t voiceStartTick,
	std::uint64_t tick) {
	if (tick < voiceStartTick) {
		return;
	}

	if (!VoiceActivityAt(records, tick - voiceStartTick)) {
		state.index = 0;
		return;
	}

	if (tick % 3 == 0) {
		++state.counter;
	}
	state.index = static_cast<std::uint8_t>(state.counter % 3);
}

inline MouthAnimationState ComputeMouthAnimationAt(
	const std::vector<VoiceActivityRecord>& records,
	std::uint64_t voiceStartTick,
	std::uint64_t targetTick) {
	MouthAnimationState state;
	if (targetTick < voiceStartTick) {
		return state;
	}
	for (std::uint64_t tick = voiceStartTick; tick <= targetTick; ++tick) {
		AdvanceMouthAnimation(state, records, voiceStartTick, tick);
	}
	return state;
}

} // namespace Kotonoha
