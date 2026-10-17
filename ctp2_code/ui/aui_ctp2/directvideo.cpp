//----------------------------------------------------------------------------
//
// Project      : Call To Power 2
// File type    : C++ source
// Description  : DirectMedia video handling
//
//----------------------------------------------------------------------------
//
// Compiler flags
//
//----------------------------------------------------------------------------
//
// Modifications from the original Activision code:
//
// - Allow movies to be displayed on a system without a sound card.
//
//----------------------------------------------------------------------------

#include "c3.h"

#ifdef __AUI_USE_DIRECTX__

#include "directvideo.h"

#include "aui.h"
#include "aui_directui.h"
#include "aui_window.h"
#include "aui_directsurface.h"
#include "c3errors.h"
#include "errors.h"

DirectVideo::DirectVideo()
:
	m_isPlaying                 (false),
	m_isOpen                    (false),
	m_isFinished                (false),
	m_isPaused                  (false),
	m_isModal                   (false),
	m_mmStream                  (NULL),
	m_dd                        (NULL),
	m_primaryVidStream          (NULL),
	m_ddStream                  (NULL),
	m_primarySurface            (NULL),
	m_streamSurface             (NULL),
	m_streamSample              (NULL),
	m_flags                     (0),
	m_window                    (NULL),
	m_timePerFrame              (0),
	m_lastFrameTime             (0)
{
	RECT emptyRect  = {0,0,0,0};

	m_surfaceRect   = emptyRect;
	m_destRect      = emptyRect;
}

DirectVideo::~DirectVideo()
{
}

HRESULT DirectVideo::Initialize(aui_DirectUI *ui)
{
	Assert(ui != NULL);
	if (ui == NULL) return -1;

	LPDIRECTDRAW			dd = ui->DD();
	Assert(dd != NULL);
	if (dd == NULL) return -1;
	m_dd = dd;

	LPDIRECTDRAWSURFACE		primarySurface = ((aui_DirectSurface *)ui->Secondary())->BUFFER();

	Assert(primarySurface != NULL);
	if (primarySurface == NULL) return -1;
	m_primarySurface = primarySurface;

	m_flags = DVFLAGS_FULLSCREEN;

	return 0;
}

HRESULT DirectVideo::Initialize(aui_DirectUI *ui, aui_Window *window, BOOL modal)
{
	Assert(ui != NULL);
	if (ui == NULL) return -1;

	LPDIRECTDRAW	dd = ui->DD();
	Assert(dd != NULL);
	if (dd == NULL) return -1;
	m_dd = dd;

	m_window = window;

	m_flags = DVFLAGS_INWINDOW;

	m_isModal = modal;

	LPDIRECTDRAWSURFACE		primarySurface;

	if (m_isModal)
	{
		primarySurface = ((aui_DirectSurface *)ui->Primary())->BUFFER();
	}
	else
	{
		primarySurface = ((aui_DirectSurface *)window->TheSurface())->BUFFER();
	}

	Assert(primarySurface != NULL);
	if (primarySurface == NULL) return -1;
	m_primarySurface = primarySurface;

	return 0;
}

