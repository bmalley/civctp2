//----------------------------------------------------------------------------
//
// Project      : Call To Power 2
// File type    : C++ header
// Description  : Map timelapse window: plays back the replay recording
//
//----------------------------------------------------------------------------

#ifdef HAVE_PRAGMA_ONCE
#pragma once
#endif

#ifndef __REPLAYWINDOW_H__
#define __REPLAYWINDOW_H__

#include "aui_control.h"
#include "Replay.h"

class aui_Surface;
class ctp2_Button;
class ctp2_Window;

// The map at one round of the recording, a timeline strip under it to click
// or drag along, and a legend with each civ's city count. Built from
// ldl as objecttype "ctp2_ReplayMap".
class ReplayMap : public aui_Control
{
public:
	ReplayMap(AUI_ERRCODE *retval, uint32 id, const MBCHAR *ldlBlock);
	virtual ~ReplayMap();

	void    SetGame(ReplayGame &game);     // takes the contents
	bool    HasRounds() const { return !m_rounds.empty(); }

	void    SetPlaying(bool playing);
	bool    IsPlaying() const { return m_playing; }
	void    Step(sint32 delta);

	virtual AUI_ERRCODE DrawThis(aui_Surface *surface, sint32 x, sint32 y);
	virtual AUI_ERRCODE Idle(void);

	virtual void MouseLGrabInside(aui_MouseEvent *mouseData);
	virtual void MouseLDragInside(aui_MouseEvent *mouseData);
	virtual void MouseLDragOutside(aui_MouseEvent *mouseData);
	virtual void MouseLDropInside(aui_MouseEvent *mouseData);
	virtual void MouseLDropOutside(aui_MouseEvent *mouseData);

	// Called when playback starts or stops by itself
	typedef void (StateCallback)(void);
	void    SetStateCallback(StateCallback *func) { m_stateCallback = func; }

private:
	void    SetIndex(sint32 index);
	void    Render(void);
	void    RenderMap(const ReplayRound &round);
	void    RenderTimeline(const ReplayRound &round);
	void    RenderLegend(const ReplayRound &round);
	void    SeekTo(sint32 x);

	aui_Surface *       m_surface;
	ReplayGame          m_game;
	std::vector<sint32> m_rounds;       // recorded round numbers, in order
	std::vector<bool>   m_inGame;       // players ever seen on the map
	sint32              m_index;        // into m_rounds
	bool                m_playing;
	bool                m_dragging;
	uint32              m_lastStep;     // GetTickCount of the last step
	uint32              m_stepTime;     // ms per round while playing
	RECT                m_mapRect;
	RECT                m_timelineRect;
	RECT                m_legendRect;
	StateCallback *     m_stateCallback;
};

class ReplayWindow
{
public:
	static void Open(void);
	static void Close(void);
	static void Cleanup(void);

private:
	ReplayWindow();
	~ReplayWindow();

	void    Load(void);
	void    UpdateButtons(void);

	static void StateChanged(void);
	static void ButtonCallback(aui_Control *control, uint32 action, uint32 data, Cookie cookie);

	ctp2_Window *   m_window;
	ReplayMap *     m_map;
	ctp2_Button *   m_playButton;
	ctp2_Button *   m_backButton;
	ctp2_Button *   m_forwardButton;
	ctp2_Button *   m_closeButton;
};

#endif
