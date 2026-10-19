#ifdef HAVE_PRAGMA_ONCE
#pragma once
#endif

#ifndef AIRUN_H__
#define AIRUN_H__

// AI test runs (ctp/tests/airun): an all-AI game started with `noshell` in
// observer mode, steered by environment variables so a script can run many:
//   CTP2_SEED=N          fixed random seed for a new game
//   CTP2_AIRUN=FILE      append a JSON line per round and per event to FILE,
//                        and end each observed turn without delay
//   CTP2_AIRUN_ROUNDS=N  exit once round N has been recorded
bool airun_IsActive();
bool airun_GetSeed(uint32 &seed);
void airun_RecordRound(sint32 round);
void airun_RecordTurn(sint32 player, sint32 round);

#endif
