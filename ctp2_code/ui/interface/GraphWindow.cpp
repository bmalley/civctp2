//----------------------------------------------------------------------------
//
// Project      : Call To Power 2
// File type    : C++ source
// Description  : Large ranking graph window, with values
//
//----------------------------------------------------------------------------

#include "c3.h"
#include "GraphWindow.h"

#include <algorithm>
#include <math.h>
#include <stdio.h>

#include "aui_bitmapfont.h"
#include "aui_Factory.h"
#include "aui_ldl.h"
#include "aui_surface.h"
#include "aui_window.h"
#include "c3blitter.h"
#include "c3ui.h"
#include "Civilisation.h"
#include "CivilisationPool.h"   // g_theCivilisationPool
#include "colorset.h"           // g_colorSet
#include "ctp2_button.h"
#include "ctp2_dropdown.h"
#include "ctp2_listitem.h"
#include "ctp2_Static.h"
#include "ctp2_Window.h"
#include "Diffcly.h"            // diffutil_GetYearStringFromTurn
#include "GameSettings.h"       // g_theGameSettings
#include "gstypes.h"            // PLAYER_INDEX_VANDALS
#include "linegraph.h"          // eRankingCategories, linegraph_GetCombinedStrength
#include "player.h"             // g_player
#include "pointerlist.h"
#include "primitives.h"
#include "StrDB.h"              // g_theStringDB
#include "Strengths.h"
#include "textutils.h"
#include "TurnCnt.h"            // g_turn

extern C3UI *                   g_c3ui;
extern PointerList<Player> *    g_deadPlayer;

namespace
{
	sint32 const    k_LEGEND_WIDTH  = 200;
	sint32 const    k_LEGEND_LINE   = 18;
	sint32 const    k_AXIS_WIDTH    = 56;
	sint32 const    k_AXIS_HEIGHT   = 24;

	// The Ranking tab's categories, in its order
	struct Category
	{
		MBCHAR const *  name;
		sint32          category;
	};

	Category const  k_CATEGORIES[] =
	{
		{ "str_ldl_RANKING_MILITARY",   kRankingMilitary     },
		{ "str_ldl_RANKING_ECONOMIC",   kRankingEconomic     },
		{ "str_ldl_RANKING_SCIENTIFIC", kRankingScientific   },
		{ "str_ldl_RANKING_POLLUTION",  kRankingPollution    },
		{ "str_ldl_RANKING_TRADE",      kRankingTrade        },
		{ "str_ldl_RANKING_GOLD",       kRankingGold         },
		{ "str_ldl_RANKING_CITIES",     kRankingCities       },
		{ "str_ldl_RANKING_POPULATION", kRankingPopulation   },
		{ "str_ldl_RANKING_AREA",       kRankingGeographical },
		{ "str_ldl_RANKING_UNITS",      kRankingUnits        },
		{ "str_ldl_RANKING_BUILDINGS",  kRankingBuildings    },
		{ "str_ldl_RANKING_WONDERS",    kRankingWonders      },
		{ "str_ldl_RANKING_OVERALL",    kRankingOverall      },
	};
	sint32 const    k_NUM_CATEGORIES    = sizeof(k_CATEGORIES) / sizeof(k_CATEGORIES[0]);
	sint32 const    k_OVERALL           = k_NUM_CATEGORIES - 1;

	// Only English has these strings; other languages get the English text
	MBCHAR const * Str(MBCHAR const *id, MBCHAR const *english)
	{
		MBCHAR const *text = g_theStringDB->GetNameStr(id);
		return text ? text : english;
	}

	// Width as primitives_DrawText draws it (textutils_GetWidth is 0 in the
	// SDL build, which draws with a bitmap font)
	sint32 TextWidth(aui_Surface *surface, const MBCHAR *text)
	{
#ifdef __AUI_USE_DIRECTX__
		return textutils_GetWidth(surface, text);
#else
		aui_BitmapFont *font = getBitmapFont();
		if (!font) return 0;
		sint32 const width = font->GetStringWidth(text);
		freeBitmapFont(font);
		return width;
#endif
	}