HRESULT	DirectVideo::OpenStream(MBCHAR *name)
{
	IAMMultiMediaStream *pAMStream;
	HRESULT hr;
	TCHAR	errorText[_MAX_PATH];

	hr = CoCreateInstance(CLSID_AMMultiMediaStream, NULL, CLSCTX_INPROC_SERVER,
				 IID_IAMMultiMediaStream, (void **)&pAMStream);
	if (FAILED(hr)) {
		AMGetErrorText(hr, errorText, _MAX_PATH);
		c3errors_ErrorDialog("Video", "Could not create AMMultiMediaStream.  Error#%d.", hr);
		goto Exit;
	}

	hr = pAMStream->Initialize(STREAMTYPE_READ, 0, NULL);
	if (FAILED(hr)) {
		c3errors_ErrorDialog("Video", "Could not initialize IAMMultiMediaStream.  Error#%d.", hr);
		goto Exit;
	}

	hr = pAMStream->AddMediaStream(m_dd, &MSPID_PrimaryVideo, 0, NULL);
	if (FAILED(hr)) {
		c3errors_ErrorDialog("Video", "Could not add primary video stream to AMStream.  Error#%d.", hr);
		goto Exit;
	}

	hr = pAMStream->AddMediaStream(NULL, &MSPID_PrimaryAudio, AMMSF_ADDDEFAULTRENDERER, NULL);
	(void) hr;  // Ignore failures: display video without sound.

	WCHAR       wPath[_MAX_PATH];
	MultiByteToWideChar(CP_ACP, 0, name, -1, wPath, sizeof(wPath)/sizeof(wPath[0]));

	hr = pAMStream->OpenFile(wPath, 0);
	if (FAILED(hr)) {
		c3errors_ErrorDialog("Video", "Unable to open the file.  Error#%d.", hr);
		goto Exit;
	}

	m_mmStream = pAMStream;




	hr = m_mmStream->GetMediaStream(MSPID_PrimaryVideo, &m_primaryVidStream);
	if (FAILED(hr))
	{
		c3errors_ErrorDialog("Video", "Couldn't access the primary video stream.  Error#%d.", hr);
		goto Exit;
	}

	hr = m_primaryVidStream->QueryInterface(IID_IDirectDrawMediaStream, (void **)&m_ddStream);
	if (FAILED(hr)) {
		c3errors_ErrorDialog("Video", "Couldn't access the DirectDraw media stream.  Error#%d.", hr);
		goto Exit;
	}

	STREAM_TIME		streamTime;

	hr = m_ddStream->GetTimePerFrame(&streamTime);
	m_timePerFrame = (uint32)(streamTime / (UNITS / MILLISECONDS));

	hr = m_ddStream->CreateSample(NULL, NULL, 0, &m_streamSample);
	if (FAILED(hr)) {
		c3errors_ErrorDialog("Video", "Couldn't access the DirectDraw stream sample.  Error#%d.", hr);
		goto Exit;
	}

	hr = m_streamSample->GetSurface(&m_streamSurface, &m_surfaceRect);
	if (FAILED(hr)) {
		c3errors_ErrorDialog("Video", "Couldn't access the DirectDraw sample surface.  Error#%d.", hr);
		goto Exit;
	}

	m_isFinished = FALSE;
	m_isOpen = TRUE;

	return hr;

Exit:
	if (pAMStream != NULL)
		pAMStream->Release();

	m_mmStream = NULL;
	m_isFinished = FALSE;
	m_isOpen = FALSE;

	CloseStream();

	return hr;
}

HRESULT	DirectVideo::PlayAll(void)
{
	HRESULT hr = m_mmStream->SetState(STREAMSTATE_RUN);
	if (FAILED(hr)) return hr;

	m_isFinished = false;

	while (true)
	{
		if (m_streamSample->Update(0, NULL, NULL, 0) != S_OK)
		{
			break;
		}
		hr = m_primarySurface->Blt(&m_destRect, m_streamSurface, &m_surfaceRect, DDBLT_WAIT, NULL);
	}

	return hr;
}

HRESULT	DirectVideo::PlayOne(void)
{
	HRESULT		hr = 0;

	if (m_isFinished) return hr;

	Assert(m_mmStream);

	if (!m_mmStream) {
		m_isPlaying = FALSE;

		return (HRESULT)-1;
	}

	hr = m_mmStream->SetState(STREAMSTATE_RUN);
	if (FAILED(hr)) return hr;

	m_isPlaying = true;

	if (m_lastFrameTime == 0) m_lastFrameTime = GetTickCount();
//	sint32 iterations = (GetTickCount() - m_lastFrameTime)/m_timePerFrame + 2;

	sint32	r = m_streamSample->Update(SSUPDATE_CONTINUOUS, NULL, NULL, 0);

	r = m_streamSample->CompletionStatus(COMPSTAT_WAIT, 0);
	if (r == MS_S_ENDOFSTREAM) {
		STREAM_TIME time = 0;

		m_mmStream->Seek(time);
	}

	hr = m_primarySurface->Blt(&m_destRect, m_streamSurface, &m_surfaceRect, DDBLT_WAIT, NULL);

	m_lastFrameTime = GetTickCount();

	if (m_flags == DVFLAGS_INWINDOW && !m_isModal) {
		Assert(m_window != NULL);
		if (m_window) {
			m_window->Invalidate(&m_destRect);
		}
	}

	return hr;
}

