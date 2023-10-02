/**
 * @file Video.h
 * @brief Contains the definition of the videoObject class and the playVideo function.
 * 
 * The videoObject class is used to push video data to the kotonohaData::acessMapper object.
 * The playVideo function is used to play the video data pushed to the kotonohaData::acessMapper object.
 */
namespace kotonoha
{
    class videoObject
    {
    public:
        kotonohaData::acessMapper *exportTo = NULL;
        int push(std::string filenameString, std::string timeStart, std::string timeEnd)
        {
            std::stringstream ss;
            ss << "./Midia/";
            ss << filenameString;
            ss << ".WMV";
            std::string filenameStr = ss.str();
            kotonohaData::videoData videoTemporary;
            videoTemporary.path = filenameStr;
            videoTemporary.play = kotonohaTime::convertToTime(timeStart);
            videoTemporary.end = kotonohaTime::convertToTime(timeEnd);
            exportTo->video.push_back(videoTemporary);
            return 0;
        };
    };
    int playVideo(void *import)
    {
        kotonohaData::acessMapper *importedTo = static_cast<kotonohaData::acessMapper *>(import);
        std::vector<kotonohaData::videoData> videoData = importedTo->video;
        kotonohaData::controlData *control = importedTo->control;
        kotonohaData::rootData *root = importedTo->root;
        kotonohaTime::delay(1000);
        double timePass = 0.0;
        int h = 0, w = 0;
        root->log0->appendLog("(Video) - Start");
        while (!control->exit)
        {
            if (control->display[1])
            {
                if (!control->videoEnd)
                {
                    for (std::vector<kotonohaData::videoData>::size_type i = 0; i < videoData.size(); i++)
                    {
                        timePass = control->timer0.pushTime();
                        if (videoData[i].play < timePass && videoData[i].played == false)
                        {
                            root->log0->appendLog("(Video) - Playing " + videoData[i].path);
                            AVFormatContext *formatCtx = avformat_alloc_context();
                            videoData[i].played = true;
                            if (avformat_open_input(&formatCtx, videoData[i].path.c_str(), NULL, NULL) != 0)
                            {
                                root->log0->appendLog("(Video) - Error on open file");
                            }
                            // Find video stream
                            int videoStream = -1;
                            for (unsigned int i = 0; i < formatCtx->nb_streams; i++)
                            {
                                if (formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
                                {
                                    videoStream = i;
                                    break;
                                }
                            }
                            if (videoStream == -1)
                            {
                                root->log0->appendLog("(Video) - No stream found");
                            }
                            // Setup video decoder
                            const auto codec = avcodec_find_decoder(formatCtx->streams[videoStream]->codecpar->codec_id);
                            if (!codec)
                            {
                                root->log0->appendLog("(Video) - No codec support");
                            }
                            AVCodecContext *codecCtx = avcodec_alloc_context3(codec);
                            avcodec_parameters_to_context(codecCtx, formatCtx->streams[videoStream]->codecpar);
                            avcodec_open2(codecCtx, codec, nullptr);
                            AVFrame *frame = av_frame_alloc();
                            // Allocate frame
                            AVPacket packet;
                            while (av_read_frame(formatCtx, &packet) >= 0 && !control->exit)
                            {
                                if (packet.stream_index == videoStream)
                                {
                                    while (!control->display[1] && !control->exit)
                                    {
                                        continue;
                                    }
                                    kotonohaTime::delay(30);
                                    // Decode frame
                                    avcodec_send_packet(codecCtx, &packet);
                                    avcodec_receive_frame(codecCtx, frame);
                                    // Create SDL texture to render frame
                                    SDL_Texture *texture = SDL_CreateTexture(root->renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING, frame->width, frame->height);
                                    SDL_UpdateYUVTexture(texture, NULL, frame->data[0], frame->linesize[0], frame->data[1], frame->linesize[1], frame->data[2], frame->linesize[2]);
                                    SDL_GetWindowSize(root->window, &w, &h);
                                    SDL_Rect square = {0, 0, w, h};
                                    SDL_RenderCopy(root->renderer, texture, NULL, &square);
                                    SDL_DestroyTexture(texture);
                                    control->display[1] = false;
                                    control->display[2] = true;
                                }
                                av_packet_unref(&packet);
                            }
                            // Free resorces
                            av_frame_free(&frame);
                            avformat_close_input(&formatCtx);
                            avcodec_close(codecCtx);
                            // Check video array size
                            if (videoData.size() == 0)
                            {
                                control->videoEnd = true;
                                goto END;
                            }
                            videoData.erase(videoData.begin() + i);
                            i = 0;
                        }
                    }
                }
                END:
                control->display[1] = true;
                control->display[2] = true;
            }
        }
        root->log0->appendLog("(Video) - End");
        return 0;
    };
}