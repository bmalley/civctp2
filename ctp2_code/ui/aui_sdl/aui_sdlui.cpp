//----------------------------------------------------------------------------
//
// Project      : Call To Power 2
// File type    : C++ source
// Description  : SDL user interface handling
//
//----------------------------------------------------------------------------
//
// Disclaimer
//
// THIS FILE IS NOT GENERATED OR SUPPORTED BY ACTIVISION.
//
// This material has been developed at apolyton.net by the Apolyton CtP2
// Source Code Project. Contact the authors at ctp2source@apolyton.net.
//
//----------------------------------------------------------------------------
//
// Compiler flags
//
// __AUI_USE_SDL__
//
//----------------------------------------------------------------------------
//
// Modifications from the original Activision code:
//
// - Prevented crashes
//
//----------------------------------------------------------------------------

#include <iostream>

#include "c3.h"

#ifdef __AUI_USE_SDL__

#include "aui_sdlui.h"

#include "civ3_main.h"

#include "aui_mouse.h"
#include "aui_keyboard.h"
#include "aui_rectangle.h"              // Rectangle_Clip
#include "aui_joystick.h"
#include "aui_sdlsurface.h"
#include "aui_sdlmouse.h"
#include "aui_sdlmoviemanager.h"
#include "civapp.h"
#include "profileDB.h"                  // g_theProfileDB

extern BOOL                 g_exclusiveMode;
extern CivApp*              g_civApp;
extern ProfileDB*           g_theProfileDB;

extern BOOL                 g_createDirectDrawOnSecondary;
extern sint32               g_ScreenWidth;
extern sint32               g_ScreenHeight;

extern sint32               g_is565Format;

aui_SDLUI::aui_SDLUI
(
	AUI_ERRCODE *retval,
	HINSTANCE hinst,
	HWND hwnd,
	sint32 width,
	sint32 height,
	sint32 bpp,
	const MBCHAR *ldlFilename,
	BOOL useExclusiveMode
)
:   aui_UI              (),
    aui_SDL             (),
    m_SDLWindow         (0),
    m_SDLRenderer       (0),
    m_SDLTexture        (0),
    m_mapMix            (NULL),
    m_mapWindow         (NULL),
    m_mapScale          (1.0),
    m_mapTexture        (NULL),
    m_uiTexture         (NULL),
    m_uiColors          (NULL),
    m_mapMask           (NULL),
    m_mapShadow         (NULL),
    m_mapShadowStale    (true),
    m_lastPresent       (0)
{
	*retval = aui_Region::InitCommon( 0, 0, 0, width, height );
	Assert( AUI_SUCCESS(*retval) );
	if ( !AUI_SUCCESS(*retval) ) return;

	Assert( aui_Base::GetBaseRefCount() == 2 );
	g_ui = aui_Base::GetBaseRefCount() == 2 ? this : NULL;

	*retval = aui_UI::InitCommon( hinst, hwnd, bpp, ldlFilename );
	Assert( AUI_SUCCESS(*retval) );
	if ( !AUI_SUCCESS(*retval) ) return;

	*retval = InitCommon();
	Assert( AUI_SUCCESS(*retval) );
	if ( !AUI_SUCCESS(*retval) ) return;

	*retval = CreateNativeScreen( useExclusiveMode );
	Assert( AUI_SUCCESS(*retval) );
	if ( !AUI_SUCCESS(*retval) ) return;
}

AUI_ERRCODE aui_SDLUI::InitCommon()
{
	m_savedMouseAnimFirstIndex = 0;
	m_savedMouseAnimLastIndex = 0;
	m_savedMouseAnimCurIndex = 0;

	return AUI_ERRCODE_OK;
}

AUI_ERRCODE aui_SDLUI::DestroyNativeScreen(void)
{
	if (m_primary)
	{
		delete m_primary;
		m_primary   = NULL;
	}

	return AUI_ERRCODE_OK;
}

