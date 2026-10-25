#include <Kotonoha/audio/VoicePcm.hpp>
#include <Kotonoha/utils/Log.h>

#include <algorithm>
#include <limits>

extern "C" {
#include <Kotonoha/utils/FFmpeg.h>
#include <libswresample/swresample.h>
}

namespace Kotonoha {
bool DecodeVoicePcm(const std::string& path, std::vector<int16_t>& output,
                    const std::atomic_bool* cancelled,
                    uint64_t maxDurationMs) {
    // This is an analysis-only auxiliary decode and may run on a shared gameplay
    // worker. A malformed/custom AVIO stream must never be able to keep that
    // worker inside av_read_frame forever or grow the PCM vector without bound.
    // Callers with an authored event window can supply its duration; generic
    // callers fall back to the container duration and finally to a conservative
    // absolute ceiling.
    constexpr uint64_t kSampleRate = 44100;
    constexpr uint64_t kAbsoluteMaxDurationMs = 10ull * 60ull * 1000ull;
    constexpr uint64_t kDurationGuardMs = 2000;
    output.clear();
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    SwrContext* resampler = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    Kotonoha_FFmpegInputContext* inputContext = nullptr;
    Kotonoha_FFmpegInput input{};
    bool success = false;
    const int virtualInput = KotonohaFFmpegResolveInput(path.c_str(), &input);
    do {
        if (virtualInput < 0) break;
        int stream = -1;
        const bool loaded = virtualInput > 0
            ? KotonohaFFmpegLoadInput(
                  &input, &format, &codec, AVMEDIA_TYPE_AUDIO, &stream, 0,
                  nullptr, &inputContext)
            : KotonohaFFmpegLoad(
                  path.c_str(), &format, &codec, AVMEDIA_TYPE_AUDIO, &stream,
                  0, nullptr);
        if (!loaded) break;

        uint64_t boundedDurationMs = maxDurationMs;
        if (boundedDurationMs == 0 && format->duration != AV_NOPTS_VALUE &&
            format->duration > 0) {
            const uint64_t durationUs = static_cast<uint64_t>(format->duration);
            boundedDurationMs = durationUs / 1000ull;
            if (boundedDurationMs <=
                std::numeric_limits<uint64_t>::max() - kDurationGuardMs) {
                boundedDurationMs += kDurationGuardMs;
            }
        }
        if (boundedDurationMs == 0 ||
            boundedDurationMs > kAbsoluteMaxDurationMs) {
            boundedDurationMs = kAbsoluteMaxDurationMs;
        }
        const uint64_t maxSamples64 =
            (boundedDurationMs * kSampleRate + 999ull) / 1000ull;
        const size_t maxSamples = static_cast<size_t>(std::min<uint64_t>(
            maxSamples64, static_cast<uint64_t>(std::numeric_limits<size_t>::max())));
        // Typical compressed voice packets are orders of magnitude sparser than
        // this.  The generous budget is only a second guard for a demuxer that
        // repeatedly returns packets without yielding decoded samples.
        const uint64_t maxPacketReads =
            std::min<uint64_t>(
                std::numeric_limits<uint64_t>::max() - 4096ull,
                boundedDurationMs * 4ull) +
            4096ull;
        uint64_t packetReads = 0;
        bool reachedLimit = false;
        AVChannelLayout mono;
        av_channel_layout_default(&mono, 1);
        const int config = swr_alloc_set_opts2(
            &resampler, &mono, AV_SAMPLE_FMT_S16, 44100,
            &codec->ch_layout, codec->sample_fmt, codec->sample_rate, 0, nullptr);
        av_channel_layout_uninit(&mono);
        if (config < 0 || !resampler || swr_init(resampler) < 0) break;
        frame = av_frame_alloc();
        packet = av_packet_alloc();
        if (!frame || !packet) break;
        const auto drain = [&]() -> bool {
            for (;;) {
                const int result = avcodec_receive_frame(codec, frame);
                if (result == AVERROR(EAGAIN) || result == AVERROR_EOF)
                    return true;
                if (result < 0) return false;
                const int capacity = swr_get_out_samples(resampler, frame->nb_samples);
                if (capacity < 0) return false;
                if (output.size() >= maxSamples) {
                    reachedLimit = true;
                    av_frame_unref(frame);
                    return true;
                }
                const size_t remaining = maxSamples - output.size();
                const int boundedCapacity = static_cast<int>(std::min<size_t>(
                    static_cast<size_t>(capacity), remaining));
                if (boundedCapacity <= 0) {
                    reachedLimit = true;
                    av_frame_unref(frame);
                    return true;
                }
                std::vector<int16_t> converted(static_cast<size_t>(boundedCapacity));
                uint8_t* data = reinterpret_cast<uint8_t*>(converted.data());
                const int count = swr_convert(resampler, &data, boundedCapacity,
                    const_cast<const uint8_t**>(frame->extended_data),
                    frame->nb_samples);
                av_frame_unref(frame);
                if (count < 0) return false;
                output.insert(output.end(), converted.begin(),
                              converted.begin() + count);
                if (output.size() >= maxSamples) {
                    reachedLimit = true;
                    return true;
                }
            }
        };
        bool okay = true;
        while ((cancelled == nullptr ||
                !cancelled->load(std::memory_order_acquire)) &&
               !reachedLimit && packetReads < maxPacketReads &&
               KotonohaFFmpegReadFrame(format, packet) >= 0) {
            ++packetReads;
            if (packet->stream_index == stream) {
                okay = avcodec_send_packet(codec, packet) >= 0 && drain();
            }
            av_packet_unref(packet);
            if (!okay) break;
        }
        if (packetReads >= maxPacketReads && !reachedLimit) {
            KLOG_WARN(CORE,
                      "Voice PCM analysis packet budget reached for '%s' (%llu reads)",
                      path.c_str(),
                      static_cast<unsigned long long>(packetReads));
            reachedLimit = true;
        }
        if (reachedLimit) {
            KLOG_WARN(CORE,
                      "Voice PCM analysis capped for '%s' at %zu samples (%llu ms)",
                      path.c_str(), output.size(),
                      static_cast<unsigned long long>(boundedDurationMs));
            success = !output.empty();
            break;
        }
        if (!okay ||
            (cancelled != nullptr && cancelled->load(std::memory_order_acquire)) ||
            avcodec_send_packet(codec, nullptr) < 0 || !drain()) break;
        for (;;) {
            const int capacity = swr_get_out_samples(resampler, 0);
            if (capacity <= 0) break;
            if (output.size() >= maxSamples) break;
            const size_t remaining = maxSamples - output.size();
            const int boundedCapacity = static_cast<int>(std::min<size_t>(
                static_cast<size_t>(capacity), remaining));
            if (boundedCapacity <= 0) break;
            std::vector<int16_t> converted(static_cast<size_t>(boundedCapacity));
            uint8_t* data = reinterpret_cast<uint8_t*>(converted.data());
            const int count = swr_convert(resampler, &data, boundedCapacity, nullptr, 0);
            if (count <= 0) break;
            output.insert(output.end(), converted.begin(),
                          converted.begin() + count);
        }
        success = true;
    } while (false);
    av_packet_free(&packet);
    av_frame_free(&frame);
    swr_free(&resampler);
    avcodec_free_context(&codec);
    if (inputContext != nullptr)
        KotonohaFFmpegCloseInput(&format, &inputContext);
    else
        avformat_close_input(&format);
    if (!success) output.clear();
    return success;
}
} // namespace Kotonoha
