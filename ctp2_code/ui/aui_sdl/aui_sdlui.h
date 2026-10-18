//----------------------------------------------------------------------------
//
// Project      : Call To Power 2
// File type    : C++ header
// Description  : User interface SDL component
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
// _MSC_VER
// - Compiler version (for the Microsoft C++ compiler only)
//
//----------------------------------------------------------------------------
//
// Modifications from the original Activision code:
//
// - Initialised pointer in default constructor to prevent destructor crash.
//
//----------------------------------------------------------------------------
#ifndef __AUI_SDLUI_H__
#define __AUI_SDLUI_H__

#ifdef __AUI_USE_SDL__

#include "aui_ui.h"
#include "aui_sdl.h"

class aui_SDLUI : public aui_UI, public aui_SDL
{
public:
	aui_SDLUI(
		AUI_ERRCODE *retval,
		HINSTANCE hinst,
		HWND hwnd,
		sint32 width,
		sint32 height,
		sint32 bpp,
		const MBCHAR *ldlFilename = NULL,
		BOOL useExclusiveMode = FALSE );
	virtual ~aui_SDLUI();

protected:
	aui_SDLUI()
	:   aui_UI              (),
	    aui_SDL             (),
	    m_mapMix            (NULL),
	    m_mapWindow         (NULL),
	    m_mapTexture        (NULL),
	    m_uiTexture         (NULL),
	    m_uiColors          (NULL),
	    m_mapMask           (NULL),
	    m_mapShadow         (NULL),
	    m_mapShadowStale    (true),
	    m_lastPresent       (0)
	{ ; };

	AUI_ERRCODE InitCommon( void );

public:

	virtual AUI_ERRCODE AltTabOut( void );
	virtual AUI_ERRCODE AltTabIn( void );
	AUI_ERRCODE CreateNativeScreen( BOOL useExclusiveMode );
	AUI_ERRCODE DestroyNativeScreen(void);
	AUI_ERRCODE TearDownMouse(void);
	AUI_ERRCODE RestoreMouse(void);

	void SetWidth(sint32 width) { m_width = width; }
	void SetHeight(sint32 height) { m_height = height; }
	AUI_ERRCODE ChangeSize(sint32 width, sint32 height);

	aui_MovieManager* CreateMovieManager( void );

	virtual AUI_ERRCODE SDLDrawScreen( void );

	virtual bool GetOutputScale(sint32 &num, sint32 &den);
	virtual void SetMapLayer(aui_Surface *mix, const RECT *mixRect,
		aui_Window *window, const RECT *viewport, double scale);
	virtual void UpdateMapLayer(const RECT *rect);
	virtual void MapLayerBlt(sint32 x, sint32 y, aui_Window *window, const RECT *windowRect);

protected:
	void DestroyMapLayer();
	bool DrawMapLayer();

	SDL_Window *m_SDLWindow;
	SDL_Renderer *m_SDLRenderer;
	SDL_Texture *m_SDLTexture;

	// Map layer (see aui_UI::SetMapLayer)
	aui_Surface *m_mapMix;
	RECT         m_mapMixRect;
	aui_Window  *m_mapWindow;
	RECT         m_mapViewport;
	double       m_mapScale;
	SDL_Texture *m_mapTexture;      // m_mapMixRect of m_mapMix
	SDL_Texture *m_uiTexture;       // ARGB primary, clear where the map shows
	uint32      *m_uiColors;        // primary pixel -> ARGB
	uint8       *m_mapMask;         // per screen pixel: copied from the map window
	uint16      *m_mapShadow;       // what m_mapTexture holds
	bool         m_mapShadowStale;  // until all of it is uploaded

	Uint32       m_lastPresent;     // SDL ticks
};

typedef aui_SDLUI aui_NativeUI;

#endif

#endif
