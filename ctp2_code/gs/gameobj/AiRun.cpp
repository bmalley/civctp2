//----------------------------------------------------------------------------
//
// Project      : Call To Power 2
// File type    : C++ source
// Description  : AI test runs: per-round statistics as JSON lines
//
//----------------------------------------------------------------------------

#include "c3.h"
#include "AiRun.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>             // _exit
#include <map>
#include <vector>
#include <string>
#include <string.h>
#include "Advances.h"
#include "AgreementMatrix.h"
#include "citydata.h"
#include "Civilisation.h"
#include "Diplomat.h"
#include "Events.h"               // STDEHANDLER
#include "GameEventUser.h"
#include "GameEventManager.h"   // g_gevManager
#include "player.h"             // g_player
#include "RandGen.h"            // g_rand
#include "Score.h"
#include "StrategyRecord.h"
#include "Unit.h"
#include "UnitData.h"
#include "UnitDynArr.h"
#include "UnitRecord.h"
#include "World.h"              // g_theWorld
#include "CTP2Combat.h"         // CombatOdds
#include "TurnCnt.h"            // g_turn

extern MapPoint g_player_start_list[k_MAX_PLAYERS];   // gameinit.cpp
extern sint32 g_player_start_score[k_MAX_PLAYERS];

namespace
{
	FILE *  s_file          = NULL;
	bool    s_checked       = false;
	sint32  s_lastRound     = -1;
	sint32  s_exitRound     = -1;
	bool    s_hooked        = false;

	// Counts since the last recorded round
	sint32  s_battles[k_MAX_PLAYERS];   // battles fought as attacker
	sint32  s_kills[k_MAX_PLAYERS];     // enemy units killed
	sint32  s_losses[k_MAX_PLAYERS];    // own units killed by an enemy

	// The strategic states each player used in its last turn, kept as its next turn
	// begins: Diplomat::BeginTurn clears them before the round is recorded
	std::string s_strategies[k_MAX_PLAYERS];

	// Cities captured since the last recorded round, to report whether the new
	// owner still holds them and has units in them at the next round
	struct Captured { Unit city; sint32 owner; };
	std::vector<Captured> s_captured;

	FILE *File()
	{
		if (!s_checked)
		{
			s_checked = true;
			const char *path = getenv("CTP2_AIRUN");
			if (path && *path)
			{
				s_file = fopen(path, "a");
			}
			const char *rounds = getenv("CTP2_AIRUN_ROUNDS");
			if (rounds && *rounds)
			{
				s_exitRound = atoi(rounds);
			}
		}
		return s_file;
	}

	bool Valid(sint32 p)
	{
		return p >= 0 && p < k_MAX_PLAYERS && g_player[p];
	}

	// Names are ours or from the databases: no quoting needed but for safety
	std::string Json(const char *s)
	{
		std::string out = "\"";
		for (; s && *s; ++s)
		{
			if (*s == '"' || *s == '\\') out += '\\';
			if (static_cast<unsigned char>(*s) >= ' ') out += *s;
		}
		return out + "\"";
	}

	std::string CivName(sint32 p)
	{
		MBCHAR name[k_MAX_NAME_LEN] = "";
		if (Valid(p) && g_player[p]->GetCivilisation())
			g_player[p]->GetCivilisation()->GetSingularCivName(name);
		return name;
	}

	STDEHANDLER(AiRun_BattleAftermath)
	{
		sint32 attacker, defender;
		if (args->GetPlayer(0, attacker) && args->GetPlayer(1, defender)
		 && attacker >= 0 && attacker < k_MAX_PLAYERS)
		{
			++s_battles[attacker];
		}
		return GEV_HD_Continue;
	}

	STDEHANDLER(AiRun_BeginTurn)
	{
		sint32 p;
		if (!args->GetPlayer(0, p) || !Valid(p)) return GEV_HD_Continue;

		Diplomat &diplomat = Diplomat::GetDiplomat(p);
		std::string names;
		for (Diplomat::AiStateList::const_iterator it = diplomat.GetStrategicStates().begin();
		     it != diplomat.GetStrategicStates().end(); ++it)
		{
			if (it->dbIndex < 0 || it->dbIndex >= g_theStrategyDB->NumRecords()) continue;
			const char *name = g_theStrategyDB->Get(it->dbIndex)->GetIDText();
			if (!strncmp(name, "STRATEGY_", 9)) name += 9;
			names += (names.empty() ? "" : ",") + Json(name);
		}
		s_strategies[p] = names;
		return GEV_HD_Continue;
	}

