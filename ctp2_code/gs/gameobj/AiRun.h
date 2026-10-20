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
//   CTP2_AIRUN_DRAW=1    draw as a watched game does; otherwise a test run
//                        draws nothing and doesn't wait on the clock
bool airun_IsActive();
// A test run that draws nothing and runs frames and the director flat out (all
// screen work: the game plays the same, see airun_Fast in AiRun.cpp)
bool airun_Fast();
bool airun_GetSeed(uint32 &seed);
// CTP2_AIRUN_STARTS=N: player i takes the map's start (i - 1 + N) mod (number of
// starts) instead of start i - 1. The map generator places start 0 best and start 1
// close to it, so player 1 always has the best land and player 2 beside it; rotating
// moves that pairing to other players. 0 when unset.
sint32 airun_StartRotation();
void airun_RecordRound(sint32 round);
void airun_RecordTurn(sint32 player, sint32 round);
// CTP2_AIX=letters: AI experiments switched on for a test run (see each caller)
bool airun_Experiment(char which);

struct CombatOdds;
// CTP2_AIRUN_ODDS=N: before each battle, simulate it N times (CTP2Combat::Simulate)
sint32 airun_OddsTrials();
// a "battle" line: what the simulation expected, and how the fight came out
void airun_RecordBattle(sint32 attacker, sint32 defender, sint32 attackers, sint32 defenders,
                        const CombatOdds &odds, sint32 attackersLeft, sint32 defendersLeft);

#endif
