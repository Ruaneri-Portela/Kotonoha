#include <Kotonoha/SchoolDaysVoicePcm.hpp>

extern "C" {
#include <Kotonoha/utils/FFmpeg.h>
#include <libswresample/swresample.h>
}

namespace Kotonoha {
bool DecodeSchoolDaysVoicePcm(const std::string& path,
                             std::vector<int16_t>& output,
                             const std::atomic_bool* cancelled) {
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
                std::vector<int16_t> converted(static_cast<size_t>(capacity));
                uint8_t* data = reinterpret_cast<uint8_t*>(converted.data());
                const int count = swr_convert(resampler, &data, capacity,
                    const_cast<const uint8_t**>(frame->extended_data),
                    frame->nb_samples);
                av_frame_unref(frame);
                if (count < 0) return false;
                output.insert(output.end(), converted.begin(),
                              converted.begin() + count);
            }
        };
        bool okay = true;
        while ((cancelled == nullptr ||
                !cancelled->load(std::memory_order_acquire)) &&
               KotonohaFFmpegReadFrame(format, packet) >= 0) {
            if (packet->stream_index == stream) {
                okay = avcodec_send_packet(codec, packet) >= 0 && drain();
            }
            av_packet_unref(packet);
            if (!okay) break;
        }
        if (!okay ||
            (cancelled != nullptr && cancelled->load(std::memory_order_acquire)) ||
            avcodec_send_packet(codec, nullptr) < 0 || !drain()) break;
        for (;;) {
            const int capacity = swr_get_out_samples(resampler, 0);
            if (capacity <= 0) break;
            std::vector<int16_t> converted(static_cast<size_t>(capacity));
            uint8_t* data = reinterpret_cast<uint8_t*>(converted.data());
            const int count = swr_convert(resampler, &data, capacity, nullptr, 0);
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
