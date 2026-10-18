#include "ctp2_config.h"
#include "c3.h"

#ifdef __AUI_USE_SDL__

#include "aui_ui.h"
#include "aui_sdlmouse.h"
#include "aui_rectangle.h"

#include "ctp2_listbox.h"
#include "c3_listbox.h"
#include "aui_ranger.h"

#include "civapp.h"
#include "aui_sdlsurface.h"

#include "profileDB.h"

extern CivApp*           g_civApp;
sint32                   aui_Mouse::m_mouseRefCount = 0;
SDL_mutex*               aui_SDLMouse::s_cs          = NULL;

int SDLMouseMessageHandler(void* userdata, SDL_Event* event);

// Whether copying srcRect of src to x, y would change the secondary surface.
// Most frames redraw windows (the map's sprites, the control panel) without
// changing a pixel; the screen is only uploaded and presented if one did.
static bool BltChangesSecondary(sint32 x, sint32 y, aui_Surface *src, const RECT *srcRect)
{
	aui_Surface *dest = g_ui->Secondary();
	if (!src || !dest || !dest->Buffer() || src->BitsPerPixel() != dest->BitsPerPixel())
	{
		return true;
	}

	RECT from       = *srcRect;
	RECT srcBounds  = { 0, 0, src->Width(), src->Height() };
	if (!Rectangle_Clip(&from, &srcBounds)) return false;
	RECT to         = from;
	OffsetRect(&to, x - srcRect->left, y - srcRect->top);
	RECT destBounds = { 0, 0, dest->Width(), dest->Height() };
	if (!Rectangle_Clip(&to, &destBounds)) return false;
	OffsetRect(&from, to.left - from.left + srcRect->left - x, to.top - from.top + srcRect->top - y);

	// Window surfaces only have a buffer while locked
	uint8 *srcBuf = NULL;
	if (src->Lock(NULL, (LPVOID *)&srcBuf, 0) != AUI_ERRCODE_OK || !srcBuf)
	{
		return true;
	}

	const sint32 bytesPerPixel = src->BitsPerPixel() / 8;
	const size_t bytes         = (to.right - to.left) * bytesPerPixel;
	bool         changed       = false;
	for (sint32 row = 0; row < to.bottom - to.top && !changed; ++row)
	{
		const uint8 *a = srcBuf + (from.top + row) * src->Pitch() + from.left * bytesPerPixel;
		const uint8 *b = dest->Buffer() + (to.top + row) * dest->Pitch() + to.left * bytesPerPixel;
		changed = memcmp(a, b, bytes) != 0;
	}

	src->Unlock(srcBuf);
	return changed;
}

uint32 HandleMouseAnimation(uint32 interval, void *param)
{
	aui_SDLMouse *mouse = (aui_SDLMouse*) param;
	mouse->HandleAnim();

	return interval;
}

aui_SDLMouse::aui_SDLMouse(
    AUI_ERRCODE *retval,
    const MBCHAR *ldlBlock,
    BOOL useExclusiveMode)
    :
    aui_Input            (),
    aui_Mouse            (retval, ldlBlock),
    aui_SDLInput         (retval, useExclusiveMode),
    m_currentCursor      (NULL),
    m_animationTimer     (0),
    m_lastFrameTick      (0)
{
	Assert(AUI_SUCCESS(*retval));
	if (!AUI_SUCCESS(*retval)) return;

	int x = 0, y = 0;
	SDL_GetMouseState(&x, &y);
	m_data.position.x = x;
	m_data.position.y = y;

	if (!m_mouseRefCount++)
	{
		s_cs = SDL_CreateMutex();
	}

	SDL_AddEventWatch(SDLMouseMessageHandler, this);
}

aui_SDLMouse::~aui_SDLMouse()
{
	SDL_DelEventWatch(SDLMouseMessageHandler, this);

	if (m_animationTimer)
	{
		SDL_RemoveTimer(m_animationTimer);
		m_animationTimer = 0;
	}

	if (!--m_mouseRefCount)
	{
		SDL_DestroyMutex(s_cs);
		s_cs = NULL;
	}

	End();
}

AUI_ERRCODE aui_SDLMouse::HandleAnim()
{
	Assert(m_lastIndex > m_firstIndex);
	Assert(m_curCursor >= m_cursors + m_firstIndex);

	if (m_curCursor++ >= m_cursors + m_lastIndex)
	{
		m_curCursor = m_cursors + m_firstIndex;
	}

	ActivateCursor(*m_curCursor);
	return AUI_ERRCODE_HANDLED;
}