	// A round 1, 2 or 5 times a power of ten, so that range / step <= lines
	double NiceStep(double range, sint32 lines)
	{
		if (range <= 0.0) return 1.0;
		double const raw = range / lines;
		double const magnitude = pow(10.0, floor(log10(raw)));
		static double const steps[] = { 1.0, 2.0, 5.0, 10.0 };
		for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); ++i)
		{
			if (steps[i] * magnitude >= raw) return steps[i] * magnitude;
		}
		return 10.0 * magnitude;
	}

	// 8-bit components (pixelutils_RGB takes 5-bit ones)
	Pixel16 Rgb(uint32 r, uint32 g, uint32 b)
	{
		if (g_is565Format)
			return static_cast<Pixel16>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
		else
			return static_cast<Pixel16>(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
	}

	void HLine(aui_Surface *surface, sint32 x1, sint32 x2, sint32 y, Pixel16 color)
	{
		RECT r = { x1, y, x2, y + 1 };
		primitives_PaintRect16(surface, &r, color);
	}

	void VLine(aui_Surface *surface, sint32 x, sint32 y1, sint32 y2, Pixel16 color)
	{
		if (y2 <= y1) return;
		RECT r = { x, y1, x + 1, y2 };
		primitives_PaintRect16(surface, &r, color);
	}

	GraphWindow *   s_graphWindow = NULL;
}

//----------------------------------------------------------------------------
// StatsGraph
//----------------------------------------------------------------------------

StatsGraph::StatsGraph(AUI_ERRCODE *retval, uint32 id, const MBCHAR *ldlBlock)
:
	aui_Control     (retval, id, ldlBlock),
	m_surface       (NULL),
	m_series        (),
	m_rounds        (0),
	m_share         (false),
	m_hover         (-1)
{
	AUI_ERRCODE errcode = AUI_ERRCODE_OK;
	m_surface = aui_Factory::new_Surface(errcode, m_width, m_height);
	Assert(AUI_NEWOK(m_surface, errcode));

	SetRect(&m_legendRect, m_width - k_LEGEND_WIDTH, 0, m_width, m_height);
	SetRect(&m_plotRect, k_AXIS_WIDTH, 8, m_legendRect.left - 12, m_height - k_AXIS_HEIGHT);

	m_drawMask = k_AUI_REGION_DRAWFLAG_UPDATE;
}

StatsGraph::~StatsGraph()
{
	delete m_surface;
}

void StatsGraph::Load(sint32 category)
{
	m_series.clear();
	m_rounds = g_turn ? g_turn->GetRound() : 0;
	m_hover = -1;

	// The players the Ranking graph shows: the living ones but the
	// barbarians, then the dead
	std::vector<Player *> players;
	for (sint32 i = 0; i < k_MAX_PLAYERS; ++i)
	{
		if (g_player[i] && i != PLAYER_INDEX_VANDALS)
			players.push_back(g_player[i]);
	}
	if (g_deadPlayer)
	{
		for (PointerList<Player>::Walker walk(g_deadPlayer); walk.IsValid(); walk.Next())
		{
			if (walk.GetObj()) players.push_back(walk.GetObj());
		}
	}

	for (size_t i = 0; i < players.size(); ++i)
	{
		Player *player = players[i];
		if (!player->m_strengths) continue;

		Series series;
		MBCHAR name[k_MAX_NAME_LEN] = "";
		Civilisation *civ = player->GetCivilisation();
		if (civ && g_theCivilisationPool->IsValid(*civ))
			civ->GetSingularCivName(name);
		series.name = name;
		series.color = g_colorSet->GetColor(g_colorSet->ComputePlayerColor(player->GetOwner()));
		for (sint32 round = 0; round < m_rounds; ++round)
		{
			series.values.push_back(
			    linegraph_GetCombinedStrength(*player->m_strengths, round, category));
		}
		m_series.push_back(series);
	}

	Render();
}

