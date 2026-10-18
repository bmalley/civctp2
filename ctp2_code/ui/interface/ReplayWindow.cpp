//----------------------------------------------------------------------------
//
// Project      : Call To Power 2
// File type    : C++ source
// Description  : Map timelapse window: plays back the replay recording
//
//----------------------------------------------------------------------------

#include "c3.h"
#include "ReplayWindow.h"

#include <stdio.h>
#include <algorithm>            // std::swap

#include "aui_bitmapfont.h"
#include "aui_Factory.h"
#include "aui_ldl.h"
#include "aui_surface.h"
#include "aui_window.h"
#include "c3blitter.h"
#include "c3ui.h"
#include "colorset.h"           // g_colorSet
#include "ctp2_button.h"
#include "ctp2_Window.h"
#include "pixelutils.h"
#include "primitives.h"
#include "StrDB.h"              // g_theStringDB
#include "textutils.h"

extern C3UI *   g_c3ui;

namespace
{
	sint32 const    k_TIMELINE_HEIGHT   = 28;
	sint32 const    k_LEGEND_WIDTH      = 170;
	sint32 const    k_LEGEND_LINE       = 18;
	sint32 const    k_GAP               = 6;

	// Playback takes about this long, however many rounds were recorded
	uint32 const    k_PLAY_TIME         = 20000;
	uint32 const    k_MIN_STEP          = 80;
	uint32 const    k_MAX_STEP          = 500;