HRESULT	DirectVideo::CloseStream(void)
{
	HRESULT			hr = 0;

    if (m_primaryVidStream != NULL) {
		m_primaryVidStream->Release();
		m_primaryVidStream = NULL;
	}

    if (m_ddStream != NULL) {
		m_ddStream->Release();
		m_ddStream = NULL;
	}

    if (m_streamSample != NULL) {
		m_streamSample->Release();
		m_streamSample = NULL;
	}

    if (m_streamSurface != NULL) {
		m_streamSurface->Release();
		m_streamSurface = NULL;
	}

	if (m_mmStream != NULL) {
		m_mmStream->Release();
		m_mmStream = NULL;
	}

	return hr;
}

HRESULT DirectVideo::Process(void)
{
	if (m_flags == DVFLAGS_INWINDOW)
    {
        return (m_isModal) ? PlayAll() : PlayOne();
	}

	return 0;
}

void DirectVideo::Pause(void)
{
	if (m_isPlaying)
    {
		m_isPaused = true;

		if (m_mmStream)
        {
	        HRESULT hr = m_mmStream->SetState(STREAMSTATE_STOP);
			if (FAILED(hr)) return;
		}
	}
}

void DirectVideo::Resume(void)
{
	if (m_isPaused)
    {
		if (m_mmStream)
        {
			HRESULT hr = m_mmStream->SetState(STREAMSTATE_RUN);
			if (FAILED(hr)) return;
		}

		m_isPaused = false;
	}
}

#elif defined(__AUI_USE_SDL__) && defined(USE_SDL_FFMPEG)

#include "directvideo.h"

#include <SDL2/SDL.h>
extern "C" {
	#include <libavformat/avformat.h>
	#include <libavcodec/avcodec.h>
	#include <libswscale/swscale.h>
}

#include "aui_surface.h"
#include "aui_window.h"

DirectVideo::DirectVideo()
:
	m_isOpen                    (false),
	m_isPaused                  (false),
	m_window                    (NULL),
	m_frameBytesPerRow          (0),
	m_frames                    (),
	m_timePerFrame              (1000 / 15),
	m_startTime                 (0)
{
	m_destRect.left = m_destRect.top = m_destRect.right = m_destRect.bottom = 0;
}

DirectVideo::~DirectVideo()
{
	CloseStream();
}

HRESULT DirectVideo::Initialize(aui_Window *window)
{
	m_window = window;
	return m_window ? 0 : -1;
}