void StatsGraph::SetShare(bool share)
{
	m_share = share;
	Render();
}

double StatsGraph::Value(size_t series, sint32 round) const
{
	const std::vector<double> &values = m_series[series].values;
	return (round >= 0 && round < static_cast<sint32>(values.size())) ? values[round] : 0.0;
}

double StatsGraph::Total(sint32 round) const
{
	double total = 0.0;
	for (size_t s = 0; s < m_series.size(); ++s)
	{
		double const value = Value(s, round);
		if (value > 0.0) total += value;
	}
	return total;
}

sint32 StatsGraph::RoundX(sint32 round, sint32 rounds) const
{
	sint32 const width = m_plotRect.right - m_plotRect.left - 1;
	if (rounds < 2) return m_plotRect.left + width / 2;
	return m_plotRect.left + (round * width) / (rounds - 1);
}

void StatsGraph::Render(void)
{
	if (!m_surface) return;

	RECT all = { 0, 0, m_width, m_height };
	primitives_PaintRect16(m_surface, &all, g_colorSet->GetColor(COLOR_BLACK));

	if (m_rounds < 1 || m_series.empty())
	{
		primitives_DrawText(m_surface, 10, 20, Str("str_ldl_GRAPH_NONE", "Nothing to show before the first turn has ended."),
		                    g_colorSet->GetColorRef(COLOR_WHITE), true);
		ShouldDraw();
		return;
	}

	double top = 1.0;
	if (m_share)
	{
		top = 100.0;
	}
	else
	{
		double max = 0.0;
		for (size_t s = 0; s < m_series.size(); ++s)
			for (sint32 r = 0; r < m_rounds; ++r)
				max = std::max(max, Value(s, r));
		double const step = NiceStep(max, 5);
		top = std::max(step, ceil(max / step) * step);
	}

	RenderGrid(top, m_rounds);
	if (m_share)
		RenderShares(m_rounds);
	else
		RenderLines(top, m_rounds);

	if (m_hover >= 0)
	{
		VLine(m_surface, RoundX(m_hover, m_rounds), m_plotRect.top, m_plotRect.bottom,
		      g_colorSet->GetColor(COLOR_WHITE));
	}
	primitives_FrameRect16(m_surface, &m_plotRect, g_colorSet->GetColor(COLOR_GRAY));

	RenderLegend(m_hover >= 0 ? m_hover : m_rounds - 1);
	ShouldDraw();
}

void StatsGraph::RenderGrid(double top, sint32 rounds)
{
	Pixel16 const grid = Rgb(52, 52, 52);
	COLORREF const label = g_colorSet->GetColorRef(COLOR_GRAY);
	sint32 const height = m_plotRect.bottom - m_plotRect.top;
	MBCHAR text[64];

	// Values up the side
	double const step = m_share ? 25.0 : NiceStep(top, 5);
	for (double v = 0.0; v <= top + step / 2; v += step)
	{
		sint32 const y = m_plotRect.bottom - 1 - static_cast<sint32>((v / top) * (height - 1));
		HLine(m_surface, m_plotRect.left, m_plotRect.right, y, grid);
		if (m_share)
			sprintf(text, "%d%%", static_cast<sint32>(v));
		else
			sprintf(text, "%.0f", v);
		primitives_DrawText(m_surface, m_plotRect.left - 6 - TextWidth(m_surface, text), y,
		                    text, label, true);
	}

	// Turns along the bottom
	static sint32 const turnSteps[] = { 1, 2, 5, 10, 20, 25, 50, 100, 200, 250, 500, 1000 };
	sint32 turnStep = turnSteps[0];
	for (size_t i = 0; i < sizeof(turnSteps) / sizeof(turnSteps[0]); ++i)
	{
		turnStep = turnSteps[i];
		if (rounds / turnStep <= 8) break;
	}
	for (sint32 r = 0; r < rounds; r += turnStep)
	{
		sint32 const x = RoundX(r, rounds);
		VLine(m_surface, x, m_plotRect.top, m_plotRect.bottom, grid);
		sprintf(text, "%d", r);
		primitives_DrawText(m_surface, x - TextWidth(m_surface, text) / 2,
		                    m_plotRect.bottom + k_AXIS_HEIGHT / 2, text, label, true);
	}
}

