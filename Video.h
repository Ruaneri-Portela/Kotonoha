namespace kotonoha
{
    class videoObject
    {
    public:
        kotonohaData::acessMapper *exportTo = NULL;
        int push(std::string filenameString, std::string timeStart, std::string timeEnd)
        {
            // Prepare path
            std::stringstream ss;
            ss << exportTo->root->fileConfigs->mediaPath;
            ss << filenameString;
            ss << exportTo->root->fileConfigs->videoExtension;
            std::string filenameStr = ss.str();
            // Build object to append on array
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
        importedTo->root->log0->appendLog("(Video) - Start");
        importedTo->root->log0->appendLog("(Video) - " + std::to_string(importedTo->video.size()) + " Videos to play");
        SDL_Texture *texture = NULL;
        double timePass = 0.0;
        int h = 0, w = 0;
        SDL_Rect square = { 0,0,0,0 };
        while (importedTo->control->outCode == 0)
        {
            if (importedTo->control->display[1])
            {
                if (!importedTo->control->videoEnd && !importedTo->control->nonVideo && !(importedTo->video.size() == 0))
                {
                    for (std::vector<kotonohaData::videoData>::size_type i = 0; i < importedTo->video.size(); i++)
                    {
                        timePass = importedTo->control->timer0.pushTime();
                        if (importedTo->video[i].play < timePass && importedTo->video[i].played == false)
                        {
                            importedTo->root->log0->appendLog("(Video) - Playing " + importedTo->video[i].path);
                            AVFormatContext *formatCtx = avformat_alloc_context();
                            importedTo->video[i].played = true;
                            if (avformat_open_input(&formatCtx, importedTo->video[i].path.c_str(), NULL, NULL) != 0)
                            {
                                importedTo->root->log0->appendLog("(Video) - Error on open file " + importedTo->audio[i].path);
                            }
                            // Find video stream
                            int videoStream = -1;
                            for (unsigned int i = 0; i < formatCtx->nb_streams; i++)
                            {
                                if (formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
                                {
                                    videoStream = i;
                                }
                            }
                            if (videoStream == -1)
                            {
                                importedTo->root->log0->appendLog("(Video) - No stream found " + importedTo->audio[i].path);
                            }
                            // Setup video decoder
                            const AVCodec *codec = avcodec_find_decoder(formatCtx->streams[videoStream]->codecpar->codec_id);
                            if (!codec)
                            {
                                importedTo->root->log0->appendLog("(Video) - No codec support " + importedTo->audio[i].path);
                            }
                            AVCodecContext *codecCtx = avcodec_alloc_context3(codec);
                            avcodec_parameters_to_context(codecCtx, formatCtx->streams[videoStream]->codecpar);
                            avcodec_open2(codecCtx, codec, nullptr);
                            // Allocate frame
                            AVFrame* frame = av_frame_alloc();
                            AVPacket packet;
                            double sTime = importedTo->control->timer0.pushTime();
                            double pTime = 0.0;
                            double fTime = 1.0 / 24.0;
                            while (av_read_frame(formatCtx, &packet) >= 0 && importedTo->control->outCode == 0)
                            {
                                if (packet.stream_index == videoStream)
                                {
                                    // Decode frame
                                    avcodec_send_packet(codecCtx, &packet);
                                    avcodec_receive_frame(codecCtx, frame);
                                    // Create SDL texture to render frame
                                    texture = SDL_CreateTexture(importedTo->root->renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING, frame->width, frame->height);
                                    SDL_UpdateYUVTexture(texture, NULL, frame->data[0], frame->linesize[0], frame->data[1], frame->linesize[1], frame->data[2], frame->linesize[2]);
                                    SDL_GetWindowSize(importedTo->root->window, &w, &h);
                                    square = {0, 0, w, h};
                                    //Wait other in display
                                    while (!importedTo->control->display[1] && importedTo->control->outCode == 0)
                                    {
                                        continue;
                                    }
                                    // Wait frame time
                                    while (fTime > pTime)
                                    {
                                        pTime = importedTo->control->timer0.pushTime() - sTime;
                                        if (importedTo->control->display[1] == true && importedTo->control->timer0.paused)
                                        {
                                            importedTo->control->display[2] = true;
                                            importedTo->control->display[1] = false;
                                        }
                                    }
                                    SDL_RenderCopy(importedTo->root->renderer, texture, NULL, &square);
                                    SDL_DestroyTexture(texture);
                                    sTime = importedTo->control->timer0.pushTime();
                                    pTime = 0.0;
                                    importedTo->control->display[2] = true;
                                    importedTo->control->display[1] = false;
                                }
                                av_packet_unref(&packet);
                            }
                            // Free resorces
                            av_frame_free(&frame);
                            avformat_close_input(&formatCtx);
                            avcodec_close(codecCtx);
                            // Delete video from array
                            importedTo->video.erase(importedTo->video.begin() + i);
                            break;
                        }
                    }
                }
                importedTo->control->display[2] = true;
                importedTo->control->display[1] = false;
                importedTo->video.size() == 0 &&!importedTo->control->videoEnd ? importedTo->control->videoEnd = true : 0;
            }
        }
        importedTo->root->log0->appendLog("(Video) - End");
        return 0;
    };
}