	STDEHANDLER(AiRun_KillUnit)
	{
		Unit unit;
		sint32 cause, killer;
		if (args->GetUnit(0, unit) && unit.IsValid() && args->GetInt(0, cause)
		 && args->GetPlayer(0, killer))
		{
			sint32 const owner = unit.GetOwner();
			if (killer >= 0 && killer < k_MAX_PLAYERS && killer != owner)
			{
				++s_kills[killer];
				if (owner >= 0 && owner < k_MAX_PLAYERS) ++s_losses[owner];
			}
		}
		return GEV_HD_Continue;
	}

	STDEHANDLER(AiRun_CaptureCity)
	{
		Unit city;
		sint32 newOwner, cause;
		if (s_file && args->GetCity(0, city) && city.IsValid() && args->GetPlayer(0, newOwner)
		 && args->GetInt(0, cause))
		{
			fprintf(s_file, "{\"t\":\"capture\",\"r\":%d,\"from\":%d,\"to\":%d,\"cause\":%d,"
			        "\"city\":%s,\"size\":%d}\n",
			        s_lastRound + 1, city.GetOwner(), newOwner, cause,
			        Json(city.GetName()).c_str(), city.PopCount());
			Captured c = { city, newOwner };
			s_captured.push_back(c);
		}
		return GEV_HD_Continue;
	}

	STDEHANDLER(AiRun_KillCity)
	{
		Unit city;
		sint32 cause, killer;
		// No killer when the last pop dies (PLAYER_UNASSIGNED), e.g. a size-1 city captured
		if (s_file && args->GetCity(0, city) && city.IsValid() && args->GetInt(0, cause))
		{
			if (!args->GetPlayer(0, killer)) killer = -1;
			fprintf(s_file, "{\"t\":\"citykilled\",\"r\":%d,\"owner\":%d,\"by\":%d,\"cause\":%d,"
			        "\"city\":%s}\n",
			        s_lastRound + 1, city.GetOwner(), killer, cause, Json(city.GetName()).c_str());
		}
		return GEV_HD_Continue;
	}

	void Hook()
	{
		if (s_hooked || !g_gevManager) return;
		s_hooked = true;
		g_gevManager->AddCallback(GEV_BeginTurn,       GEV_PRI_Pre, &s_AiRun_BeginTurn);
		g_gevManager->AddCallback(GEV_BattleAftermath, GEV_PRI_Pre, &s_AiRun_BattleAftermath);
		g_gevManager->AddCallback(GEV_KillUnit,        GEV_PRI_Pre, &s_AiRun_KillUnit);
		g_gevManager->AddCallback(GEV_CaptureCity,     GEV_PRI_Pre, &s_AiRun_CaptureCity);
		g_gevManager->AddCallback(GEV_KillCity,        GEV_PRI_Pre, &s_AiRun_KillCity);
	}

