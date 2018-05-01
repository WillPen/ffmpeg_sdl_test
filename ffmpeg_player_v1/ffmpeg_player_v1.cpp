/* ffmpeg_decoder_v2.cpp: 定义控制台应用程序的入口点。

*/
#include "stdafx.h"

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"
#include "sdl/SDL.h"
};

//链接库
#pragma comment(lib , "avformat.lib")
#pragma comment(lib , "avcodec.lib")
#pragma comment(lib , "avdevice.lib")
#pragma comment(lib , "avfilter.lib")
#pragma comment(lib , "avutil.lib")
#pragma comment(lib , "postproc.lib")
#pragma comment(lib , "swresample.lib")
#pragma comment(lib , "swscale.lib")

#define dst_w		640
#define dst_h		272

//Refresh Event
#define REFRESH_EVENT  (SDL_USEREVENT + 1)
#define BREAK_EVENT    (SDL_USEREVENT + 2)

int thread_exit = 0;

int refresh_video(void *opaque) {
	thread_exit = 0;
	while (!thread_exit) {
		SDL_Event event;
		event.type = REFRESH_EVENT;
		SDL_PushEvent(&event);
		SDL_Delay(40);
	}
	thread_exit = 0;
	//Break
	SDL_Event event;
	event.type = BREAK_EVENT;
	SDL_PushEvent(&event);

	return 0;
}

