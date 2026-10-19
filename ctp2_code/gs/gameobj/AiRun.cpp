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
#include "Score.h"
#include "StrategyRecord.h"
#include "Unit.h"
#include "UnitData.h"
#include "UnitDynArr.h"
#include "UnitRecord.h"
#include "World.h"              // g_theWorld

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
		}
		return GEV_HD_Continue;
	}

	STDEHANDLER(AiRun_KillCity)
	{
		Unit city;
		sint32 cause, killer;
		if (s_file && args->GetCity(0, city) && city.IsValid() && args->GetInt(0, cause)
		 && args->GetPlayer(0, killer))
		{
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

bool airun_GetSeed(uint32 &seed)
{
	const char *s = getenv("CTP2_SEED");
	if (!s || !*s) return false;
	seed = static_cast<uint32>(strtoul(s, NULL, 10));
	return true;
}

void airun_RecordRound(sint32 round)
{
	FILE *f = File();
	if (!f || !g_player || round == s_lastRound) return;
	Hook();
	s_lastRound = round;

	fprintf(f, "{\"t\":\"round\",\"r\":%d,\"players\":[", round);
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