int SDLMouseMessageHandler(void* userdata, SDL_Event* event)
{
	aui_SDLMouse* mouse = static_cast<aui_SDLMouse*>(userdata);
	switch (event->type)
	{
		case SDL_MOUSEMOTION:
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			mouse->GetInput(event);
			mouse->ManipulateInputs(mouse->GetLatestMouseEvent(), TRUE);
			break;
		default:
			break;
	}
	return 0;
}

void aui_SDLMouse::GetInput(SDL_Event* event)
{
	switch (event->type)
	{
		case SDL_MOUSEMOTION:
		{
			m_data.time = event->motion.timestamp;
			m_data.position.x = event->motion.x;
			m_data.position.y = event->motion.y;
			m_data.lbutton = ((event->motion.state & SDL_BUTTON_LMASK) != 0);
			m_data.rbutton = ((event->motion.state & SDL_BUTTON_RMASK) != 0);
			m_data.mbutton = ((event->motion.state & SDL_BUTTON_MMASK) != 0);
			m_data.tbutton = ((event->motion.state & SDL_BUTTON_X1MASK) != 0);
			m_data.ubutton = ((event->motion.state & SDL_BUTTON_X2MASK) != 0);

			break;
		}
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
		{
			m_data.time = event->button.timestamp;
			m_data.position.x = event->button.x;
			m_data.position.y = event->button.y;

			switch(event->button.button)
			{
				case SDL_BUTTON_LEFT:
					if (g_theProfileDB && g_theProfileDB->GetLeftHandedMouse())
					{
						// Left handed mouse so reverse the button
						m_data.rbutton = (event->button.state == SDL_PRESSED);
					}
					else
					{
						m_data.lbutton = (event->button.state == SDL_PRESSED);
					}
					break;
				case SDL_BUTTON_RIGHT:
					if (g_theProfileDB && g_theProfileDB->GetLeftHandedMouse())
					{
						// Left handed mouse so reverse the button
						m_data.lbutton = (event->button.state == SDL_PRESSED);
					}
					else
					{
						m_data.rbutton = (event->button.state == SDL_PRESSED);
					}
					break;
				case SDL_BUTTON_MIDDLE:
					m_data.mbutton = (event->button.state == SDL_PRESSED);
					break;
				case SDL_BUTTON_X1:
					m_data.tbutton = (event->button.state == SDL_PRESSED);
					break;
				case SDL_BUTTON_X2:
					m_data.ubutton = (event->button.state == SDL_PRESSED);
					break;
				default:
					break;
			}

			break;
		}
		default:
			break;
	}

	m_data.flags = m_flags;
}

sint32 aui_SDLMouse::ManipulateInputs(aui_MouseEvent *data, BOOL add)
{
	sint32 numManipulated = 0;

	static sint32 index = 0;
	// We have to pump event, otherwise we get into endless mouse scroll
	SDL_PumpEvents();

	SDL_mutexP(s_cs);

	if (add)
	{
		if (index <= k_MOUSE_MAXINPUT)
		{
			if (index > 1)
			{
				aui_MouseEvent *event = m_inputs + index - 2;
				if (event->position.x == data->position.x
					&&   event->position.y == data->position.y
					&&   event->lbutton == data->lbutton
					&&   event->rbutton == data->rbutton)
				{
					event++;
					if (event->position.x == data->position.x
						&&   event->position.y == data->position.y
						&&   event->lbutton == data->lbutton
						&&   event->rbutton == data->rbutton)
						index--;
				}
			}

			if (index < k_MOUSE_MAXINPUT)
			{
				memcpy(m_inputs + index, data, sizeof(aui_MouseEvent));
				numManipulated = 1;
				index++;
			}
		}
	}
	else
	{
		if (index)
			memcpy(data, m_inputs, index * sizeof(aui_MouseEvent));

		numManipulated = index;
		index = 0;
	}

	uint32 currentFrameTick = SDL_GetTicks();
	if (numManipulated > 0)
	{
		m_lastFrameTick = currentFrameTick;
	}
	else
	{
		// generate at least a single event every x ticks to force a redraw
		const int FRAMES_PER_SECOND = 60;
		const int TICKS_PER_FRAME = 1000 / FRAMES_PER_SECOND;

		if (currentFrameTick > m_lastFrameTick + TICKS_PER_FRAME)
		{
			m_lastFrameTick = currentFrameTick;
			data[0] = m_data;
			data[0].time = currentFrameTick;
			numManipulated = 1;
		}
	}

	SDL_mutexV(s_cs);

	return numManipulated;
}