int main(int argc, char* argv[])
{

	AVFormatContext	*pFormatCtx = NULL;
	int				videoindex = -1;
	AVCodecContext	*pCodecCtx = NULL;
	AVCodec			*pCodec = NULL;
	AVFrame	*pFrame = NULL, *pFrameYUV = NULL;
	uint8_t *out_buffer = NULL;
	AVPacket *packet = NULL;
	struct SwsContext *img_convert_ctx;

	int y_size = 0;
	int ret = -1;

	char filepath[] = "Titanic.mkv";
	FILE *fp_yuv = fopen("output.yuv", "wb+");

	//avcodec_register_all();//复用器等并没有使用到，不需要初始化，直接调用av_register_all就行
	av_register_all();
	//avformat_network_init();
	if (!(pFormatCtx = avformat_alloc_context()))
	{
		printf("avformat_alloc_context error!!,ret=%d\n", AVERROR(ENOMEM));
		return -1;
	}

	if (avformat_open_input(&pFormatCtx, filepath, NULL, NULL) != 0) {
		printf("Couldn't open input stream.\n");
		return -1;
	}
	if (avformat_find_stream_info(pFormatCtx, NULL)<0) {
		printf("Couldn't find stream information.\n");
		return -1;
	}

	/*
	//another way to get the stream id
	for (i = 0; i < pFormatCtx->nb_streams; i++) {
	if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
	videoindex = i;
	break;
	}
	}
	*/

	/* select the video stream */
	ret = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &pCodec, 0);
	if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "Cannot find a video stream in the input file\n");
		return ret;
	}
	videoindex = ret; //video stream id

	pCodec = avcodec_find_decoder(pFormatCtx->streams[videoindex]->codecpar->codec_id);
	if (pCodec == NULL) {
		printf("Codec not found.\n");
		return -1;
	}
	pCodecCtx = avcodec_alloc_context3(pCodec);
	if (pCodecCtx == NULL)
	{
		printf("Could not allocate AVCodecContext\n");
		return -1;
	}
	if ((ret = avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoindex]->codecpar)) < 0)
	{
		printf("Failed to copy codec parameters to decoder context\n");
		return ret;
	}

	if (avcodec_open2(pCodecCtx, pCodec, NULL)<0) {
		printf("Could not open codec.\n");
		return -1;
	}

	pFrame = av_frame_alloc(); //the data after decoder
	pFrameYUV = av_frame_alloc(); //the data after scale
	out_buffer = (uint8_t *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, dst_w, dst_h, 1));
	av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, out_buffer, AV_PIX_FMT_YUV420P, dst_w, dst_h, 1);
	packet = (AVPacket *)av_malloc(sizeof(AVPacket));
	if (!packet) {
		fprintf(stderr, "Can not alloc packet\n");
		return -1;
	}

	av_dump_format(pFormatCtx, 0, filepath, 0);

	img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
		dst_w, dst_h, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);

	//***************** SDL   *******************
	if (SDL_Init(SDL_INIT_VIDEO)) {
		printf("Could not initialize SDL - %s\n", SDL_GetError());
		return -1;
	}

	SDL_Window *screen;
	//SDL 2.0 Support for multiple windows
	screen = SDL_CreateWindow("Simplest Video Play SDL2", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		dst_w, dst_h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
	if (!screen) {
		printf("SDL: could not create window - exiting:%s\n", SDL_GetError());
		return -1;
	}
	SDL_Renderer* sdlRenderer = SDL_CreateRenderer(screen, -1, 0);

	Uint32 pixformat = 0;

	//IYUV: Y + U + V  (3 planes)
	//YV12: Y + V + U  (3 planes)
	pixformat = SDL_PIXELFORMAT_IYUV;

	SDL_Texture* sdlTexture = SDL_CreateTexture(sdlRenderer, pixformat, SDL_TEXTUREACCESS_STREAMING, dst_w, dst_h);
	SDL_Thread *refresh_thread = SDL_CreateThread(refresh_video, NULL, NULL);
	SDL_Event event;
	//*****************************END

	while (1)
	{
		//Wait
		SDL_WaitEvent(&event);
		if (event.type == REFRESH_EVENT)
		{
			if (av_read_frame(pFormatCtx, packet) >= 0)
			{
				if (packet->stream_index == videoindex)
				{
					ret = avcodec_send_packet(pCodecCtx, packet);
					if (ret != 0)
					{
						printf("send pkt error.\n");
						return ret;
					}

					if (avcodec_receive_frame(pCodecCtx, pFrame) == 0) {
						sws_scale(img_convert_ctx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height,
							pFrameYUV->data, pFrameYUV->linesize);

						//y_size = pCodecCtx->width*pCodecCtx->height;
#if 0
						y_size = dst_w * dst_h;
						fwrite(pFrameYUV->data[0], 1, y_size, fp_yuv);    //Y 
						fwrite(pFrameYUV->data[1], 1, y_size / 4, fp_yuv);  //U
						fwrite(pFrameYUV->data[2], 1, y_size / 4, fp_yuv);  //V
#endif
//SDL---------------------------
						SDL_UpdateTexture(sdlTexture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0]);
						SDL_RenderClear(sdlRenderer);
						//SDL_RenderCopy( sdlRenderer, sdlTexture, &sdlRect, &sdlRect );  
						SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
						SDL_RenderPresent(sdlRenderer);
						//SDL End-----------------------
						av_packet_unref(packet);
						printf("Succeed to decode 1 frame!\n");
					}
				}
			}
		}
		else if (event.type == SDL_KEYDOWN)
		{
		}
		else if (event.type == SDL_QUIT)
		{
			thread_exit = 1;
		}
		else if (event.type == BREAK_EVENT)
		{
			break;
		}
	}

	/*
	while (av_read_frame(pFormatCtx, packet) >= 0) {
		if (packet->stream_index == videoindex) {
			ret = avcodec_send_packet(pCodecCtx, packet);
			if (ret != 0)
			{
				printf("send pkt error.\n");
				return ret;
			}

			if (avcodec_receive_frame(pCodecCtx, pFrame) == 0) {
				sws_scale(img_convert_ctx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height,
					pFrameYUV->data, pFrameYUV->linesize);

				//y_size = pCodecCtx->width*pCodecCtx->height;
				y_size = dst_w * dst_h;
				fwrite(pFrameYUV->data[0], 1, y_size, fp_yuv);    //Y 
				fwrite(pFrameYUV->data[1], 1, y_size / 4, fp_yuv);  //U
				fwrite(pFrameYUV->data[2], 1, y_size / 4, fp_yuv);  //V
				printf("Succeed to decode 1 frame!\n");

			}
		}
		av_packet_unref(packet);
	}
	*/
	//flush decoder
	//FIX: Flush Frames remained in Codec

	/*
	while (1) {
		ret = avcodec_send_packet(pCodecCtx, NULL);
		if (ret != 0)
		{
			printf("send pkt error.\n");
			break;
		}

		if (avcodec_receive_frame(pCodecCtx, pFrame) != 0)
		{
			break;
		}
		sws_scale(img_convert_ctx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height,
			pFrameYUV->data, pFrameYUV->linesize);

		//int y_size = pCodecCtx->width*pCodecCtx->height;
		fwrite(pFrameYUV->data[0], 1, y_size, fp_yuv);    //Y 
		fwrite(pFrameYUV->data[1], 1, y_size / 4, fp_yuv);  //U
		fwrite(pFrameYUV->data[2], 1, y_size / 4, fp_yuv);  //V

		printf("Flush Decoder: Succeed to decode 1 frame!\n");
	}
	*/
	SDL_Quit();
	sws_freeContext(img_convert_ctx);

	fclose(fp_yuv);

	av_frame_free(&pFrameYUV);
	av_frame_free(&pFrame);
	avcodec_free_context(&pCodecCtx);
	avformat_close_input(&pFormatCtx);

	return 0;
}