AUI_ERRCODE aui_SDLUI::CreateNativeScreen( BOOL useExclusiveMode )
{
	AUI_ERRCODE errcode = aui_SDL::InitCommon( useExclusiveMode );
	Assert( AUI_SUCCESS(errcode) );
	if ( !AUI_SUCCESS(errcode) ) return errcode;

	m_pixelFormat = aui_Surface::TransformBppToSurfacePixelFormat(m_bpp);

	// Windows are right now not resizable, the content is also be shrunken, which should not happen.
	uint32 sdl_flags = g_theProfileDB->IsWindowedMode() ? SDL_WINDOW_ALLOW_HIGHDPI /*| SDL_WINDOW_RESIZABLE*/ : SDL_WINDOW_FULLSCREEN_DESKTOP;

	m_SDLWindow = SDL_CreateWindow("Call To Power 2", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
	                               m_width, m_height, sdl_flags);

	if (!m_SDLWindow)
	{
		c3errors_FatalDialog("aui_SDLUI", "SDL window creation failed:\n%s\n", SDL_GetError());
	}

	if (g_theProfileDB->IsWindowedMode())
		SDL_SetWindowMinimumSize(m_SDLWindow, 800, 600); // These numbers should be constants.

	m_SDLRenderer = SDL_CreateRenderer(m_SDLWindow, -1, 0);
	if (!m_SDLRenderer)
	{
		c3errors_FatalDialog("aui_SDLUI", "SDL renderer creation failed:\n%s\n", SDL_GetError());
	}

	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	SDL_RenderSetLogicalSize(m_SDLRenderer, m_width, m_height);
	m_SDLTexture = SDL_CreateTexture(m_SDLRenderer, aui_SDLSurface::TransformSurfacePixelFormatToSDL(m_pixelFormat),
		SDL_TEXTUREACCESS_STREAMING, m_width, m_height);

	if (!m_SDLTexture)
	{
		c3errors_FatalDialog("aui_SDLUI", "SDL texture creation failed:\n%s\n", SDL_GetError());
	}

	m_primary = new aui_SDLSurface(&errcode, m_width, m_height, m_bpp, NULL, TRUE);
	Assert( AUI_NEWOK(m_primary,errcode) );
	if ( !AUI_NEWOK(m_primary,errcode) ) return AUI_ERRCODE_MEMALLOCFAILED;

	if(!m_secondary)
	{
		m_secondary = new aui_SDLSurface(&errcode, m_width, m_height, m_bpp, NULL, FALSE);
		Assert( AUI_NEWOK(m_secondary,errcode) );
		if ( !AUI_NEWOK(m_secondary,errcode) ) return AUI_ERRCODE_MEMALLOCFAILED;
	}

	m_pixelFormat = m_primary->PixelFormat();

	return AUI_ERRCODE_OK;
}

aui_SDLUI::~aui_SDLUI( void )
{
	DestroyMapLayer();
	if (m_SDLTexture) {
		SDL_DestroyTexture(m_SDLTexture);
		m_SDLTexture = NULL;
	}
	if (m_SDLRenderer) {
		SDL_DestroyRenderer(m_SDLRenderer);
		m_SDLRenderer = NULL;
	}
	if (m_SDLWindow) {
		SDL_DestroyWindow(m_SDLWindow);
		m_SDLWindow = NULL;
	}
}

AUI_ERRCODE aui_SDLUI::TearDownMouse(void)
{
	if (m_mouse) {
		m_mouse->GetAnimIndexes(&m_savedMouseAnimFirstIndex, &m_savedMouseAnimLastIndex);
		m_savedMouseAnimCurIndex = m_mouse->GetCurrentCursorIndex();
		m_savedMouseAnimDelay = (sint32)m_mouse->GetAnimDelay();

		if ( m_minimize || m_exclusiveMode )
		{
#if 0
			SetCursorPos( m_mouse->X(), m_mouse->Y() );
#endif
		}

		m_mouse->End();
		delete m_mouse;
		m_mouse = NULL;
	}

	return AUI_ERRCODE_OK;
}

AUI_ERRCODE aui_SDLUI::RestoreMouse(void)
{
	AUI_ERRCODE		auiErr;
	BOOL			exclusive = TRUE;

	aui_SDLMouse *mouse = new aui_SDLMouse( &auiErr, "CivMouse", exclusive );
	Assert(mouse != NULL);
	if ( !mouse ) return AUI_ERRCODE_MEMALLOCFAILED;

	delete m_mouse;
	m_mouse = mouse;

	m_mouse->SetAnimIndexes(m_savedMouseAnimFirstIndex, m_savedMouseAnimLastIndex);
	m_mouse->SetCurrentCursor(m_savedMouseAnimCurIndex);
	m_mouse->SetAnimDelay((uint32)m_savedMouseAnimDelay);

	auiErr = m_mouse->Start();
	Assert(auiErr == AUI_ERRCODE_OK);
	if ( auiErr != AUI_ERRCODE_OK ) return auiErr;

	if ( m_minimize || m_exclusiveMode )
	{
		POINT point;
		//GetCursorPos( &point );
		m_mouse->SetPosition( &point );
	}

	return AUI_ERRCODE_OK;
}

