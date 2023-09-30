namespace video
{
    class videoObject
    {
    public:
        std::vector<videoData> videoArray;
        void *data;
        int push(std::string filenameString, std::string timeStart, std::string timeEnd, SDL_Renderer *renderer, drawControl *dataDraw, SDL_Window *window)
        {
            std::stringstream ss;
            ss << "./Midia/";
            ss << filenameString;
            ss << ".WMV";
            std::string filenameStr = ss.str();
            videoData videoTemporary;
            videoTemporary.filename = filenameStr;
            videoTemporary.timeToPlay = timeUtils::convertToTime(timeStart);
            videoTemporary.timeToEnd = timeUtils::convertToTime(timeEnd);
            videoTemporary.renderer = renderer;
            videoTemporary.dataDraw = dataDraw;
            videoTemporary.window = window;
            videoArray.push_back(videoTemporary);
            data = &videoArray;
            return 0;
        };
    };
    int play(void *import)
    {
        int h = 0, w = 0;
        timeUtils::delay(1000);
        drawControl *local = NULL;
        double timePass = 0.0;
        int arraySize = -1;
        std::vector<videoData> *videoSource = static_cast<std::vector<videoData> *>(import);
        if (import != NULL && videoSource->size() > 0)
        {
            if (local == NULL)
            {
                local = (*videoSource)[0].dataDraw;
            }
            std::cout << "Video init, videos to play =" << videoSource->size()  << " " << std::endl;
            while (!local->exit && arraySize != 0)
            {
                for (std::vector<videoData>::size_type i = 0; i < (*videoSource).size(); i++)

                {
                    timePass = local->timer0.pushTime();
                    if ((*videoSource)[i].timeToPlay < timePass && (*videoSource)[i].played == false)
                    {
                        AVFormatContext *formatCtx = avformat_alloc_context();
                        (*videoSource)[i].played = true;
                        if (avformat_open_input(&formatCtx, (*videoSource)[i].filename.c_str(), NULL, NULL) != 0)
                        {
                            std::cerr << "Erro ao abrir o arquivo de vídeo " << (*videoSource)[i].filename << std::endl;
                            return -1;
                        }

                        // Encontre o stream de vídeo
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
                            std::cerr << "Nenhum stream de vídeo encontrado no arquivo" << std::endl;
                            return -1;
                        }
                        // Configure o decodificador de vídeo
                        const auto codec = avcodec_find_decoder(formatCtx->streams[videoStream]->codecpar->codec_id);
                        if (!codec)
                        {
                            std::cerr << "Codec não suportado" << std::endl;
                        }
                        AVCodecContext *codecCtx = avcodec_alloc_context3(codec);
                        avcodec_parameters_to_context(codecCtx, formatCtx->streams[videoStream]->codecpar);
                        avcodec_open2(codecCtx, codec, nullptr);
                        AVFrame *frame = av_frame_alloc();
                        // Leia os quadros do arquivo de vídeo
                        AVPacket packet;
                        while (av_read_frame(formatCtx, &packet) >= 0 && !local->exit)
                        {
                            if (packet.stream_index == videoStream)
                            {
                                while (!local->videoD)
                                {
                                }
                                timeUtils::delay(41);
                                // Decodifique o quadro
                                avcodec_send_packet(codecCtx, &packet);
                                avcodec_receive_frame(codecCtx, frame);
                                // Crie uma textura SDL com os dados do quadro+
                                SDL_Texture *texture = SDL_CreateTexture((*videoSource)[i].renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING, frame->width, frame->height);
                                SDL_UpdateYUVTexture(texture, NULL, frame->data[0], frame->linesize[0], frame->data[1], frame->linesize[1], frame->data[2], frame->linesize[2]);
                                SDL_GetWindowSize((*videoSource)[i].window, &w, &h);
                                SDL_Rect square = {0, 0, w, h};
                                SDL_RenderCopy((*videoSource)[i].renderer, texture, NULL, &square);
                                SDL_DestroyTexture(texture);
                                local->videoD = false;
                                local->uiD = true;
                            }
                            av_packet_unref(&packet);
                        }
                        // Libere os recursos
                        av_frame_free(&frame);
                        avformat_close_input(&formatCtx);
                        avcodec_close(codecCtx);
                        videoSource->erase(videoSource->begin() + i);
                        arraySize = videoSource->size();
                        i = arraySize;
                    }
                }
            }
            local->videoE = true;
        }
        std::cout << "Video end" << std::endl;
        return 0;
    };
}