void StatsGraph::RenderLines(double top, sint32 rounds)
{
	sint32 const height = m_plotRect.bottom - m_plotRect.top - 1;
	for (size_t s = 0; s < m_series.size(); ++s)
	{
		Pixel16 const color = m_series[s].color;
		sint32 lastX = 0;
		sint32 lastY = 0;
		for (sint32 r = 0; r < rounds; ++r)
		{
			double const value = std::max(0.0, Value(s, r));
			sint32 const x = RoundX(r, rounds);
			sint32 const y = m_plotRect.bottom - 1 - static_cast<sint32>((value / top) * height);
			if (r == 0 || rounds < 2)
			{
				RECT dot = { x - 1, y - 1, x + 2, y + 2 };
				primitives_PaintRect16(m_surface, &dot, color);
			}
			else
			{
				// Two pixels thick
				primitives_DrawLine16(m_surface, lastX, lastY, x, y, color);
				primitives_DrawLine16(m_surface, lastX, lastY - 1, x, y - 1, color);
			}
			lastX = x;
			lastY = y;
		}
	}
}

// Stacked shares of the total, one pixel column at a time
void StatsGraph::RenderShares(sint32 rounds)
{
	sint32 const width = m_plotRect.right - m_plotRect.left - 1;
	sint32 const height = m_plotRect.bottom - m_plotRect.top - 1;
	for (sint32 px = 0; px <= width; ++px)
	{
		sint32 const r = (rounds < 2 || width <= 0) ? 0 : (px * (rounds - 1) + width / 2) / width;
		double const total = Total(r);
		if (total <= 0.0) continue;

		double sum = 0.0;
		sint32 const x = m_plotRect.left + px;
		for (size_t s = 0; s < m_series.size(); ++s)
		{
			double const value = std::max(0.0, Value(s, r));
			if (value <= 0.0) continue;
			sint32 const y0 = m_plotRect.bottom - 1 - static_cast<sint32>((sum / total) * height);
			sum += value;
			sint32 const y1 = m_plotRect.bottom - 1 - static_cast<sint32>((sum / total) * height);
			VLine(m_surface, x, y1, y0 + 1, m_series[s].color);
		}
	}
}