HRESULT DirectVideo::OpenStream(MBCHAR *name)
{
	CloseStream();

	aui_Surface *surface = m_window ? m_window->TheSurface() : NULL;
	sint32 const width  = m_destRect.right - m_destRect.left;
	sint32 const height = m_destRect.bottom - m_destRect.top;
	if (!surface || width <= 0 || height <= 0) return -1;

	AVPixelFormat dstFormat;
	switch (surface->PixelFormat())
	{
	case AUI_SURFACE_PIXELFORMAT_565: dstFormat = AV_PIX_FMT_RGB565; break;
	case AUI_SURFACE_PIXELFORMAT_555: dstFormat = AV_PIX_FMT_RGB555; break;
	case AUI_SURFACE_PIXELFORMAT_888: dstFormat = AV_PIX_FMT_0RGB32; break;
	default: return -1;
	}
	m_frameBytesPerRow = width * (surface->BitsPerPixel() / 8);

	AVFormatContext *format = NULL;
	if (avformat_open_input(&format, name, NULL, NULL) < 0) return -1;

	AVCodecContext *codec  = NULL;
	SwsContext     *scaler = NULL;
	AVPacket       *packet = av_packet_alloc();
	AVFrame        *frame  = av_frame_alloc();
	const AVCodec  *decoder = NULL;
	int stream = -1;

	if (packet && frame && avformat_find_stream_info(format, NULL) >= 0)
		stream = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
	if (stream >= 0)
	{
		codec = avcodec_alloc_context3(decoder);
		if (codec && (avcodec_parameters_to_context(codec, format->streams[stream]->codecpar) < 0
		          ||  avcodec_open2(codec, decoder, NULL) < 0))
			avcodec_free_context(&codec);

		AVRational rate = format->streams[stream]->avg_frame_rate;
		if (rate.num > 0 && rate.den > 0)
			m_timePerFrame = static_cast<uint32>(1000 * static_cast<int64_t>(rate.den) / rate.num);
	}

	// Decode the whole clip up front: the library clips are 160x120 and a few seconds long
	bool draining = false;
	while (codec)
	{
		if (!draining)
		{
			if (av_read_frame(format, packet) < 0)
			{
				draining = true;
				avcodec_send_packet(codec, NULL);
			}
			else
			{
				if (packet->stream_index == stream)
					avcodec_send_packet(codec, packet);
				av_packet_unref(packet);
			}
		}

		int got;
		while ((got = avcodec_receive_frame(codec, frame)) >= 0)
		{
			// Fit inside the rect keeping the aspect ratio: the wonder films are 5:3 and
			// the slot is 4:3, so they get black bars (a zeroed buffer is black)
			sint32 fitWidth  = width;
			sint32 fitHeight = height;
			if (frame->width * height > width * frame->height)
				fitHeight = (width * frame->height / frame->width) & ~1;
			else
				fitWidth  = (height * frame->width / frame->height) & ~1;
			size_t const offset = ((height - fitHeight) / 2) * m_frameBytesPerRow
			                    + ((width - fitWidth) / 2) * (surface->BitsPerPixel() / 8);

			scaler = sws_getCachedContext(scaler, frame->width, frame->height,
			                              static_cast<AVPixelFormat>(frame->format),
			                              fitWidth, fitHeight, dstFormat, SWS_BICUBIC, NULL, NULL, NULL);
			if (scaler)
			{
				m_frames.push_back(std::vector<uint8>(m_frameBytesPerRow * height));
				uint8 *dst[4]         = { m_frames.back().data() + offset, NULL, NULL, NULL };
				int    dstLinesize[4] = { m_frameBytesPerRow, 0, 0, 0 };
				sws_scale(scaler, frame->data, frame->linesize, 0, frame->height, dst, dstLinesize);
			}
			av_frame_unref(frame);
		}
		if (draining && got == AVERROR_EOF) break;
		if (draining && got != AVERROR(EAGAIN)) break;
	}

	sws_freeContext(scaler);
	av_frame_free(&frame);
	av_packet_free(&packet);
	avcodec_free_context(&codec);
	avformat_close_input(&format);

	m_isOpen    = !m_frames.empty();
	m_isPaused  = false;
	m_startTime = SDL_GetTicks();

	return m_isOpen ? 0 : -1;
}

HRESULT DirectVideo::PlayOne(void)
{
	if (!m_isOpen) return -1;
	if (m_isPaused) return 0;

	aui_Surface *surface = m_window->TheSurface();
	if (!surface || m_window->IsHidden()) return 0;

	size_t const frame = ((SDL_GetTicks() - m_startTime) / m_timePerFrame) % m_frames.size();
	sint32 const height = m_destRect.bottom - m_destRect.top;

	// Redraw every tick, not only on a new frame: a window redraw may have painted over it
	LPVOID buffer = NULL;
	if (surface->Lock(&m_destRect, &buffer, 0) != AUI_ERRCODE_OK) return 0; // try next tick
	uint8 const *src = m_frames[frame].data();
	uint8       *dst = static_cast<uint8 *>(buffer);
	for (sint32 y = 0; y < height; ++y)
	{
		memcpy(dst, src, m_frameBytesPerRow);
		src += m_frameBytesPerRow;
		dst += surface->Pitch();
	}
	surface->Unlock(buffer);

	m_window->Invalidate(&m_destRect);
	return 0;
}

HRESULT DirectVideo::CloseStream(void)
{
	m_frames.clear();
	m_isOpen = false;
	return 0;
}

#endif // __AUI_USE_DIRECTX__ / USE_SDL_FFMPEG