AUI_ERRCODE aui_SDLUI::AltTabOut( void )
{
	Assert(0);

	if(m_keyboard) m_keyboard->Unacquire();
	if ( m_joystick ) m_joystick->Unacquire();

	if (m_mouse)
	{
		if (g_exclusiveMode)
		{
			TearDownMouse();
		}
		else
		{
			main_RestoreTaskBar();

			if (!m_mouse->IsSuspended())
			{
				m_mouse->Suspend(FALSE);
				m_mouse->Unacquire();
			}
		}
	}

	if ( m_minimize || m_exclusiveMode )
	{
		DestroyNativeScreen();
	}

#if 0
	while ( ShowCursor( TRUE ) < 0 )
		;

	if ( m_minimize || m_exclusiveMode )
	{
		while ( !IsIconic( m_hwnd ) )
			::ShowWindow( m_hwnd, SW_MINIMIZE );
	}
#endif
	if (g_civApp)
	{
		g_civApp->SetInBackground(TRUE);
	}

	return AUI_ERRCODE_OK;
}

AUI_ERRCODE aui_SDLUI::AltTabIn( void )
{
	Assert(0);

	if ( !m_primary ) CreateNativeScreen( m_exclusiveMode );

#if 0
	if ( m_minimize || m_exclusiveMode )
		while ( GetForegroundWindow() != m_hwnd )
			::ShowWindow( m_hwnd, SW_RESTORE );
	::ShowWindow(m_hwnd, SW_SHOWMAXIMIZED);

	while ( ShowCursor( FALSE ) >= 0 )
		;

	if (g_exclusiveMode)
	{
		RestoreMouse();
	}
	else
	{
		if ( m_minimize || m_exclusiveMode )
		{
			POINT point;
			GetCursorPos( &point );
			m_mouse->SetPosition( &point );
		}

		if (m_mouse) {
			m_mouse->Acquire();
			m_mouse->Resume();
		}

		main_HideTaskBar();

		RECT clipRect = { 0, 0, m_width, m_height };
		ClipCursor(&clipRect);
	}
#endif
	if ( m_joystick ) m_joystick->Acquire();
	if (m_keyboard) m_keyboard->Acquire();

	if (g_civApp)
	{
		g_civApp->SetInBackground(FALSE);
	}

	return FlushDirtyList();
}

aui_MovieManager* aui_SDLUI::CreateMovieManager( void )
{
	Assert(m_SDLWindow);
	Assert(m_SDLRenderer);
	Assert(m_SDLTexture);
	int windowWidth;
	int windowHeight;
	SDL_GetWindowSize(m_SDLWindow, &windowWidth, &windowHeight);
	return new aui_SDLMovieManager(m_SDLRenderer, m_SDLTexture, windowWidth, windowHeight);
}

AUI_ERRCODE aui_SDLUI::SDLDrawScreen( void )
{
	Assert(m_primary);
	Assert(m_SDLTexture);
	Assert(m_SDLRenderer);

	// An idle game draws nothing most frames; uploading and presenting the
	// screen anyway kept a core and the GPU busy. Still refresh now and then
	// in case something drew past us.
	static const Uint32 k_REFRESH_TICKS = 1000;
	Uint32 now = SDL_GetTicks();
	if (!m_screenChanged && now - m_lastPresent < k_REFRESH_TICKS)
	{
		return AUI_ERRCODE_OK;
	}
	if (m_screenChanged)
		m_lastScreenChange = GetTickCount();
	m_screenChanged = false;
	m_lastPresent   = now;

	if (DrawMapLayer())
	{
		SDL_RenderPresent(m_SDLRenderer);
		return AUI_ERRCODE_OK;
	}

	int errcode;
	errcode= SDL_UpdateTexture(m_SDLTexture, NULL, m_primary->Buffer(), m_primary->Pitch());
#if !defined(WIN32)
	// VS claims std::cerr is not there even so the right file is included
	if (errcode < 0) std::cerr << "SDL error: " << SDL_GetError() << std::endl;
#endif
	errcode= SDL_RenderClear(m_SDLRenderer);
#if !defined(WIN32)
	if (errcode < 0) std::cerr << "SDL error: " << SDL_GetError() << std::endl;
#endif
	errcode= SDL_RenderCopy(m_SDLRenderer, m_SDLTexture, NULL, NULL);
#if !defined(WIN32)
	if (errcode < 0) std::cerr << "SDL error: " << SDL_GetError() << std::endl;
#endif
	SDL_RenderPresent(m_SDLRenderer);

	if (errcode < 0)
		return AUI_ERRCODE_SURFACEFAILURE;
	else
		return AUI_ERRCODE_OK;
}