void StatsGraph::RenderLegend(sint32 round)
{
	COLORREF const white = g_colorSet->GetColorRef(COLOR_WHITE);
	COLORREF const gray = g_colorSet->GetColorRef(COLOR_GRAY);
	sint32 const x = m_legendRect.left + 4;
	sint32 const right = m_legendRect.right - 4;
	sint32 y = m_legendRect.top + k_LEGEND_LINE / 2;
	MBCHAR text[128];

	// Which turn: its number and date
	sprintf(text, Str("str_ldl_GRAPH_TURN", "Turn %d"), round);
	primitives_DrawText(m_surface, x, y, text, white, true);

	std::string date;
	if (g_theGameSettings)
	{
		MBCHAR const *raw = diffutil_GetYearStringFromTurn(g_theGameSettings->GetDifficulty(), round);
		for (; raw && *raw; ++raw)
		{
			// The date strings carry formatting bytes; keep the printable part
			if (*raw >= ' ' && *raw <= '~') date += *raw;
		}
		size_t const first = date.find_first_not_of(' ');
		date = (first == std::string::npos) ? "" : date.substr(first);
	}
	primitives_DrawText(m_surface, right - TextWidth(m_surface, date.c_str()), y,
	                    date.c_str(), white, true);
	y += k_LEGEND_LINE + 4;

	// Highest first
	std::vector<std::pair<double, size_t> > order;
	for (size_t s = 0; s < m_series.size(); ++s)
		order.push_back(std::make_pair(-Value(s, round), s));
	std::stable_sort(order.begin(), order.end());

	double const total = Total(round);
	for (size_t i = 0; i < order.size() && y + k_LEGEND_LINE <= m_legendRect.bottom; ++i)
	{
		size_t const s = order[i].second;
		double const value = Value(s, round);

		RECT swatch = { x, y - 5, x + 10, y + 5 };
		primitives_PaintRect16(m_surface, &swatch, m_series[s].color);
		primitives_FrameRect16(m_surface, &swatch, g_colorSet->GetColor(COLOR_BLACK));

		COLORREF const color = value > 0.0 ? white : gray;
		primitives_DrawText(m_surface, x + 16, y, m_series[s].name.c_str(), color, true);

		if (m_share)
			sprintf(text, "%.0f%%", total > 0.0 ? 100.0 * std::max(0.0, value) / total : 0.0);
		else
			sprintf(text, "%.0f", value);
		primitives_DrawText(m_surface, right - TextWidth(m_surface, text), y, text, color, true);

		y += k_LEGEND_LINE;
	}
}

AUI_ERRCODE StatsGraph::DrawThis(aui_Surface *surface, sint32 x, sint32 y)
{
	if (!surface) surface = m_window->TheSurface();

	RECT rect = { 0, 0, m_width, m_height };
	OffsetRect(&rect, m_x + x, m_y + y);
	ToWindow(&rect);

	RECT src = { 0, 0, m_width, m_height };
	g_c3ui->TheBlitter()->Blt(surface, rect.left, rect.top, m_surface, &src,
	                          k_AUI_BLITTER_FLAG_COPY);

	if (surface == m_window->TheSurface())
		m_window->AddDirtyRect(&rect);

	return AUI_ERRCODE_OK;
}

void StatsGraph::Hover(sint32 x, sint32 y)
{
	sint32 hover = -1;
	sint32 const width = m_plotRect.right - m_plotRect.left - 1;
	if (m_rounds > 0 && x >= m_plotRect.left && x < m_plotRect.right
	 && y >= m_plotRect.top && y < m_plotRect.bottom)
	{
		hover = (m_rounds < 2 || width <= 0) ? 0
		      : ((x - m_plotRect.left) * (m_rounds - 1) + width / 2) / width;
	}

	if (hover != m_hover)
	{
		m_hover = hover;
		Render();
	}
}

// The position is in the parent's coordinates
void StatsGraph::MouseMoveOver(aui_MouseEvent *mouseData)
{
	aui_Control::MouseMoveOver(mouseData);
	Hover(mouseData->position.x - m_x, mouseData->position.y - m_y);
}

void StatsGraph::MouseMoveInside(aui_MouseEvent *mouseData)
{
	aui_Control::MouseMoveInside(mouseData);
	Hover(mouseData->position.x - m_x, mouseData->position.y - m_y);
}

void StatsGraph::MouseMoveAway(aui_MouseEvent *mouseData)
{
	aui_Control::MouseMoveAway(mouseData);
	Hover(-1, -1);
}

//----------------------------------------------------------------------------
// GraphWindow
//----------------------------------------------------------------------------

