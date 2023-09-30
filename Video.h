namespace video
{
    class videoObject
    {
    public:
        std::vector<videoData> videoArray;
        void *data;
        int push(std::string filenameString, std::string timeStart, std::string timeEnd, SDL_Renderer *renderer, drawControl *dataDraw,SDL_Rect *square)
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
            videoTemporary.square = square;
            videoArray.push_back(videoTemporary);
            data = &videoArray;
            return 0;
        };
    };
    int play(void *import)
    {
        timeUtils::delay(1000);
        ///
        double timePass = 0.0;
        std::chrono::time_point<std::chrono::high_resolution_clock> timeInitial;
        timeInitial = std::chrono::high_resolution_clock::now();
        ///
        if (import != NULL)
        {
            std::vector<videoData> *videoSource = static_cast<std::vector<videoData> *>(import);
            while (runVideo)
            {
                for (int i = 0; i < (*videoSource).size(); i++)
                {
                    ///
                    std::chrono::time_point<std::chrono::high_resolution_clock> timeToCompare = std::chrono::high_resolution_clock::now();
                    std::chrono::duration<double> duration = timeToCompare - timeInitial;
                    timePass = duration.count();
                    ///
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
                        while (av_read_frame(formatCtx, &packet) >= 0)
                        {
                            if (packet.stream_index == videoStream)
                            {
                                while (!(*videoSource)[i].dataDraw->videoD)
                                {
                                }
                                timeUtils::delay(41);
                                // Decodifique o quadro
                                avcodec_send_packet(codecCtx, &packet);
                                avcodec_receive_frame(codecCtx, frame);
                                // Crie uma textura SDL com os dados do quadro+
                                SDL_Texture *texture = SDL_CreateTexture((*videoSource)[i].renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING, frame->width, frame->height);
                                SDL_UpdateYUVTexture(texture, NULL, frame->data[0], frame->linesize[0], frame->data[1], frame->linesize[1], frame->data[2], frame->linesize[2]);
                                SDL_RenderCopy((*videoSource)[i].renderer, texture, NULL, (*videoSource)[i].square);
                                SDL_DestroyTexture(texture);
                                (*videoSource)[i].dataDraw->videoD = false;
                                (*videoSource)[i].dataDraw->uiD = true;
                            }
                            av_packet_unref(&packet);
                        }
                        // Libere os recursos
                        av_frame_free(&frame);
                        avformat_close_input(&formatCtx);
                        avcodec_close(codecCtx);
                    }
                }
                (*videoSource)[0].dataDraw->videoD = false;
                (*videoSource)[0].dataDraw->uiD = true;
            }
        }
        return 0;
    };
}