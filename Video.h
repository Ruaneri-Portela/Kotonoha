namespace kotonoha
{
	class videoObject
	{
	public:
		kotonohaData::acessMapper* exportTo = NULL;
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
	////////////////////////////////////////////
	static AVBufferRef* hw_device_ctx = NULL;
	static enum AVPixelFormat hw_pix_fmt;
	static int hw_decoder_init(AVCodecContext* ctx, const enum AVHWDeviceType type)
	{
		int err = 0;
		if ((err = av_hwdevice_ctx_create(&hw_device_ctx, type,NULL, NULL, 0)) < 0) {
			return err;
		}
		ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
		return err;
	}
	static enum AVPixelFormat get_hw_format(AVCodecContext* ctx,
		const enum AVPixelFormat* pix_fmts)
	{
		const enum AVPixelFormat* p;
		for (p = pix_fmts; *p != -1; p++) {
			if (*p == hw_pix_fmt)
				return *p;
		}
		return AV_PIX_FMT_NONE;
	}
	////////////////////////////////////////////
	int playVideo(void* import)
	{
		kotonohaData::acessMapper * importedTo = static_cast<kotonohaData::acessMapper*>(import);
		importedTo->root->log0->appendLog("(Video) - Start");
		importedTo->root->log0->appendLog("(Video) - " + std::to_string(importedTo->video.size()) + " Videos to play");
		SDL_Texture* texture = NULL;
		double timePass = 0.0;
		int h = 0, w = 0;
		SDL_Rect square = { 0,0,0,0 };
		// Pre Config AV context
		AVPacket* packet;
		packet = av_packet_alloc();
		AVFrame* frame = av_frame_alloc();
		// Set HW decoder
		enum AVHWDeviceType type;
		type = av_hwdevice_find_type_by_name("dxva2");
		int hw = av_hwdevice_ctx_create(&hw_device_ctx, type, NULL, NULL, 0);
		hw = 1;
		while (importedTo->control->outCode == 0)
		{
			kotonohaTime::delay(kotonoha::maxtps);
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
							AVFormatContext* formatCtx = avformat_alloc_context();
							importedTo->video[i].played = true;
							if (avformat_open_input(&formatCtx, importedTo->video[i].path.c_str(), NULL, NULL) != 0)
							{
								importedTo->root->log0->appendLog("(Video) - Error on open file " + importedTo->audio[i].path);
							}
							avformat_find_stream_info(formatCtx, NULL);
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
							const AVCodec* codec = avcodec_find_decoder(formatCtx->streams[videoStream]->codecpar->codec_id);
							if (!codec)
							{
								importedTo->root->log0->appendLog("(Video) - No codec support " + importedTo->audio[i].path);
							}
							if (hw == 0) {
								for (i = 0;; i++) {
									const AVCodecHWConfig* config = avcodec_get_hw_config(codec, i);
									if (!config) {
										fprintf(stderr, "Decoder %s does not support device type %s.\n", codec->name, av_hwdevice_get_type_name(type));
									}
									if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX && config->device_type == type) {
										hw_pix_fmt = config->pix_fmt;
										break;
									}
								}
							}
							AVCodecContext* codecCtx = avcodec_alloc_context3(codec);
							avcodec_parameters_to_context(codecCtx, formatCtx->streams[videoStream]->codecpar);
							if (hw == 0)
							{
								codecCtx->get_format = get_hw_format;
								hw_decoder_init(codecCtx, type);
							}
							avcodec_open2(codecCtx, codec, NULL);
							double sTime = importedTo->control->timer0.pushTime();
							double pTime = 0.0;
							double fTime = 1.0 / av_q2d(formatCtx->streams[videoStream]->avg_frame_rate);
							bool exit = false;
							while (av_read_frame(formatCtx, packet) >= 0 && importedTo->control->outCode == 0)
							{
								if (packet->stream_index == videoStream)
								{
									// Decode frame
									avcodec_send_packet(codecCtx, packet);
									avcodec_receive_frame(codecCtx, frame);
									if (hw == 0 && frame->format == hw_pix_fmt)
									{
										AVFrame* frame_sw = av_frame_alloc();
										frame_sw->width = frame->width;
										frame_sw->height = frame->height;
										frame_sw->format = AV_PIX_FMT_NV12;
										av_frame_get_buffer(frame_sw, 32);
										av_hwframe_transfer_data(frame_sw, frame, 0);
										av_frame_free(&frame);
										frame = frame_sw;
									}
									// Loop to wait frame time 
									while (!exit && importedTo->control->outCode == 0)
									{
										kotonohaTime::delay(kotonoha::maxtps);
										pTime = importedTo->control->timer0.pushTime() - sTime;
										// Case frame time is a target
										if (pTime >= fTime - 0.003 or importedTo->control->paused)
										{
											// Can display frame
											if (importedTo->control->display[1] == true)
											{
												if (importedTo->control->hiddenVideo) goto END;
												// Render frame
												if (hw == 0) {
													texture = SDL_CreateTexture(importedTo->root->renderer, SDL_PIXELFORMAT_NV12, SDL_TEXTUREACCESS_STREAMING, frame->width, frame->height);
													SDL_UpdateNVTexture(texture, NULL, frame->data[0], frame->linesize[0], frame->data[1], frame->linesize[1]);
												}
												else {
													texture = SDL_CreateTexture(importedTo->root->renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING, frame->width, frame->height);
													SDL_UpdateYUVTexture(texture, NULL, frame->data[0], frame->linesize[0], frame->data[1], frame->linesize[1], frame->data[2], frame->linesize[2]);
												}
												SDL_GetWindowSize(importedTo->root->window, &w, &h);
												square = { 0, 0, w, h };
												SDL_RenderCopy(importedTo->root->renderer, texture, NULL, &square);
												SDL_DestroyTexture(texture);
											END:
												importedTo->control->display[2] = true;
												importedTo->control->display[1] = false;
											}
											if (!importedTo->control->paused) {
												sTime = importedTo->control->timer0.pushTime();
												pTime = 0.0;
												exit = true;
											}
										}
									}
									exit = false;
								}
								av_packet_unref(packet);
							}
							// Free resorces
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
				importedTo->video.size() == 0 && !importedTo->control->videoEnd ? importedTo->control->videoEnd = true : 0;
			}
		}
		av_frame_free(&frame);
		importedTo->root->log0->appendLog("(Video) - End");
		return 0;
	};
}