//----------------------------------------------------------------------------
//
// Project      : Call To Power 2
// File type    : C++ source
// Description  :
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
//
//----------------------------------------------------------------------------
//
// Modifications from the original Activision code:
//
// - Repaired memory leak.
// - Deep ocean is drawn as shallow water until Oil Refining (DeepOcean), as
//   in CtP1: GetDisplayTileNum.
//
//----------------------------------------------------------------------------
//
/// \file   TileInfo.cpp
/// \brief  Tile information (definitions)

#include "c3.h"
#include "TileInfo.h"

#include <algorithm>        // std::fill
#include "civarchive.h"
#include "GoodActor.h"
#include "profileDB.h"      // g_theProfileDB

#include "player.h"         // g_player
#include "aui_control.h"
#include "scenarioeditor.h" // ScenarioEditor::IsShown
#include "SelItem.h"        // g_selected_item
#include "TerrainRecord.h"
#include "terrainutil.h"

extern sint32			g_fog_toggle;
extern sint32			g_god;

TileInfo::TileInfo()
:
    m_riverPiece    (-1),
    m_megaInfo      (0),
    m_terrainType   (0),
    m_transform     (static_cast<uint8>(rand() % 256)),
    m_tileNum       (0),
    m_goodActor     (NULL)
{
    std::fill(m_transitions, m_transitions + k_NUM_TRANSITIONS, 0);
}

/// @todo Replace with proper copy constructor. This will crash when copy is NULL.
TileInfo::TileInfo(TileInfo *copy)
{
	*this = *copy;

	if (copy->m_goodActor)
    {
		m_goodActor = new GoodActor(*copy->m_goodActor);
	}
    // else: already set in *this = *copy;
}

TileInfo::TileInfo(CivArchive &archive)
:
    m_riverPiece    (-1),
    m_megaInfo      (0),
    m_terrainType   (0),
    m_transform     (0),
    m_tileNum       (0),
    m_goodActor     (NULL)
{
	Serialize(archive);
}

TileInfo::~TileInfo()
{
	delete m_goodActor;
}

TILEINDEX TileInfo::GetTileNum(void) const
{
	return m_tileNum;
}

//----------------------------------------------------------------------------
//
// Name       : TileInfo::IsHiddenDeepOcean
//
// Description: Whether terrain of this type is drawn as shallow water for
//              the player at the screen.
//
// Parameters : terrainType : index into g_theTerrainDB
//
// Globals    : g_fog_toggle, g_god, g_selected_item, g_player
//              (and never in the scenario editor)
//
// Returns    : bool : true while hidden
//
// Remark(s)  : Until the visible player has an advance with DeepOcean (Oil
//              Refining) or the global radar, sea that shallow-water ships
//              can't enter (deep, shelf, trench, rift, volcano) is drawn as
//              shallow water, as CtP1 did. Activision's CtP2 had the same
//              check in GetTileNum behind an early return. It's kept out of
//              GetTileNum because PostProcessMap stores that value back.
//
//----------------------------------------------------------------------------
bool TileInfo::IsHiddenDeepOcean(sint32 terrainType)
{
	if (g_fog_toggle || g_god || !g_selected_item || ScenarioEditor::IsShown())
		return false;

	sint32 const    visible = g_selected_item->GetVisiblePlayer();
	if (visible < 0 || visible >= k_MAX_PLAYERS)
		return false;

	Player const *  player  = g_player[visible];
	if (!player || player->m_hasGlobalRadar || player->GetDeepOceanVisible())
		return false;

	TerrainRecord const * rec = g_theTerrainDB->Get(terrainType);
	return rec && rec->GetMovementTypeSea() && !rec->GetMovementTypeShallowWater();
}

// The terrain to draw for terrainType: shallow water while hidden, else itself.
// Use it wherever drawing picks a tileset (transitions) or a minimap colour.
sint32 TileInfo::DisplayTerrainType(sint32 terrainType)
{
	return IsHiddenDeepOcean(terrainType) ? static_cast<sint32>(TERRAIN_WATER_SHALLOW) : terrainType;
}

uint8 TileInfo::GetDisplayTerrainType(void) const
{
	return static_cast<uint8>(DisplayTerrainType(m_terrainType));
}

// The base tile to draw: shallow water's while hidden, else the tile's.
TILEINDEX TileInfo::GetDisplayTileNum(void) const
{
	return IsHiddenDeepOcean(m_terrainType)
	    ? static_cast<TILEINDEX>(g_theTerrainDB->Get(TERRAIN_WATER_SHALLOW)->GetTilesetIndex())
	    : m_tileNum;
}

void TileInfo::SetGoodActor(sint32 index, MapPoint const & pos)
{
    delete m_goodActor;
	m_goodActor = new GoodActor(index, pos);

	if (g_theProfileDB->IsGoodAnim())
	{
		m_goodActor->Reload(LOADTYPE_FULL);
	}
}

void TileInfo::DeleteGoodActor(void)
{
	delete m_goodActor;
	m_goodActor = NULL;
}

void TileInfo::Serialize(CivArchive &archive)
{
	sint32 hasGoodActor;

	if(archive.IsStoring()) {
		DPRINTF(k_DBG_GRAPHICS, ("%d,%d,%d,%d, %d,%d,%d,%d\n",
								 m_riverPiece, m_megaInfo,
								 m_terrainType, m_tileNum,
								 m_transitions[0], m_transitions[1],
								 m_transitions[2], m_transitions[3]));
		archive << m_riverPiece;
		archive << m_megaInfo;
		archive << m_terrainType;
		archive << m_tileNum;
		archive << m_transform;
		for (size_t i = 0; i < k_NUM_TRANSITIONS; i++)
        {
			archive << m_transitions[i];
		}
		hasGoodActor = (m_goodActor != NULL);
		archive << hasGoodActor;
		if(m_goodActor)
			m_goodActor->Serialize(archive);
	} else {
		archive >> m_riverPiece;
		archive >> m_megaInfo;
		archive >> m_terrainType;
		archive >> m_tileNum;
		archive >> m_transform;
		for (size_t i = 0; i < k_NUM_TRANSITIONS; i++)
        {
			archive >> m_transitions[i];
		}
		archive >> hasGoodActor;

        delete m_goodActor;
        m_goodActor = (hasGoodActor) ? new GoodActor(archive) : NULL;
	}
}
