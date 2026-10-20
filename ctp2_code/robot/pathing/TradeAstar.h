//----------------------------------------------------------------------------
//
// Project      : Call To Power 2
// File type    : C++ header
// Description  : A-star Trade route pathing algorithm
// Id           : $Id$
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
// - None
//
//----------------------------------------------------------------------------
//
// Modifications from the original Activision code:
//
// - None
//
//----------------------------------------------------------------------------

#pragma once
#ifndef __TRADE_ASTAR_H__
#define __TRADE_ASTAR_H__ 1

#include "gstypes.h"
#include "Astar.h"

#include <vector>

class TradeAstar : public Astar
{
	PLAYER_INDEX m_owner;
	MapPoint m_start;

	// Entry costs from SetCostGrid, for m_gridOwner's searches
	std::vector<float> const * m_costGrid  = NULL;
	PLAYER_INDEX               m_gridOwner = PLAYER_INDEX_INVALID;
	// The tiles the last search with a cost grid looked at: a bit per grid entry
	std::vector<uint64>        m_searched;

	bool EntryCost(const MapPoint &prev, const MapPoint &pos,
	                        float &cost, bool &is_zoc,
	                        ASTAR_ENTRY_TYPE &entry);

	sint32 GetMaxDir(MapPoint &pos) const;

	PLAYER_INDEX GetOwner() const { return m_owner; }

	// The city at m_start, if any - lets diagnostics filter by the city
	// this trade route search started from, the same way UnitAstar
	// filters by army.
	sint32 GetArmyId() const;

public:

	bool FindPath(const PLAYER_INDEX owner, const MapPoint &start,
	              const MapPoint &dest, Path &a_path, float &total_cost,
	              const bool isunit);

	// Entry costs worked out beforehand for owner's searches, while nothing on
	// the map can change: [x * world height + y], the ceiled freight cost of an
	// explored tile, < 0 for an unexplored one. NULL to work them out per tile.
	void SetCostGrid(const PLAYER_INDEX owner, std::vector<float> const * grid)
	{
		m_gridOwner = owner;
		m_costGrid  = grid;
	}

	// The tiles the last search with a cost grid looked at, a bit per grid
	// entry: it read no other costs, so it would find the same path again while
	// these stay the same.
	std::vector<uint64> const & GetSearchedTiles() const { return m_searched; }
};

#endif