	void PutPlayer(FILE *f, sint32 p, sint32 round)
	{
		Player *player = g_player[p];

		sint32 pop = 0, empty = 0;
		for (sint32 c = 0; c < player->m_all_cities->Num(); ++c)
		{
			Unit city = player->m_all_cities->Access(c);
			pop += city.PopCount();
			if (g_theWorld->GetCell(city.RetPos())->GetNumUnits() == 0) ++empty;
		}

		std::string wars;
		for (sint32 q = 0; q < k_MAX_PLAYERS; ++q)
		{
			if (q == p || !g_player[q]) continue;
			if (AgreementMatrix::s_agreements.HasAgreement(p, q, PROPOSAL_TREATY_DECLARE_WAR))
			{
				char buf[16];
				snprintf(buf, sizeof buf, "%s%d", wars.empty() ? "" : ",", q);
				wars += buf;
			}
		}

		Diplomat &diplomat = Diplomat::GetDiplomat(p);
		std::string const &strategies = s_strategies[p];

		fprintf(f, "{\"p\":%d,\"civ\":%s,\"robot\":%d,\"dead\":%d,\"pers\":%s,\"strat\":[%s],"
		        "\"cities\":%d,\"pop\":%d,\"empty\":%d,\"units\":%d,\"mil\":%d,\"score\":%d,"
		        "\"gold\":%d,\"techs\":%d,\"wars\":[%s],\"battles\":%d,\"kills\":%d,\"losses\":%d",
		        p, Json(CivName(p).c_str()).c_str(), player->IsRobot() ? 1 : 0,
		        player->IsDead() ? 1 : 0,
		        Json(diplomat.GetPersonalityName().c_str()).c_str(),
		        strategies.c_str(),
		        player->m_all_cities->Num(), pop, empty, player->m_all_units->Num(),
		        player->GetMilitaryStrength(),
		        player->m_score ? player->m_score->GetTotalScore() : 0,
		        player->GetGold(), player->m_advances ? player->m_advances->GetDiscovered() : 0, wars.c_str(), s_battles[p], s_kills[p], s_losses[p]);

		// Unit types every 10 rounds
		if (round % 10 == 0)
		{
			std::map<std::string, sint32> types;
			for (sint32 u = 0; u < player->m_all_units->Num(); ++u)
			{
				++types[player->m_all_units->Access(u).GetDBRec()->GetIDText()];
			}
			fprintf(f, ",\"types\":{");
			for (std::map<std::string, sint32>::const_iterator it = types.begin(); it != types.end(); ++it)
			{
				fprintf(f, "%s%s:%d", it == types.begin() ? "" : ",", Json(it->first.c_str()).c_str(),
				        it->second);
			}
			fprintf(f, "}");
		}
		fprintf(f, "}");
	}
}

bool airun_IsActive()
{
	return File() != NULL;
}

// The screen in a test run is an Xvfb nobody looks at. Unless CTP2_AIRUN_DRAW is
// set: the main loop doesn't hold frames to 60 a second, the director doesn't wait
// 75 ms between steps (each player's turn waited for one: half a second a round),
// and neither the map nor the windows are drawn. Same game, seed for seed: the
// round records of 100-round games match the drawn ones.
bool airun_Fast()
{
	static const bool fast = getenv("CTP2_AIRUN") && !getenv("CTP2_AIRUN_DRAW");
	return fast;
}

bool airun_Experiment(char which)
{
	static const char *letters = getenv("CTP2_AIX");
	return letters && strchr(letters, which) != NULL;
}

sint32 airun_OddsTrials()
{
	static const sint32 trials = File() && getenv("CTP2_AIRUN_ODDS") ? atoi(getenv("CTP2_AIRUN_ODDS")) : 0;
	return trials;
}

void airun_RecordBattle(sint32 attacker, sint32 defender, sint32 attackers, sint32 defenders,
                        const CombatOdds &odds, sint32 attackersLeft, sint32 defendersLeft)
{
	FILE *f = File();
	if (!f || odds.trials <= 0) return;
	const char *result = defendersLeft <= 0 && attackersLeft > 0 ? "att"
	                   : attackersLeft <= 0 && defendersLeft > 0 ? "def" : "draw";
	fprintf(f, "{\"t\":\"battle\",\"r\":%d,\"att\":%d,\"def\":%d,\"na\":%d,\"nd\":%d,"
	           "\"trials\":%d,\"p_att\":%.3f,\"p_def\":%.3f,\"left_att\":%.2f,\"left_def\":%.2f,"
	           "\"res\":\"%s\",\"real_left_att\":%d,\"real_left_def\":%d}\n",
	        g_turn ? g_turn->GetRound() : -1, attacker, defender, attackers, defenders,
	        odds.trials, odds.AttackerWinChance(), odds.DefenderWinChance(),
	        odds.attackerLeft / odds.trials, odds.defenderLeft / odds.trials,
	        result, attackersLeft, defendersLeft);
}

bool airun_GetSeed(uint32 &seed)
{
	const char *s = getenv("CTP2_SEED");
	if (!s || !*s) return false;
	seed = static_cast<uint32>(strtoul(s, NULL, 10));
	return true;
}

sint32 airun_StartRotation()
{
	const char *s = getenv("CTP2_AIRUN_STARTS");
	return s && *s && getenv("CTP2_AIRUN") ? atoi(s) : 0;
}