	// 8-bit components (pixelutils_RGB takes 5-bit ones)
	Pixel16 Rgb(uint32 r, uint32 g, uint32 b)
	{
		if (g_is565Format)
			return static_cast<Pixel16>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
		else
			return static_cast<Pixel16>(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
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

	// Only English has these strings; other languages get the English text
	MBCHAR const * Str(MBCHAR const *id, MBCHAR const *english)
	{
		MBCHAR const *text = g_theStringDB->GetNameStr(id);
		return text ? text : english;
	}

	Pixel16 LandColor()     { return Rgb(86, 92, 70); }
	Pixel16 WaterColor()    { return Rgb(24, 40, 72); }

	ReplayWindow *  s_replayWindow = NULL;
}

//----------------------------------------------------------------------------
// ReplayMap
//----------------------------------------------------------------------------

ReplayMap::ReplayMap(AUI_ERRCODE *retval, uint32 id, const MBCHAR *ldlBlock)
:
	aui_Control     (retval, id, ldlBlock),
	m_surface       (NULL),
	m_game          (),
	m_rounds        (),
	m_inGame        (),
	m_index         (0),
	m_playing       (false),
	m_dragging      (false),
	m_lastStep      (0),
	m_stepTime      (k_MAX_STEP),
	m_stateCallback (NULL)
{
	AUI_ERRCODE errcode = AUI_ERRCODE_OK;
	m_surface = aui_Factory::new_Surface(errcode, m_width, m_height);
	Assert(AUI_NEWOK(m_surface, errcode));

	SetRect(&m_legendRect, m_width - k_LEGEND_WIDTH, 0, m_width, m_height);
	SetRect(&m_timelineRect, 0, m_height - k_TIMELINE_HEIGHT,
	        m_legendRect.left - k_GAP, m_height);
	SetRect(&m_mapRect, 0, 0, m_timelineRect.right, m_timelineRect.top - k_GAP);

	m_drawMask = k_AUI_REGION_DRAWFLAG_UPDATE;
	Render();
}

ReplayMap::~ReplayMap()
{
	delete m_surface;
}

void ReplayMap::SetGame(ReplayGame &game)
{
	std::swap(m_game, game);
	m_rounds.clear();
	for (std::map<sint32, ReplayRound>::const_iterator it = m_game.rounds.begin();
	     it != m_game.rounds.end(); ++it)
	{
		m_rounds.push_back(it->first);
	}

	m_inGame.assign(m_game.players.size(), false);
	for (std::map<sint32, ReplayRound>::const_iterator it = m_game.rounds.begin();
	     it != m_game.rounds.end(); ++it)
	{
		const ReplayRound &round = it->second;
		for (size_t i = 0; i < round.owners.size(); ++i)
		{
			if (round.owners[i] < m_inGame.size()) m_inGame[round.owners[i]] = true;
		}
		for (size_t i = 0; i < round.cities.size(); ++i)
		{
			if (round.cities[i].owner < m_inGame.size()) m_inGame[round.cities[i].owner] = true;
		}
	}

	m_stepTime = m_rounds.empty() ? k_MAX_STEP : k_PLAY_TIME / m_rounds.size();
	if (m_stepTime < k_MIN_STEP) m_stepTime = k_MIN_STEP;
	if (m_stepTime > k_MAX_STEP) m_stepTime = k_MAX_STEP;

	m_playing = false;
	m_index = m_rounds.empty() ? 0 : static_cast<sint32>(m_rounds.size()) - 1;
	Render();
}

void ReplayMap::SetPlaying(bool playing)
{
	if (playing && m_rounds.empty()) playing = false;

	if (playing && m_index + 1 >= static_cast<sint32>(m_rounds.size()))
	{
		SetIndex(0);    // play again from the start
	}

	m_playing = playing;
	m_lastStep = GetTickCount();
}

void ReplayMap::Step(sint32 delta)
{
	m_playing = false;
	SetIndex(m_index + delta);
}

void ReplayMap::SetIndex(sint32 index)
{
	sint32 const last = static_cast<sint32>(m_rounds.size()) - 1;
	if (index > last) index = last;
	if (index < 0) index = 0;

	if (index != m_index)
	{
		m_index = index;
		Render();
	}
}

AUI_ERRCODE ReplayMap::Idle(void)
{
	if (!m_playing) return AUI_ERRCODE_OK;

	uint32 const now = GetTickCount();
	if (now - m_lastStep < m_stepTime) return AUI_ERRCODE_OK;
	m_lastStep = now;

	if (m_index + 1 >= static_cast<sint32>(m_rounds.size()))
	{
		m_playing = false;
		if (m_stateCallback) m_stateCallback();
		return AUI_ERRCODE_OK;
	}

	SetIndex(m_index + 1);
	return AUI_ERRCODE_OK;
}

void ReplayMap::Render(void)
{
	if (!m_surface) return;

	RECT all = { 0, 0, m_width, m_height };
	primitives_PaintRect16(m_surface, &all, g_colorSet->GetColor(COLOR_BLACK));

	if (m_rounds.empty())
	{
		primitives_DrawText(m_surface, 10, 20,
		                    Str("str_ldl_REPLAY_NONE", "No timelapse recorded for this game yet (Record Map Timelapse, Ctrl+P)."),
		                    g_colorSet->GetColorRef(COLOR_WHITE), true);
	}
	else
	{
		const ReplayRound &round = m_game.rounds[m_rounds[m_index]];
		RenderMap(round);
		RenderTimeline(round);
		RenderLegend(round);
	}

	ShouldDraw();
}

// Laid out like the minimap (and tools/replay.py): row y is shifted y/2 tiles
// right, wrapping, and odd rows half a tile more. In half-tile units the map
// is 2w+1 wide and 2h high, and a tile is 2 by 2.
void ReplayMap::RenderMap(const ReplayRound &round)
{
	sint32 const w = m_game.w;
	sint32 const h = m_game.h;
	if (w <= 0 || h <= 0) return;

	sint32 const areaW = m_mapRect.right - m_mapRect.left;
	sint32 const areaH = m_mapRect.bottom - m_mapRect.top;

	// The largest size with square tiles that fits
	sint32 drawW = areaW;
	sint32 drawH = (areaW * 2 * h) / (2 * w + 1);
	if (drawH > areaH)
	{
		drawH = areaH;
		drawW = (areaH * (2 * w + 1)) / (2 * h);
	}
	sint32 const ox = m_mapRect.left + (areaW - drawW) / 2;
	sint32 const oy = m_mapRect.top + (areaH - drawH) / 2;
	sint32 const units = 2 * w + 1;

	std::vector<Pixel16> colors(m_game.players.size());
	std::vector<Pixel16> waterColors(m_game.players.size());
	for (size_t p = 0; p < m_game.players.size(); ++p)
	{
		const ReplayPlayer &player = m_game.players[p];
		colors[p] = Rgb(player.r, player.g, player.b);
		waterColors[p] = Rgb(player.r / 2, player.g / 2, player.b / 2);
	}

	for (sint32 y = 0; y < h; ++y)
	{
		RECT tile;
		tile.top = oy + (y * drawH) / h;
		tile.bottom = oy + ((y + 1) * drawH) / h;

		for (sint32 x = 0; x < w; ++x)
		{
			size_t const i = y * w + x;
			uint8 const owner = i < round.owners.size() ? round.owners[i] : 255;
			bool const land = i < m_game.land.size() && m_game.land[i];

			Pixel16 color;
			if (owner < colors.size())
				color = land ? colors[owner] : waterColors[owner];
			else
				color = land ? LandColor() : WaterColor();

			sint32 const sx = 2 * ((x + y / 2) % w) + (y & 1);
			tile.left = ox + (sx * drawW) / units;
			tile.right = ox + ((sx + 2) * drawW) / units;
			primitives_PaintRect16(m_surface, &tile, color);
		}
	}

	// Cities: a white square with a black edge, bigger for bigger cities
	sint32 const tileSize = drawW / w;
	for (size_t c = 0; c < round.cities.size(); ++c)
	{
		const ReplayCity &city = round.cities[c];
		if (city.x >= w || city.y >= h) continue;

		sint32 const sx = 2 * ((city.x + city.y / 2) % w) + (city.y & 1) + 1;
		sint32 const cx = ox + (sx * drawW) / units;
		sint32 const cy = oy + ((2 * city.y + 1) * drawH) / (2 * h);
		sint32 r = tileSize / 3 + city.size / 8;
		if (r < 1) r = 1;
		if (r > tileSize) r = tileSize;

		RECT dot = { cx - r - 1, cy - r - 1, cx + r + 1, cy + r + 1 };
		primitives_PaintRect16(m_surface, &dot, g_colorSet->GetColor(COLOR_BLACK));
		InflateRect(&dot, -1, -1);
		primitives_PaintRect16(m_surface, &dot, g_colorSet->GetColor(COLOR_WHITE));
	}
}

void ReplayMap::RenderTimeline(const ReplayRound &round)
{
	RECT bar = m_timelineRect;
	primitives_PaintRect16(m_surface, &bar, Rgb(40, 40, 40));

	sint32 const count = static_cast<sint32>(m_rounds.size());
	sint32 const width = bar.right - bar.left;
	RECT done = bar;
	done.right = bar.left + (count > 1 ? (m_index * width) / (count - 1) : width);
	primitives_PaintRect16(m_surface, &done, Rgb(90, 80, 40));

	RECT knob = { done.right - 2, bar.top, done.right + 2, bar.bottom };
	if (knob.left < bar.left) OffsetRect(&knob, bar.left - knob.left, 0);
	if (knob.right > bar.right) OffsetRect(&knob, bar.right - knob.right, 0);
	primitives_PaintRect16(m_surface, &knob, g_colorSet->GetColor(COLOR_YELLOW));
	primitives_FrameRect16(m_surface, &bar, g_colorSet->GetColor(COLOR_GRAY));

	MBCHAR text[128];
	sprintf(text, "%s", round.date.c_str());
	sint32 const textY = bar.top + (bar.bottom - bar.top) / 2;
	primitives_DrawText(m_surface, bar.left + 8, textY, text,
	                    g_colorSet->GetColorRef(COLOR_WHITE), true);

	sprintf(text, Str("str_ldl_REPLAY_ROUND", "Round %d / %d"),
	        m_rounds[m_index], m_rounds.back());
	sint32 const textW = TextWidth(m_surface, text);
	primitives_DrawText(m_surface, bar.right - textW - 8, textY, text,
	                    g_colorSet->GetColorRef(COLOR_WHITE), true);
}

void ReplayMap::RenderLegend(const ReplayRound &round)
{
	size_t const n = m_game.players.size();
	std::vector<sint32> cities(n, 0);
	std::vector<sint32> tiles(n, 0);
	for (size_t i = 0; i < round.cities.size(); ++i)
	{
		if (round.cities[i].owner < n) ++cities[round.cities[i].owner];
	}
	for (size_t i = 0; i < round.owners.size(); ++i)
	{
		if (round.owners[i] < n && i < m_game.land.size() && m_game.land[i])
			++tiles[round.owners[i]];
	}

	sint32 x = m_legendRect.left + 4;
	sint32 y = m_legendRect.top + k_LEGEND_LINE / 2;
	sint32 const right = m_legendRect.right - 4;
	COLORREF const white = g_colorSet->GetColorRef(COLOR_WHITE);
	COLORREF const gray = g_colorSet->GetColorRef(COLOR_GRAY);

	MBCHAR const *header = Str("str_ldl_REPLAY_CIVS", "Civilization");
	primitives_DrawText(m_surface, x, y, header, gray, true);
	header = Str("str_ldl_REPLAY_CITIES", "Cities");
	primitives_DrawText(m_surface, right - TextWidth(m_surface, header), y,
	                    header, gray, true);
	y += k_LEGEND_LINE + 4;

	for (size_t p = 0; p < n && y + k_LEGEND_LINE <= m_legendRect.bottom; ++p)
	{
		if (!m_inGame[p]) continue;

		const ReplayPlayer &player = m_game.players[p];
		RECT swatch = { x, y - 5, x + 10, y + 5 };
		primitives_PaintRect16(m_surface, &swatch, Rgb(player.r, player.g, player.b));
		primitives_FrameRect16(m_surface, &swatch, g_colorSet->GetColor(COLOR_BLACK));

		bool const alive = cities[p] > 0 || tiles[p] > 0;
		primitives_DrawText(m_surface, x + 16, y, player.name.c_str(), alive ? white : gray, true);

		MBCHAR count[32];
		sprintf(count, "%d", cities[p]);
		primitives_DrawText(m_surface, right - TextWidth(m_surface, count), y,
		                    count, alive ? white : gray, true);

		y += k_LEGEND_LINE;
	}
}

AUI_ERRCODE ReplayMap::DrawThis(aui_Surface *surface, sint32 x, sint32 y)
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

void ReplayMap::SeekTo(sint32 x)
{
	sint32 const count = static_cast<sint32>(m_rounds.size());
	sint32 const width = m_timelineRect.right - m_timelineRect.left;
	if (count < 2 || width <= 0) return;

	sint32 const pos = x - m_timelineRect.left;
	SetIndex((pos * (count - 1) + width / 2) / width);
}

void ReplayMap::MouseLGrabInside(aui_MouseEvent *mouseData)
{
	if (IsDisabled()) return;
	if (GetWhichSeesMouse() && GetWhichSeesMouse() != this) return;
	SetWhichSeesMouse(this);

	// The position is in the parent's coordinates
	POINT const local = { mouseData->position.x - m_x, mouseData->position.y - m_y };
	if (local.y < m_timelineRect.top - k_GAP || local.x >= m_timelineRect.right) return;

	SetMouseOwnership();
	m_dragging = true;
	bool const wasPlaying = m_playing;
	m_playing = false;
	SeekTo(local.x);
	if (wasPlaying && m_stateCallback) m_stateCallback();
	m_mouseCode = AUI_ERRCODE_HANDLEDEXCLUSIVE;
}

void ReplayMap::MouseLDragInside(aui_MouseEvent *mouseData)
{
	if (!m_dragging) return;
	SeekTo(mouseData->position.x - m_x);
	m_mouseCode = AUI_ERRCODE_HANDLEDEXCLUSIVE;
}

void ReplayMap::MouseLDragOutside(aui_MouseEvent *mouseData)
{
	MouseLDragInside(mouseData);
}

void ReplayMap::MouseLDropInside(aui_MouseEvent *mouseData)
{
	if (!m_dragging) return;
	m_dragging = false;
	ReleaseMouseOwnership();
	m_mouseCode = AUI_ERRCODE_HANDLEDEXCLUSIVE;
}

void ReplayMap::MouseLDropOutside(aui_MouseEvent *mouseData)
{
	MouseLDropInside(mouseData);
}

//----------------------------------------------------------------------------
// ReplayWindow
//----------------------------------------------------------------------------

ReplayWindow::ReplayWindow()
:
	m_window        (static_cast<ctp2_Window *>(aui_Ldl::BuildHierarchyFromRoot("ReplayDialog"))),
	m_map           (static_cast<ReplayMap *>(aui_Ldl::GetObject("ReplayDialog.Map"))),
	m_playButton    (static_cast<ctp2_Button *>(aui_Ldl::GetObject("ReplayDialog.PlayButton"))),
	m_backButton    (static_cast<ctp2_Button *>(aui_Ldl::GetObject("ReplayDialog.BackButton"))),
	m_forwardButton (static_cast<ctp2_Button *>(aui_Ldl::GetObject("ReplayDialog.ForwardButton"))),
	m_closeButton   (static_cast<ctp2_Button *>(aui_Ldl::GetObject("ReplayDialog.CloseButton")))
{
	Assert(m_window && m_map && m_playButton && m_backButton && m_forwardButton && m_closeButton);

	if (m_map) m_map->SetStateCallback(StateChanged);
	if (m_playButton) m_playButton->SetActionFuncAndCookie(ButtonCallback, this);
	if (m_backButton) m_backButton->SetActionFuncAndCookie(ButtonCallback, this);
	if (m_forwardButton) m_forwardButton->SetActionFuncAndCookie(ButtonCallback, this);
	if (m_closeButton) m_closeButton->SetActionFuncAndCookie(ButtonCallback, this);
}

ReplayWindow::~ReplayWindow()
{
	if (m_window)
	{
		aui_Ldl::DeleteHierarchyFromRoot("ReplayDialog");
	}
}

void ReplayWindow::Load(void)
{
	if (!m_map) return;

	ReplayGame game;
	if (!replay_LoadCurrent(game))
	{
		game = ReplayGame();
	}
	m_map->SetGame(game);
	UpdateButtons();
}

void ReplayWindow::UpdateButtons(void)
{
	if (!m_map || !m_playButton) return;

	m_playButton->SetText(m_map->IsPlaying() ? Str("str_ldl_REPLAY_PAUSE", "Pause")
	                                          : Str("str_ldl_REPLAY_PLAY", "Play"));

	bool const enable = m_map->HasRounds();
	if (enable)
	{
		m_playButton->Enable(TRUE);
		m_backButton->Enable(TRUE);
		m_forwardButton->Enable(TRUE);
	}
	else
	{
		m_playButton->Enable(FALSE);
		m_backButton->Enable(FALSE);
		m_forwardButton->Enable(FALSE);
	}
}

void ReplayWindow::StateChanged(void)
{
	if (s_replayWindow) s_replayWindow->UpdateButtons();
}

void ReplayWindow::ButtonCallback(aui_Control *control, uint32 action, uint32 data, Cookie cookie)
{
	if (action != static_cast<uint32>(AUI_BUTTON_ACTION_EXECUTE))
		return;

	ReplayWindow *window = static_cast<ReplayWindow *>(cookie.m_voidPtr);
	if (!window || !window->m_map) return;

	if (control == window->m_closeButton)
	{
		Close();
		return;
	}

	if (control == window->m_playButton)
		window->m_map->SetPlaying(!window->m_map->IsPlaying());
	else if (control == window->m_backButton)
		window->m_map->Step(-1);
	else if (control == window->m_forwardButton)
		window->m_map->Step(1);

	window->UpdateButtons();
}

void ReplayWindow::Open(void)
{
	if (!s_replayWindow)
		s_replayWindow = new ReplayWindow();

	if (!s_replayWindow->m_window) return;

	// Read the file afresh: a round may have been recorded since
	s_replayWindow->Load();
	g_c3ui->AddWindow(s_replayWindow->m_window);
	s_replayWindow->m_window->Show();
}

void ReplayWindow::Close(void)
{
	if (!s_replayWindow || !s_replayWindow->m_window) return;

	if (s_replayWindow->m_map) s_replayWindow->m_map->SetPlaying(false);
	s_replayWindow->m_window->Hide();
	g_c3ui->RemoveWindow(s_replayWindow->m_window->Id());
}

void ReplayWindow::Cleanup(void)
{
	Close();
	delete s_replayWindow;
	s_replayWindow = NULL;
}
