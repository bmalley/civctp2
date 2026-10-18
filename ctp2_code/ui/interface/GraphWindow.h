//----------------------------------------------------------------------------
//
// Project      : Call To Power 2
// File type    : C++ header
// Description  : Large ranking graph window, with values
//
//----------------------------------------------------------------------------

#ifdef HAVE_PRAGMA_ONCE
#pragma once
#endif

#ifndef __GRAPHWINDOW_H__
#define __GRAPHWINDOW_H__

#include <string>
#include <vector>

#include "aui_control.h"
#include "pixelutils.h"         // Pixel16

class aui_Surface;
class ctp2_Button;
class ctp2_DropDown;
class ctp2_Window;

// One ranking category over time for every civ, with a labelled grid, and a
// legend sorted by value. Pointing at the graph reads off that turn's values.
// As lines, or as each civ's share of the total. Built from ldl as objecttype
// "ctp2_StatsGraph".
class StatsGraph : public aui_Control
{
public:
	StatsGraph(AUI_ERRCODE *retval, uint32 id, const MBCHAR *ldlBlock);
	virtual ~StatsGraph();

	// category: an eRankingCategories value, or -1 for the score history
	void    Load(sint32 category);
	void    SetShare(bool share);
	bool    IsShare() const { return m_share; }

	virtual AUI_ERRCODE DrawThis(aui_Surface *surface, sint32 x, sint32 y);

	virtual void MouseMoveOver(aui_MouseEvent *mouseData);
	virtual void MouseMoveInside(aui_MouseEvent *mouseData);
	virtual void MouseMoveAway(aui_MouseEvent *mouseData);

private:
	struct Series
	{
		std::string             name;
		sint32                  owner;      // player index
		Pixel16                 color;
		std::vector<double>     values;     // one a round, from round 0; < 0: missing
	};

	void    LoadScores(void);
	void    Render(void);
	void    RenderGrid(double top, sint32 rounds);
	void    RenderLines(double top, sint32 rounds);
	void    RenderShares(sint32 rounds);
	void    RenderLegend(sint32 round);
	void    Hover(sint32 x, sint32 y);

	double  Value(size_t series, sint32 round) const;
	double  Total(sint32 round) const;
	sint32  RoundX(sint32 round, sint32 rounds) const;

	aui_Surface *       m_surface;
	std::vector<Series> m_series;
	sint32              m_rounds;
	bool                m_share;
	bool                m_hasData;      // false: a score history with no records
	sint32              m_hover;        // round pointed at, or -1
	RECT                m_plotRect;
	RECT                m_legendRect;
};

class GraphWindow
{
public:
	// item: the category's index in the Ranking tab's list (the same order)
	static void Open(sint32 item);
	static void Close(void);
	static void Cleanup(void);

private:
	GraphWindow();
	~GraphWindow();

	void    Update(void);

	static void ButtonCallback(aui_Control *control, uint32 action, uint32 data, Cookie cookie);
	static void DropDownCallback(aui_Control *control, uint32 action, uint32 data, Cookie cookie);

	ctp2_Window *   m_window;
	StatsGraph *    m_graph;
	ctp2_DropDown * m_category;
	ctp2_Button *   m_shareButton;
	ctp2_Button *   m_closeButton;
};

#endif
