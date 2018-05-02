
// ffmpeg_player_mfcDlg.cpp: 实现文件
//

#include "stdafx.h"
#include "ffmpeg_player_mfc.h"
#include "ffmpeg_player_mfcDlg.h"
#include "afxdialogex.h"



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
int thread_pause = 0;
int player_flag = 0;


#ifdef _DEBUG
#define new DEBUG_NEW
#endif


// 用于应用程序“关于”菜单项的 CAboutDlg 对话框

class CAboutDlg : public CDialogEx
{
public:
	CAboutDlg();

// 对话框数据
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_ABOUTBOX };
#endif

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV 支持

// 实现
protected:
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialogEx(IDD_ABOUTBOX)
{
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialogEx)
END_MESSAGE_MAP()


// CffmpegplayermfcDlg 对话框



CffmpegplayermfcDlg::CffmpegplayermfcDlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_FFMPEG_PLAYER_MFC_DIALOG, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
	//  m_test = 0;
}

void CffmpegplayermfcDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_URL, m_url);
}

BEGIN_MESSAGE_MAP(CffmpegplayermfcDlg, CDialogEx)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_BN_CLICKED(IDC_PLAY, &CffmpegplayermfcDlg::OnBnClickedPlay)
	ON_BN_CLICKED(IDC_PAUSE, &CffmpegplayermfcDlg::OnBnClickedPause)
	ON_BN_CLICKED(IDC_STOP, &CffmpegplayermfcDlg::OnBnClickedStop)
	ON_BN_CLICKED(ID_ABOUT, &CffmpegplayermfcDlg::OnBnClickedAbout)
	ON_BN_CLICKED(IDC_FILEDIALOGE, &CffmpegplayermfcDlg::OnBnClickedFiledialoge)
END_MESSAGE_MAP()


int refresh_video(void *opaque) {

	while (!thread_exit) {
		if (!thread_pause)
		{
			SDL_Event event;
			event.type = REFRESH_EVENT;
			SDL_PushEvent(&event);
			SDL_Delay(40);
		}

	}
	thread_exit = 0;
	thread_pause = 0;
	//Break
	SDL_Event event;
	event.type = BREAK_EVENT;
	SDL_PushEvent(&event);

	return 0;
}

UINT ffmpeg_player(LPVOID pParam)
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

	char filepath[250] = {0};
	CffmpegplayermfcDlg *dlg = (CffmpegplayermfcDlg*)pParam;
	GetWindowTextA(dlg->m_url, (LPSTR)filepath, 250);

	//char filepath[] = "Titanic.mkv";
	//FILE *fp_yuv = fopen("output.yuv", "wb+");

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
	//screen = SDL_CreateWindow("Simplest Video Play SDL2", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
	//	dst_w, dst_h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
	screen = SDL_CreateWindowFrom(dlg->GetDlgItem(IDC_SCREEN)->GetSafeHwnd());

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
	player_flag = 1;
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
		else if (event.key.keysym.sym == SDLK_SPACE)
		{
			//thread_pause = !thread_pause;
		}
		else if (event.type == SDL_QUIT)
		{
			//thread_exit = 1;
			//player_flag = 0；
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
	//FIX SDL2 bug
	dlg->GetDlgItem(IDC_SCREEN)->ShowWindow(SW_SHOWNORMAL);

	sws_freeContext(img_convert_ctx);

	//fclose(fp_yuv);

	av_frame_free(&pFrameYUV);
	av_frame_free(&pFrame);
	avcodec_free_context(&pCodecCtx);
	avformat_close_input(&pFormatCtx);

	return 0;
}


// CffmpegplayermfcDlg 消息处理程序

BOOL CffmpegplayermfcDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	// 将“关于...”菜单项添加到系统菜单中。

	// IDM_ABOUTBOX 必须在系统命令范围内。
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != nullptr)
	{
		BOOL bNameValid;
		CString strAboutMenu;
		bNameValid = strAboutMenu.LoadString(IDS_ABOUTBOX);
		ASSERT(bNameValid);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}

	// 设置此对话框的图标。  当应用程序主窗口不是对话框时，框架将自动
	//  执行此操作
	SetIcon(m_hIcon, TRUE);			// 设置大图标
	SetIcon(m_hIcon, FALSE);		// 设置小图标

	// TODO: 在此添加额外的初始化代码

	return TRUE;  // 除非将焦点设置到控件，否则返回 TRUE
}

void CffmpegplayermfcDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else
	{
		CDialogEx::OnSysCommand(nID, lParam);
	}
}

// 如果向对话框添加最小化按钮，则需要下面的代码
//  来绘制该图标。  对于使用文档/视图模型的 MFC 应用程序，
//  这将由框架自动完成。

void CffmpegplayermfcDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // 用于绘制的设备上下文

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// 使图标在工作区矩形中居中
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// 绘制图标
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialogEx::OnPaint();
	}
}

//当用户拖动最小化窗口时系统调用此函数取得光标
//显示。
HCURSOR CffmpegplayermfcDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}


void CffmpegplayermfcDlg::OnBnClickedPlay()
{
	// TODO: 在此添加控件通知处理程序代码

	CString str1;
	m_url.GetWindowText(str1);
	if (str1.IsEmpty()) {
		MessageBox(_T("请选择播放文件!"));
		return;
	}
	if (player_flag)
	{
		thread_pause = !thread_pause;
		return;
	}

	AfxBeginThread(ffmpeg_player,this);

}


void CffmpegplayermfcDlg::OnBnClickedPause()
{
	// TODO: 在此添加控件通知处理程序代码
	if (player_flag) {
		thread_pause = !thread_pause;
		return;
	}
	MessageBox(_T("没有播放文件,无法暂停!"));
}


void CffmpegplayermfcDlg::OnBnClickedStop()
{
	// TODO: 在此添加控件通知处理程序代码
	//CString str2;
	//USES_CONVERSION;
	//str2 = A2T(avcodec_configuration()); // char to cstring
	//MessageBox(str2);
	if (player_flag) {
		thread_exit = 1;
		player_flag = 0;
		return;
	}
	MessageBox(_T("没有播放文件，无法停止!"));
}


void CffmpegplayermfcDlg::OnBnClickedAbout()
{
	// TODO: 在此添加控件通知处理程序代码
	CAboutDlg dlg_about;
	dlg_about.DoModal();


}


void CffmpegplayermfcDlg::OnBnClickedFiledialoge()
{
	// TODO: 在此添加控件通知处理程序代码
	//CString str1;
	//m_url.GetWindowText(str1);
	//MessageBox(str1);
	CString filepath;
	CFileDialog fdlg(TRUE, NULL, NULL, NULL, NULL);
	if (fdlg.DoModal() == IDOK)
	{
		filepath = fdlg.GetPathName();
		m_url.SetWindowText(filepath);
	}

}