void aui_SDLMouse::SetAnimIndexes(sint32 firstIndex, sint32 lastIndex)
{
	if (firstIndex == m_firstIndex && lastIndex == m_lastIndex)
		return;

	if (m_animationTimer)
	{
		SDL_RemoveTimer(m_animationTimer);
		m_animationTimer = 0;
	}
	aui_Mouse::SetAnimIndexes(firstIndex, lastIndex);
	if (firstIndex != lastIndex)
	{
		m_animationTimer = SDL_AddTimer(m_animDelay, HandleMouseAnimation, this);
	}
}

void aui_SDLMouse::ActivateCursor(aui_Cursor *cursor)
{
	if (cursor != m_currentCursor)
	{
		aui_Surface *cursorSurface = cursor->TheSurface();
		aui_SDLSurface *sdlCursorSurface = dynamic_cast<aui_SDLSurface *>(cursorSurface);
		if (sdlCursorSurface != NULL)
		{
			POINT hotspot;
			cursor->GetHotspot(hotspot);
			SDL_Cursor *sdlCursor = SDL_CreateColorCursor(sdlCursorSurface->GetSDLSurface(), hotspot.x, hotspot.y);
			if (sdlCursor != NULL)
			{
				SDL_Cursor *currentSDLCursor = SDL_GetCursor();
				SDL_SetCursor(sdlCursor);
				if (currentSDLCursor)
				{
					SDL_FreeCursor(currentSDLCursor);
				}

				m_currentCursor = cursor;
			}
		}
	}
}

AUI_ERRCODE	aui_SDLMouse::BltWindowToPrimary(aui_Window *window)
{
	AUI_ERRCODE retcode = AUI_ERRCODE_OK;

	sint32 windowX = window->X();
	sint32 windowY = window->Y();
	aui_Surface *windowSurface = window->TheSurface();
	aui_DirtyList *windowDirtyList = window->GetDirtyList();

	ListPos position = windowDirtyList->GetHeadPosition();
	for (size_t j = windowDirtyList->L(); j; j--)
	{
		RECT *windowDirtyRect = windowDirtyList->GetNext(position);

		RECT screenDirtyRect = *windowDirtyRect;
		OffsetRect(&screenDirtyRect, windowX, windowY);

		if (BltChangesSecondary(screenDirtyRect.left, screenDirtyRect.top, windowSurface, windowDirtyRect))
			g_ui->ScreenChanged();

		AUI_ERRCODE errcode = g_ui->BltToSecondary(
				screenDirtyRect.left,
				screenDirtyRect.top,
				windowSurface,
				windowDirtyRect,
				k_AUI_BLITTER_FLAG_COPY );
		Assert( errcode == AUI_ERRCODE_OK );
		g_ui->MapLayerBlt(screenDirtyRect.left, screenDirtyRect.top, window, windowDirtyRect);
		if ( errcode != AUI_ERRCODE_OK )
		{
			retcode = AUI_ERRCODE_BLTFAILED;
			break;
		}
	}

	return retcode;
}

