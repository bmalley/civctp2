#ifdef HAVE_PRAGMA_ONCE
#pragma once
#endif
#ifndef __DIRECTVIDEO_H__
#define __DIRECTVIDEO_H__

enum DVFLAGS {
	DVFLAGS_ONPRIMARY,
	DVFLAGS_FULLSCREEN,
	DVFLAGS_INWINDOW
};

#ifdef __AUI_USE_DIRECTX__
#include <mmstream.h>
#include <amstream.h>
#include <ddstream.h>

#include "aui_directui.h"
#include "aui_window.h"

class DirectVideo
{
public:
	DirectVideo();
	~DirectVideo();

	HRESULT		Initialize(aui_DirectUI *ui);
	HRESULT		Initialize(aui_DirectUI *ui, aui_Window *window, BOOL modal);
	HRESULT		OpenStream(MBCHAR *name);
	HRESULT		PlayAll(void);
	HRESULT		PlayOne(void);
	HRESULT		CloseStream(void);
	HRESULT		Process(void);

	void		GetDestRect(RECT *destRect) { *destRect = m_destRect; }
	void		SetDestRect(RECT *destRect) { m_destRect = *destRect; }

	void		GetVideoRect(RECT *videoRect) { *videoRect = m_surfaceRect; }

	BOOL		Finished(void) const { return m_isFinished; }
	BOOL		Playing(void) const { return m_isPlaying; }
	BOOL		Open(void) const { return m_isOpen; }

	void		Pause(void);
	void		Resume(void);

private:
	BOOL						m_isPlaying;
	BOOL						m_isOpen;
	BOOL						m_isFinished;
	BOOL						m_isPaused;

	BOOL						m_isModal;

	IMultiMediaStream			*m_mmStream;
	LPDIRECTDRAW				m_dd;
	IMediaStream				*m_primaryVidStream;
	IDirectDrawMediaStream		*m_ddStream;
	LPDIRECTDRAWSURFACE			m_primarySurface;
	LPDIRECTDRAWSURFACE			m_streamSurface;
	IDirectDrawStreamSample		*m_streamSample;
	RECT						m_surfaceRect;
	RECT						m_destRect;

	sint32						m_flags;
	aui_Window					*m_window;

	uint32						m_timePerFrame;
	uint32						m_lastFrameTime;
};
#elif defined(__AUI_USE_SDL__) && defined(USE_SDL_FFMPEG)
#include <vector>
#include "aui_window.h"

// Linux stand-in for the DirectShow player above, used by the Great Library for its
// short silent advance and unit clips: decodes the whole clip with FFmpeg into frames in
// the window surface's pixel format, then PlayOne() blits the current one, looping.
class DirectVideo
{
public:
	DirectVideo();
	~DirectVideo();

	HRESULT		Initialize(aui_Window *window);
	HRESULT		OpenStream(MBCHAR *name);
	HRESULT		PlayAll(void) { return PlayOne(); }
	HRESULT		PlayOne(void);
	HRESULT		CloseStream(void);
	HRESULT		Process(void) { return PlayOne(); }

	void		GetDestRect(RECT *destRect) { *destRect = m_destRect; }
	void		SetDestRect(RECT *destRect) { m_destRect = *destRect; }

	BOOL		Finished(void) const { return FALSE; }
	BOOL		Playing(void) const { return m_isOpen && !m_isPaused; }
	BOOL		Open(void) const { return m_isOpen; }

	void		Pause(void) { m_isPaused = true; }
	void		Resume(void) { m_isPaused = false; }

private:
	BOOL						m_isOpen;
	BOOL						m_isPaused;
	aui_Window					*m_window;
	RECT						m_destRect;
	sint32						m_frameBytesPerRow;
	std::vector<std::vector<uint8> >	m_frames;
	uint32						m_timePerFrame;
	uint32						m_startTime;
};
#endif

#endif
