#ifdef HAVE_PRAGMA_ONCE
#pragma once
#endif

#ifndef PATH_CACHE_EPOCH_H__
#define PATH_CACHE_EPOCH_H__

// RobotAstar2 keeps the result of each path search for as long as nothing a
// search can read has changed (robotastar2.cpp). Whatever changes the map
// (units on a tile, owners, cities, terrain, move costs), what a player has
// seen or explored, or a unit's hit points, visibility or type calls this,
// and every kept result is dropped.
extern uint32 g_pathCacheEpoch;

inline void pathcache_WorldChanged()
{
	++g_pathCacheEpoch;
}

#endif