GraphWindow::GraphWindow()
:
	m_window        (static_cast<ctp2_Window *>(aui_Ldl::BuildHierarchyFromRoot("GraphDialog"))),
	m_graph         (static_cast<StatsGraph *>(aui_Ldl::GetObject("GraphDialog.Graph"))),
	m_category      (static_cast<ctp2_DropDown *>(aui_Ldl::GetObject("GraphDialog.RankSelect.Pulldown"))),
	m_shareButton   (static_cast<ctp2_Button *>(aui_Ldl::GetObject("GraphDialog.ShareButton"))),
	m_closeButton   (static_cast<ctp2_Button *>(aui_Ldl::GetObject("GraphDialog.CloseButton")))
{
	Assert(m_window && m_graph && m_category && m_shareButton && m_closeButton);

	if (m_category)
	{
		m_category->Clear();
		for (sint32 i = 0; i < k_NUM_CATEGORIES; ++i)
		{
			ctp2_ListItem *item = static_cast<ctp2_ListItem *>(
			    aui_Ldl::BuildHierarchyFromRoot("RankingListItem"));
			ctp2_Static *label = static_cast<ctp2_Static *>(item->GetChildByIndex(0));
			label->SetText(g_theStringDB->GetNameStr(k_CATEGORIES[i].name));
			m_category->AddItem(item);
		}
		m_category->SetSelectedItem(k_OVERALL);
		m_category->SetActionFuncAndCookie(DropDownCallback, this);
	}
	if (m_shareButton) m_shareButton->SetActionFuncAndCookie(ButtonCallback, this);
	if (m_closeButton) m_closeButton->SetActionFuncAndCookie(ButtonCallback, this);
}

GraphWindow::~GraphWindow()
{
	if (m_window)
	{
		aui_Ldl::DeleteHierarchyFromRoot("GraphDialog");
	}
}

void GraphWindow::Update(void)
{
	if (!m_graph || !m_category || !m_shareButton) return;

	sint32 item = m_category->GetSelectedItem();
	if (item < 0 || item >= k_NUM_CATEGORIES) item = k_OVERALL;
	m_graph->Load(k_CATEGORIES[item].category);

	// Named for what it switches to, as on the Ranking tab
	m_shareButton->SetText(g_theStringDB->GetNameStr(
	    m_graph->IsShare() ? "str_ldl_LINE_BUTTON" : "str_ldl_ZEROSUM_BUTTON"));
}

void GraphWindow::ButtonCallback(aui_Control *control, uint32 action, uint32 data, Cookie cookie)
{
	if (action != static_cast<uint32>(AUI_BUTTON_ACTION_EXECUTE))
		return;

	GraphWindow *window = static_cast<GraphWindow *>(cookie.m_voidPtr);
	if (!window) return;

	if (control == window->m_closeButton)
	{
		Close();
	}
	else if (control == window->m_shareButton && window->m_graph)
	{
		window->m_graph->SetShare(!window->m_graph->IsShare());
		window->Update();
	}
}

void GraphWindow::DropDownCallback(aui_Control *control, uint32 action, uint32 data, Cookie cookie)
{
	if (action != static_cast<uint32>(AUI_DROPDOWN_ACTION_SELECT))
		return;

	GraphWindow *window = static_cast<GraphWindow *>(cookie.m_voidPtr);
	if (window) window->Update();
}

void GraphWindow::Open(sint32 item)
{
	if (!s_graphWindow)
		s_graphWindow = new GraphWindow();

	if (!s_graphWindow->m_window) return;

	if (s_graphWindow->m_category && item >= 0 && item < k_NUM_CATEGORIES)
		s_graphWindow->m_category->SetSelectedItem(item);

	// Read the data afresh: turns may have passed
	s_graphWindow->Update();
	g_c3ui->AddWindow(s_graphWindow->m_window);
	s_graphWindow->m_window->Show();
}

void GraphWindow::Close(void)
{
	if (!s_graphWindow || !s_graphWindow->m_window) return;

	s_graphWindow->m_window->Hide();
	g_c3ui->RemoveWindow(s_graphWindow->m_window->Id());
}

void GraphWindow::Cleanup(void)
{
	Close();
	delete s_graphWindow;
	s_graphWindow = NULL;
}
