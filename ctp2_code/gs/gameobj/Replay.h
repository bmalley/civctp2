//----------------------------------------------------------------------------
//
// Project      : Call To Power 2
// File type    : C++ header
// Description  : Map timelapse recording
//
//----------------------------------------------------------------------------

#ifdef HAVE_PRAGMA_ONCE
#pragma once
#endif

#ifndef __REPLAY_H__
#define __REPLAY_H__

// Once a round, append who owns each tile, and the cities, to REPLAY-<leader>
// beside the autosaves, for a timelapse of the map. Profile option
// RecordReplay. Read by ctp/tools/replay.py; the format is described there.
void replay_RecordRound(sint32 round);

#include <map>
#include <string>
#include <vector>

struct ReplayPlayer
{
	uint8           r, g, b;
	std::string     name;
};

struct ReplayCity
{
	uint16          x, y;
	uint8           owner;
	uint8           size;
};

struct ReplayRound
{
	std::string             date;
	std::vector<uint8>      owners;     // w * h, player index or 255 for nobody
	std::vector<ReplayCity> cities;
};

struct ReplayGame
{
	sint32                          w, h;
	bool                            xwrap, ywrap;
	std::vector<ReplayPlayer>       players;
	std::vector<uint8>              land;   // w * h, 1 land, 0 water
	std::map<sint32, ReplayRound>   rounds;
};

// Read this leader's replay file and return the game recorded on the current
// map (the last one, if none matches). False if there is no usable record.
bool replay_LoadCurrent(ReplayGame &game);

#endif