// CTP2_AIRUN_TURNS=1: also a line at each player's turn with the synced random
// generator's call count, to find where two runs of one seed part
void airun_RecordTurn(sint32 player, sint32 round)
{
	static const bool on = getenv("CTP2_AIRUN_TURNS") != NULL;
	FILE *f = File();
	if (!f || !on) return;
	fprintf(f, "{\"t\":\"turn\",\"r\":%d,\"p\":%d,\"rng\":%d}\n", round, player,
	        g_rand ? g_rand->CallCount() : -1);
}

void airun_RecordRound(sint32 round)
{
	FILE *f = File();
	if (!f || !g_player || round == s_lastRound) return;
	Hook();
	s_lastRound = round;

	// The cities captured since the last round: still held, and how many units are in them
	for (size_t i = 0; i < s_captured.size(); ++i)
	{
		Unit const &city = s_captured[i].city;
		bool const held = city.IsValid() && city.GetOwner() == s_captured[i].owner;
		fprintf(f, "{\"t\":\"capstate\",\"r\":%d,\"city\":%s,\"owner\":%d,\"held\":%d,\"units\":%d}\n",
		        round, Json(held ? city.GetName() : "").c_str(), s_captured[i].owner, held ? 1 : 0,
		        held ? g_theWorld->GetCell(city.RetPos())->GetNumUnits() : -1);
	}
	s_captured.clear();

	// Once, before the first round: where each civ started (its first unit), which of
	// the map generator's starts that is (0 is placed first, 1 near it) and its score, its continent and that continent's size, and the
	// distance (tiles, wrap included) to every other start
	static bool startDone = false;
	if (!startDone)
	{
		startDone = true;
		MapPoint pos[k_MAX_PLAYERS];
		bool has[k_MAX_PLAYERS];
		for (sint32 p = 0; p < k_MAX_PLAYERS; ++p)
		{
			has[p] = g_player[p] && g_player[p]->m_all_units && g_player[p]->m_all_units->Num() > 0;
			if (has[p]) pos[p] = g_player[p]->m_all_units->Access(0).RetPos();
		}
		fprintf(f, "{\"t\":\"start\",\"w\":%d,\"h\":%d,\"players\":[",
		        g_theWorld->GetXWidth(), g_theWorld->GetYHeight());
		bool firstStart = true;
		for (sint32 p = 1; p < k_MAX_PLAYERS; ++p)
		{
			if (!has[p]) continue;
			sint16 cont = -1; bool isLand = false;
			g_theWorld->GetContinent(pos[p], cont, isLand);
			sint32 start = -1;
			for (sint32 j = 0; j < k_MAX_PLAYERS && start < 0; ++j)
			{
				if (g_player_start_list[j] == pos[p]) start = j;
			}
			fprintf(f, "%s{\"p\":%d,\"x\":%d,\"y\":%d,\"start\":%d,\"score\":%d,\"units\":%d,\"cont\":%d,\"contsize\":%d,\"dist\":{",
			        firstStart ? "" : ",", p, pos[p].x, pos[p].y, start,
			        start >= 0 ? g_player_start_score[start] : -1,
			        g_player[p]->m_all_units->Num(), isLand ? cont : -1,
			        isLand ? g_theWorld->GetLandContinentSize(cont) : 0);
			firstStart = false;
			bool firstDist = true;
			for (sint32 q = 1; q < k_MAX_PLAYERS; ++q)
			{
				if (q == p || !has[q]) continue;
				fprintf(f, "%s\"%d\":%d", firstDist ? "" : ",", q, pos[p].NormalizedDistance(pos[q]));
				firstDist = false;
			}
			fprintf(f, "}}");
		}
		fprintf(f, "]}\n");
	}

	fprintf(f, "{\"t\":\"round\",\"r\":%d,\"rng\":%d,\"players\":[", round,
	        g_rand ? g_rand->CallCount() : -1);
	bool first = true;
	for (sint32 p = 0; p < k_MAX_PLAYERS; ++p)
	{
		if (!g_player[p] || !g_player[p]->m_all_cities) continue;
		if (!first) fprintf(f, ",");
		first = false;
		PutPlayer(f, p, round);
		s_battles[p] = s_kills[p] = s_losses[p] = 0;
	}
	fprintf(f, "]}\n");
	fflush(f);

	if (s_exitRound >= 0 && round >= s_exitRound)
	{
		fprintf(f, "{\"t\":\"end\",\"r\":%d}\n", round);
		fclose(f);
		_exit(0);
	}
}