AUI_ERRCODE	aui_SDLMouse::BltDirtyRectInfoToPrimary()
{
	AUI_ERRCODE retcode = AUI_ERRCODE_OK;
	AUI_ERRCODE errcode;

	if (g_civApp->IsInBackground()) return AUI_ERRCODE_OK;

	tech_WLList<aui_UI::DirtyRectInfo *> *driList = g_ui->GetDirtyRectInfoList();

	uint32 blitFlags;
	LPVOID primaryBuf = NULL;

	if (g_theProfileDB && g_theProfileDB->IsUseDirectXBlitter())
	{
		blitFlags = k_AUI_BLITTER_FLAG_COPY;
	}
	else
	{
		blitFlags = k_AUI_BLITTER_FLAG_COPY | k_AUI_BLITTER_FLAG_FAST;
		errcode = g_ui->Secondary()->Lock(NULL, &primaryBuf, 0);
		Assert( errcode == AUI_ERRCODE_OK );
	}

	ListPos position = driList->GetHeadPosition();
	for (size_t j = driList->L(); j; j--)
	{
		aui_UI::DirtyRectInfo *dri = driList->GetNext(position);

		aui_Window *window = dri->window;

		sint32 windowX = window->X();
		sint32 windowY = window->Y();
		aui_Surface *windowSurface = window->TheSurface();

		if (!windowSurface) continue;

		if (g_civApp->IsInBackground()) continue;

		RECT *windowDirtyRect = &dri->rect;

		RECT screenDirtyRect = *windowDirtyRect;
		OffsetRect(&screenDirtyRect, windowX, windowY);

		if (!g_civApp->IsInBackground()) // Actual Drawing
		{
			if (BltChangesSecondary(screenDirtyRect.left, screenDirtyRect.top, windowSurface, windowDirtyRect))
				g_ui->ScreenChanged();

			errcode = g_ui->BltToSecondary(
					screenDirtyRect.left,
					screenDirtyRect.top,
					windowSurface,
					windowDirtyRect,
					blitFlags);
			Assert(errcode == AUI_ERRCODE_OK);
			g_ui->MapLayerBlt(screenDirtyRect.left, screenDirtyRect.top, window, windowDirtyRect);
			if (errcode != AUI_ERRCODE_OK)
			{
				retcode = AUI_ERRCODE_BLTFAILED;
				break;
			}
		}
	}

	// The whole screen: only when something changed
	if (g_ui->IsScreenChanged())
	{
		errcode = g_ui->BltSecondaryToPrimary(blitFlags);
		Assert(errcode == AUI_ERRCODE_OK);
		if (errcode != AUI_ERRCODE_OK)
		{
			retcode = AUI_ERRCODE_BLTFAILED;
		}
	}

	if (!g_theProfileDB || !g_theProfileDB->IsUseDirectXBlitter())
	{
		errcode = g_ui->Secondary()->Unlock( primaryBuf );
		Assert( errcode == AUI_ERRCODE_OK );
	}

	return retcode;
}

AUI_ERRCODE	aui_SDLMouse::BltBackgroundColorToPrimary(COLORREF color, aui_DirtyList *colorAreas)
{
	if (g_civApp->IsInBackground()) return AUI_ERRCODE_OK;

	ListPos position = colorAreas->GetHeadPosition();
	for (size_t j = colorAreas->L(); j; j--)
	{
		RECT *screenDirtyRect = colorAreas->GetNext(position);
		AUI_ERRCODE errcode = g_ui->ColorBltToSecondary(screenDirtyRect, color,0);
		g_ui->ScreenChanged();
		g_ui->MapLayerBlt(screenDirtyRect->left, screenDirtyRect->top, NULL, screenDirtyRect);
		Assert(errcode == AUI_ERRCODE_OK);
		if (errcode != AUI_ERRCODE_OK)
		{
			return AUI_ERRCODE_BLTFAILED;
		}
	}

	return AUI_ERRCODE_OK;
}

AUI_ERRCODE	aui_SDLMouse::BltBackgroundImageToPrimary(aui_Image *image, RECT *imageRect, aui_DirtyList *imageAreas)
{
	if (g_civApp->IsInBackground()) return AUI_ERRCODE_OK;

	ListPos position = imageAreas->GetHeadPosition();
	for ( size_t j = imageAreas->L(); j; j-- )
	{
		RECT*         rectPtr = imageAreas->GetNext(position);
		RECT  screenDirtyRect = *rectPtr;
		OffsetRect(&screenDirtyRect, -imageRect->left, -imageRect->top);

		AUI_ERRCODE errcode = g_ui->BltToSecondary(
			rectPtr->left,
			rectPtr->top,
			image->TheSurface(),
			&screenDirtyRect,
			k_AUI_BLITTER_FLAG_COPY);
		g_ui->ScreenChanged();
		g_ui->MapLayerBlt(rectPtr->left, rectPtr->top, NULL, &screenDirtyRect);

		Assert(errcode == AUI_ERRCODE_OK);
		if (errcode != AUI_ERRCODE_OK)
		{
			return AUI_ERRCODE_BLTFAILED;
		}
	}

	return AUI_ERRCODE_OK;
}

#endif
