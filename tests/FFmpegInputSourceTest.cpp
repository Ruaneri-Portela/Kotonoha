#include <Kotonoha/utils/Demuxer.h>
#include <Kotonoha/utils/FFmpeg.h>
#include <Kotonoha/utils/VoicePcm.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

struct MemoryInput {
    std::vector<uint8_t> bytes;
    int64_t position = 0;
    int* closeCount = nullptr;
};

[[noreturn]] void Fail(const char* message) {
    std::cerr << "FFmpegInputSourceTest: " << message << '\n';
    std::exit(1);
}

void Check(bool condition, const char* message) {
    if (!condition) Fail(message);
}

void Append16(std::vector<uint8_t>* out, uint16_t value) {
    out->push_back(static_cast<uint8_t>(value));
    out->push_back(static_cast<uint8_t>(value >> 8u));
}

void Append32(std::vector<uint8_t>* out, uint32_t value) {
    out->push_back(static_cast<uint8_t>(value));
    out->push_back(static_cast<uint8_t>(value >> 8u));
    out->push_back(static_cast<uint8_t>(value >> 16u));
    out->push_back(static_cast<uint8_t>(value >> 24u));
}

void AppendTag(std::vector<uint8_t>* out, const char tag[4]) {
    out->insert(out->end(), tag, tag + 4);
}

std::vector<uint8_t> MakeWav() {
    constexpr uint32_t sampleRate = 8000u;
    std::vector<int16_t> samples(800u);
    for (size_t i = 0; i < samples.size(); ++i) {
        const int phase = static_cast<int>(i % 32u) - 16;
        samples[i] = static_cast<int16_t>(phase * 120);
    }
    const uint32_t dataBytes =
        static_cast<uint32_t>(samples.size() * sizeof(int16_t));

    std::vector<uint8_t> out;
    out.reserve(44u + dataBytes);
    AppendTag(&out, "RIFF");
    Append32(&out, 36u + dataBytes);
    AppendTag(&out, "WAVE");
    AppendTag(&out, "fmt ");
    Append32(&out, 16u);
    Append16(&out, 1u);
    Append16(&out, 1u);
    Append32(&out, sampleRate);
    Append32(&out, sampleRate * 2u);
    Append16(&out, 2u);
    Append16(&out, 16u);
    AppendTag(&out, "data");
    Append32(&out, dataBytes);
    const auto* sampleBytes = reinterpret_cast<const uint8_t*>(samples.data());
    out.insert(out.end(), sampleBytes, sampleBytes + dataBytes);
    return out;
}

int ReadMemory(void* opaque, Uint8* buffer, int bufferSize) {
    auto* input = static_cast<MemoryInput*>(opaque);
    if (input == nullptr || buffer == nullptr || bufferSize <= 0)
        return AVERROR(EINVAL);
    if (input->position < 0 ||
        static_cast<uint64_t>(input->position) >= input->bytes.size())
        return 0;

    const size_t available =
        input->bytes.size() - static_cast<size_t>(input->position);
    const size_t count = std::min(available, static_cast<size_t>(bufferSize));
    std::memcpy(buffer, input->bytes.data() + input->position, count);
    input->position += static_cast<int64_t>(count);
    return static_cast<int>(count);
}

Sint64 SeekMemory(void* opaque, Sint64 offset, int whence) {
    auto* input = static_cast<MemoryInput*>(opaque);
    if (input == nullptr) return AVERROR(EINVAL);

    int64_t base = 0;
    switch (whence) {
    case SEEK_SET:
        base = 0;
        break;
    case SEEK_CUR:
        base = input->position;
        break;
    case SEEK_END:
        base = static_cast<int64_t>(input->bytes.size());
        break;
    default:
        return AVERROR(EINVAL);
    }
    if ((offset > 0 && base > INT64_MAX - offset) ||
        (offset < 0 && base < INT64_MIN - offset))
        return AVERROR(EINVAL);
    const int64_t target = base + offset;
    if (target < 0 || static_cast<uint64_t>(target) > input->bytes.size())
        return AVERROR(EINVAL);
    input->position = target;
    return target;
}

