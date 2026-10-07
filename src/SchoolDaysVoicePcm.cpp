#include <Kotonoha/SchoolDaysVoicePcm.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

namespace Kotonoha {
bool DecodeSchoolDaysVoicePcm(const std::string& path,
                             std::vector<int16_t>& output) {
    output.clear();
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    SwrContext* resampler = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    bool success = false;
    do {
        if (avformat_open_input(&format, path.c_str(), nullptr, nullptr) < 0 ||
            avformat_find_stream_info(format, nullptr) < 0) break;
        const int stream = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO,
                                               -1, -1, nullptr, 0);
        if (stream < 0) break;
        const AVCodec* decoder = avcodec_find_decoder(
            format->streams[stream]->codecpar->codec_id);
        if (!decoder) break;
        codec = avcodec_alloc_context3(decoder);
        if (!codec || avcodec_parameters_to_context(
                codec, format->streams[stream]->codecpar) < 0 ||
            avcodec_open2(codec, decoder, nullptr) < 0) break;
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
        while (av_read_frame(format, packet) >= 0) {
            if (packet->stream_index == stream) {
                okay = avcodec_send_packet(codec, packet) >= 0 && drain();
            }
            av_packet_unref(packet);
            if (!okay) break;
        }
        if (!okay || avcodec_send_packet(codec, nullptr) < 0 || !drain()) break;
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
    avformat_close_input(&format);
    if (!success) output.clear();
    return success;
}
} // namespace Kotonoha
