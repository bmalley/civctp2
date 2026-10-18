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

#endif