Sint64 SizeMemory(void* opaque) {
    const auto* input = static_cast<const MemoryInput*>(opaque);
    return input == nullptr ? AVERROR(EINVAL)
                            : static_cast<Sint64>(input->bytes.size());
}

void CloseMemory(void* opaque) {
    auto* input = static_cast<MemoryInput*>(opaque);
    if (input == nullptr) return;
    if (input->closeCount != nullptr) ++*input->closeCount;
    delete input;
}

Kotonoha_FFmpegInput MakeInput(const std::vector<uint8_t>& bytes,
                               int* closeCount) {
    auto* memory = new MemoryInput{bytes, 0, closeCount};
    Kotonoha_FFmpegInput input{};
    input.name = "memory.wav";
    input.opaque = memory;
    input.read = &ReadMemory;
    input.seek = &SeekMemory;
    input.size = &SizeMemory;
    input.close = &CloseMemory;
    return input;
}

void CheckDirectLoad(const std::vector<uint8_t>& wav) {
    int closes = 0;
    Kotonoha_FFmpegInput input = MakeInput(wav, &closes);

    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    Kotonoha_FFmpegInputContext* inputContext = nullptr;
    int streamIndex = -1;
    Check(KotonohaFFmpegLoadInput(
              &input, &format, &codec, AVMEDIA_TYPE_AUDIO, &streamIndex, 0,
              nullptr, &inputContext),
          "custom AVIO WAV failed to open");
    Check(format != nullptr && codec != nullptr && inputContext != nullptr,
          "custom AVIO load returned incomplete state");
    Check(streamIndex >= 0, "custom AVIO audio stream was not selected");
    Check(closes == 0, "custom source closed before decoder teardown");

    Check(KotonohaFFmpegSeekFrame(format, streamIndex, 0, AVSEEK_FLAG_BACKWARD) >= 0,
          "custom AVIO input did not seek");

    avcodec_free_context(&codec);
    KotonohaFFmpegCloseInput(&format, &inputContext);
    Check(format == nullptr && inputContext == nullptr,
          "custom AVIO close left live handles");
    Check(closes == 1, "custom source close callback did not run exactly once");
}

void CheckDemuxerOwnership(const std::vector<uint8_t>& wav) {
    int closes = 0;
    Kotonoha_FFmpegInput input = MakeInput(wav, &closes);
    Kotonoha_Config config{};
    config.mediaCacheSeconds = 0.25f;
    Kotonoha_Demuxer* demuxer = KotonohaDemuxerCreate(false, &config);
    Check(demuxer != nullptr, "demuxer allocation failed");

    Kotonoha_DemuxStream* stream = KotonohaDemuxerAddStreamInput(
        demuxer, &input, nullptr, AVMEDIA_TYPE_AUDIO);
    Check(stream != nullptr, "demuxer rejected custom AVIO input");
    Check(stream->inputContext != nullptr,
          "demuxer did not retain custom AVIO ownership");
    Check(closes == 0, "demuxer source closed while stream was live");

    KotonohaDemuxerRemoveStream(demuxer, &stream);
    Check(stream == nullptr, "demuxer stream removal did not clear handle");
    Check(closes == 1, "demuxer did not close custom source exactly once");
    KotonohaDemuxerDestroy(&demuxer);
}

void CheckVoicePcm(const std::vector<uint8_t>& wav) {
    int closes = 0;
    Kotonoha_FFmpegInput input = MakeInput(wav, &closes);
    Sint16* samples = nullptr;
    size_t sampleCount = 0;
    Check(KotonohaPcmDecodeVoiceInput(&input, &samples, &sampleCount),
          "voice PCM decoder rejected custom AVIO input");
    Check(samples != nullptr && sampleCount != 0u,
          "voice PCM decoder returned no samples");
    Check(closes == 1, "voice PCM decoder did not close custom source once");
    SDL_free(samples);
}

} // namespace

int main() {
    const auto wav = MakeWav();
    CheckDirectLoad(wav);
    CheckDemuxerOwnership(wav);
    CheckVoicePcm(wav);
    return 0;
}