bool aui_SDLUI::GetOutputScale(sint32 &num, sint32 &den)
{
	num = den = 1;
	int w = 0, h = 0;
	if (m_bpp != 16 || !m_SDLRenderer || SDL_GetRendererOutputSize(m_SDLRenderer, &w, &h) < 0)
		return false;

	// The logical size keeps its aspect ratio, so the tighter side sets the scale
	if ((sint64)w * m_height <= (sint64)h * m_width)
	{
		num = w;
		den = m_width;
	}
	else
	{
		num = h;
		den = m_height;
	}
	return num > 0 && den > 0;
}

void aui_SDLUI::DestroyMapLayer()
{
	if (m_mapMix) ScreenChanged();
	if (m_mapTexture)
	{
		SDL_DestroyTexture(m_mapTexture);
		m_mapTexture = NULL;
	}
	if (m_uiTexture)
	{
		SDL_DestroyTexture(m_uiTexture);
		m_uiTexture = NULL;
	}
	delete [] m_uiColors;
	m_uiColors  = NULL;
	delete [] m_mapMask;
	m_mapMask   = NULL;
	delete [] m_mapShadow;
	m_mapShadow = NULL;
	m_mapMix    = NULL;
	m_mapWindow = NULL;
}

void aui_SDLUI::SetMapLayer(aui_Surface *mix, const RECT *mixRect,
	aui_Window *window, const RECT *viewport, double scale)
{
	if (!mix || !window || m_bpp != 16)
	{
		DestroyMapLayer();
		return;
	}

	sint32 width  = mixRect->right - mixRect->left;
	sint32 height = mixRect->bottom - mixRect->top;
	bool   upload = mix != m_mapMix
	             || memcmp(mixRect, &m_mapMixRect, sizeof(RECT)) != 0;

	if (m_mapTexture && upload)
	{
		int w = 0, h = 0;
		SDL_QueryTexture(m_mapTexture, NULL, NULL, &w, &h);
		if (w != width || h != height)
		{
			SDL_DestroyTexture(m_mapTexture);
			m_mapTexture = NULL;
		}
	}

	if (!m_mapTexture)
	{
		m_mapTexture = SDL_CreateTexture(m_SDLRenderer,
			aui_SDLSurface::TransformSurfacePixelFormatToSDL(m_pixelFormat),
			SDL_TEXTUREACCESS_STREAMING, width, height);
		if (!m_mapTexture)
		{
			std::cerr << "SDL map texture: " << SDL_GetError() << std::endl;
			DestroyMapLayer();
			return;
		}
		// Map pixels land on whole output pixels; super zoom enlarges them blocky
		SDL_SetTextureScaleMode(m_mapTexture, SDL_ScaleModeNearest);

		delete [] m_mapShadow;
		m_mapShadow = new uint16[width * height];
	}

	if (!m_uiTexture)
	{
		m_uiTexture = SDL_CreateTexture(m_SDLRenderer, SDL_PIXELFORMAT_ARGB8888,
			SDL_TEXTUREACCESS_STREAMING, m_width, m_height);
		if (!m_uiTexture)
		{
			std::cerr << "SDL interface texture: " << SDL_GetError() << std::endl;
			DestroyMapLayer();
			return;
		}
		// Clear pixels are all 0 and the rest fully opaque, so the texture is
		// premultiplied: linear scaling then fades edges into the map instead
		// of into black. Not every renderer has custom blend modes.
		SDL_BlendMode premultiplied = SDL_ComposeCustomBlendMode(
			SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
			SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
		if (SDL_SetTextureBlendMode(m_uiTexture, premultiplied) < 0)
			SDL_SetTextureBlendMode(m_uiTexture, SDL_BLENDMODE_BLEND);

		SDL_PixelFormat *format = SDL_AllocFormat(
			aui_SDLSurface::TransformSurfacePixelFormatToSDL(m_pixelFormat));
		m_uiColors = new uint32[0x10000];
		for (uint32 i = 0; i < 0x10000; ++i)
		{
			Uint8 r, g, b;
			SDL_GetRGB(i, format, &r, &g, &b);
			m_uiColors[i] = 0xFF000000 | (r << 16) | (g << 8) | b;
		}
		SDL_FreeFormat(format);

		// Filled in as windows reach the screen; the map window is redrawn
		// in full when the layer starts
		m_mapMask = new uint8[m_width * m_height];
		memset(m_mapMask, 0, m_width * m_height);
	}

	m_mapMix      = mix;
	m_mapMixRect  = *mixRect;
	m_mapWindow   = window;
	m_mapViewport = *viewport;
	m_mapScale    = scale;

	if (upload)
	{
		m_mapShadowStale = true;
		UpdateMapLayer(mixRect);
	}
}

void aui_SDLUI::UpdateMapLayer(const RECT *rect)
{
	if (!m_mapMix || !m_mapTexture || !m_mapShadow) return;

	RECT r = *rect;
	if (!Rectangle_Clip(&r, &m_mapMixRect) || r.right <= r.left) return;

	uint8 *buffer = NULL;
	if (m_mapMix->Lock(NULL, (LPVOID *)&buffer, 0) != AUI_ERRCODE_OK) return;

	SDL_Rect dest = { r.left - m_mapMixRect.left, r.top - m_mapMixRect.top,
	                  r.right - r.left, r.bottom - r.top };
	sint32   pitch = m_mapMix->Pitch();

	// The map's sprites are repainted every frame whether they changed or
	// not: upload (and present) only if a pixel did
	const sint32 shadowWidth = m_mapMixRect.right - m_mapMixRect.left;
	const size_t bytes       = dest.w * sizeof(uint16);
	bool         changed     = m_mapShadowStale;
	for (sint32 y = 0; y < dest.h; ++y)
	{
		const uint8 *src    = buffer + (r.top + y) * pitch + r.left * sizeof(uint16);
		uint16      *shadow = m_mapShadow + (dest.y + y) * shadowWidth + dest.x;
		if (m_mapShadowStale || memcmp(shadow, src, bytes) != 0)
		{
			memcpy(shadow, src, bytes);
			changed = true;
		}
	}
	if (!changed)
	{
		m_mapMix->Unlock(buffer);
		return;
	}
	if (memcmp(&r, &m_mapMixRect, sizeof(RECT)) == 0)
		m_mapShadowStale = false;
	ScreenChanged();

	if (SDL_UpdateTexture(m_mapTexture, &dest,
		buffer + r.top * pitch + r.left * sizeof(uint16), pitch) < 0)
	{
		std::cerr << "SDL error: " << SDL_GetError() << std::endl;
	}

	m_mapMix->Unlock(buffer);
}

void aui_SDLUI::MapLayerBlt(sint32 x, sint32 y, aui_Window *window, const RECT *windowRect)
{
	if (!m_mapMask) return;

	RECT screen = *windowRect;
	OffsetRect(&screen, x - windowRect->left, y - windowRect->top);
	RECT bounds = { 0, 0, m_width, m_height };
	if (!Rectangle_Clip(&screen, &bounds) || screen.right <= screen.left) return;

	const sint32 width = screen.right - screen.left;
	const sint32 dx    = windowRect->left - x;
	const sint32 dy    = windowRect->top - y;
	uint8 *      bits  = new uint8[width];
	for (sint32 row = screen.top; row < screen.bottom; ++row)
	{
		if (window && window == m_mapWindow)
		{
			memset(bits, 1, width);
		}
		else if (window && !window->IsOpaque() && window->GetStencil())
		{
			// Only where the window shows what's under it (aui_UI::ClipAndConsolidate)
			aui_StencilRowMask(window->GetStencil(), row + dy,
				screen.left + dx, screen.right + dx, bits);
		}
		else
		{
			memset(bits, 0, width);
		}

		// Which layer shows can change where the pixels don't
		uint8 *mask = m_mapMask + row * m_width + screen.left;
		if (memcmp(mask, bits, width) != 0)
		{
			memcpy(mask, bits, width);
			ScreenChanged();
		}
	}
	delete [] bits;
}

/// Draw the map layer, and the interface over it except where the screen
/// shows the map window's own (lower resolution) copy of the map: pixels last
/// copied from it, directly or through another window's holes, and not drawn
/// over since (the cursor)
bool aui_SDLUI::DrawMapLayer()
{
	if (!m_mapMix || !m_mapTexture || !m_uiTexture || !m_mapWindow
	 || m_mapWindow->IsHidden() || GetWindow(m_mapWindow->Id()) != m_mapWindow)
	{
		return false;
	}

	aui_Surface *window = m_mapWindow->TheSurface();
	if (!window || window->BitsPerPixel() != 16 || m_primary->BitsPerPixel() != 16)
		return false;

	// The viewport on the screen
	sint32 windowX = m_mapWindow->X();
	sint32 windowY = m_mapWindow->Y();
	RECT   screen  = m_mapViewport;
	OffsetRect(&screen, windowX, windowY);
	RECT   bounds  = { 0, 0, m_width, m_height };
	RECT   windowBounds = { windowX, windowY, windowX + window->Width(), windowY + window->Height() };
	RECT   compare = screen;
	if (!Rectangle_Clip(&compare, &bounds) || !Rectangle_Clip(&compare, &windowBounds))
		SetRect(&compare, 0, 0, 0, 0);

	uint8 *windowBuf = NULL;
	if (window->Lock(NULL, (LPVOID *)&windowBuf, 0) != AUI_ERRCODE_OK)
		return false;

	void *pixels = NULL;
	int   pitch  = 0;
	if (SDL_LockTexture(m_uiTexture, NULL, &pixels, &pitch) < 0)
	{
		window->Unlock(windowBuf);
		return false;
	}

	const uint8  *primaryBuf   = m_primary->Buffer();
	const sint32  primaryPitch = m_primary->Pitch();
	const sint32  windowPitch  = window->Pitch();
	for (sint32 y = 0; y < m_height; ++y)
	{
		const uint16 *p = (const uint16 *)(primaryBuf + y * primaryPitch);
		uint32       *d = (uint32 *)((uint8 *)pixels + y * pitch);
		sint32        x = 0;
		if (y >= compare.top && y < compare.bottom)
		{
			const uint16 *w = (const uint16 *)(windowBuf + (y - windowY) * windowPitch) - windowX;
			const uint8  *m = m_mapMask + y * m_width;
			for (; x < compare.left; ++x)
				d[x] = m_uiColors[p[x]];
			for (; x < compare.right; ++x)
				d[x] = (m[x] && p[x] == w[x]) ? 0 : m_uiColors[p[x]];
		}
		for (; x < m_width; ++x)
			d[x] = m_uiColors[p[x]];
	}

	SDL_UnlockTexture(m_uiTexture);
	window->Unlock(windowBuf);

	sint32   mixWidth  = m_mapMixRect.right - m_mapMixRect.left;
	sint32   mixHeight = m_mapMixRect.bottom - m_mapMixRect.top;
	SDL_FRect mapRect  = { (float)screen.left, (float)screen.top,
	                       (float)(mixWidth / m_mapScale), (float)(mixHeight / m_mapScale) };

	SDL_RenderClear(m_SDLRenderer);
	SDL_RenderCopyF(m_SDLRenderer, m_mapTexture, NULL, &mapRect);
	SDL_RenderCopy(m_SDLRenderer, m_uiTexture, NULL, NULL);
	return true;
}

AUI_ERRCODE aui_SDLUI::ChangeSize(sint32 width, sint32 height)
{
	AUI_ERRCODE errcode = AUI_ERRCODE_OK;

	// Something needs to be done here so that the screen content is not shrunken.
/*
	m_width  = width;
	m_height = height;

	g_ScreenWidth  = width;
	g_ScreenHeight = height;

	delete m_primary;
	m_primary = new aui_SDLSurface(&errcode, m_width, m_height, m_bpp, NULL, TRUE);
	Assert(AUI_NEWOK(m_primary, errcode));
	if (!AUI_NEWOK(m_primary, errcode)) return AUI_ERRCODE_MEMALLOCFAILED;

	delete m_secondary;
	m_secondary = new aui_SDLSurface(&errcode, m_width, m_height, m_bpp, NULL, FALSE);
		Assert(AUI_NEWOK(m_secondary, errcode));
		if (!AUI_NEWOK(m_secondary, errcode)) return AUI_ERRCODE_MEMALLOCFAILED;

	m_pixelFormat = m_primary->PixelFormat();
*/
	Invalidate(NULL);
	ScreenChanged();

	return AUI_ERRCODE_OK;
}
#endif  // __AUI_USE_SDL__

