//----------------------------------------------------------------------------
//
// Project      : Call To Power 2
// File type    : C++ header
// Description  : Combat handling
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
// TEST_APP
//
//----------------------------------------------------------------------------
//
// Modifications from the original Activision code:
//
// - Veteran effect added.
// - Replaced old const database by new one. (5-Aug-2007 Martin Gühmann)
// - Added a new combat option (28-Feb-2009 Maq)
//
//----------------------------------------------------------------------------

#ifndef CTP2_COMBAT_H__
#define CTP2_COMBAT_H__

#ifdef TEST_APP

typedef long sint32;
typedef unsigned long uint32;
#else

#include "GameEventManager.h"
#include "ConstRecord.h"	// g_theConstDB
#include "Unit.h"
#include "MapPoint.h"
#include "battle.h"
#include "dynarr.h"

#define k_COMBAT_WIDTH 4
#define k_COMBAT_HEIGHT 7

#endif

enum UNIT_TYPE {
	UNIT_TYPE_GROUND,
	UNIT_TYPE_NAVAL,
	UNIT_TYPE_AIR,
	UNIT_TYPE_FLANKER,
	UNIT_TYPE_NONCOMBAT,
};

#define k_AIR_COL 2
#define k_RANGED_COL 1
#define k_FRONT_COL 0
#define k_NONCOMBAT_COL 3

#define k_NON_FLANKER_PRIORITY 10000

class Army;
class Battle;
class BattleEvent;
class RandomGenerator;

// Orders a human attacker gives before a fight (the attack dialog, BattleOrdersWindow).
// The defaults are the old rules; the AI and every defender always fight with them.
struct BattleOrders
{
	enum Line
	{
		LINE_AUTO,              // the old layout
		LINE_WOUNDED_BACK       // units under half their hit points wait in the second rank
	};
	enum Retreat
	{
		RETREAT_NEVER,
		RETREAT_OUTNUMBERED,    // when the defenders have more units left than we do
		RETREAT_HALF_LOST       // when half the units we started with are dead
	};
	sint32 line;
	sint32 retreat;

	BattleOrders() : line(LINE_AUTO), retreat(RETREAT_NEVER) {}
	bool IsDefault() const { return line == LINE_AUTO && retreat == RETREAT_NEVER; }
};

#define k_COMBAT_ODDS_UNITS 28  // k_COMBAT_WIDTH * k_COMBAT_HEIGHT: no stack is bigger

// What CTP2Combat::Simulate found: how a fight between two stacks ends, over a number of
// trials of the real combat rules
struct CombatOdds
{
	sint32 trials;
	sint32 attackerWins;    // every defender dead
	sint32 defenderWins;    // every attacker dead
	sint32 retreats;        // the attacker retreated (its orders)
	double attackerLeft;    // attacker units alive at the end, summed over the trials
	double defenderLeft;    // defender units alive at the end, summed over the trials
	sint32 attackers;       // the stack sizes
	sint32 defenders;
	// How often each unit, by its index in the lists given to Simulate, was alive at the end
	sint32 attackerSurvived[k_COMBAT_ODDS_UNITS];
	sint32 defenderSurvived[k_COMBAT_ODDS_UNITS];

	CombatOdds()
	:   trials(0), attackerWins(0), defenderWins(0), retreats(0),
	    attackerLeft(0.0), defenderLeft(0.0), attackers(0), defenders(0)
	{
		for (sint32 i = 0; i < k_COMBAT_ODDS_UNITS; ++i)
		{
			attackerSurvived[i] = 0;
			defenderSurvived[i] = 0;
		}
	}
	double AttackerWinChance() const { return trials ? double(attackerWins) / trials : 0.0; }
	double DefenderWinChance() const { return trials ? double(defenderWins) / trials : 0.0; }
	double RetreatChance() const { return trials ? double(retreats) / trials : 0.0; }
	// Expected units lost
	double AttackerLosses() const { return trials ? attackers - attackerLeft / trials : 0.0; }
	double DefenderLosses() const { return trials ? defenders - defenderLeft / trials : 0.0; }
	double AttackerSurvival(sint32 i) const
	{ return (trials && i >= 0 && i < k_COMBAT_ODDS_UNITS) ? double(attackerSurvived[i]) / trials : 0.0; }
	double DefenderSurvival(sint32 i) const
	{ return (trials && i >= 0 && i < k_COMBAT_ODDS_UNITS) ? double(defenderSurvived[i]) / trials : 0.0; }
};

class CombatUnit {
private:
	double m_offense, m_defense,
		m_strength, m_armor,
		m_ranged, m_hp;
	bool m_isVeteran;
	UNIT_TYPE m_type;
	sint32 m_preferredCol;
	sint32 m_priority;

	bool m_valid;
	double m_maxHP;
	bool m_reserve;         // BattleOrders::LINE_WOUNDED_BACK: kept out of the front line

public:
	double GetMaxHP() const { return m_maxHP; }
	bool IsReserve() const { return m_reserve; }
	void SetReserve(bool reserve) { m_reserve = reserve; }

public:
	bool m_alreadyExploded;
	bool m_alreadyAttacked;
#ifdef TEST_APP
	uint32 m_id;
#else
	Unit m_unit;
#endif

	CombatUnit() { m_valid = false; m_maxHP = 0.0; m_reserve = false; }

#ifdef TEST_APP
	CombatUnit(double offense, double defense,
			   double strength, double armor,
			   double ranged, double hp,
			   UNIT_TYPE type,
			   bool const isVeteran = false
			  );
#else
	CombatUnit(double offense, double defense,
			   double strength, double armor,
			   double ranged, double hp,
			   Unit &u);
#endif

	double GetOffense() const
	{
		return (m_isVeteran)
			   ? m_offense + (m_offense * g_theConstDB->Get(0)->GetVeteranCoef() * 0.01)
			   : m_offense;
	}
	double GetDefense() { return m_defense; }
	double GetStrength() { return m_strength; }
	double GetArmor() { return m_armor; }
	double GetRangedAttack() const
	{
		return (m_isVeteran)
			   ? m_ranged + (m_ranged * g_theConstDB->Get(0)->GetVeteranCoef() * 0.01)
			   : m_ranged;
	}
	UNIT_TYPE GetCombatType() { return m_type; }
	sint32 GetPreferredCol() { return m_preferredCol; }
	void SetPreferredCol(sint32 col) { m_preferredCol = col; }

	sint32 GetPriority() { return m_priority; }
	void SetPriority(sint32 pri) { m_priority = pri; }

	double GetHP() { return m_hp; }
	void DeductHP(double amt)
#ifndef TEST_APP
		;
#else
	{
		m_hp -= amt;
		if(m_hp < 0.001) {
			m_hp = 0;
		}

	}
#endif

	bool IsValid() { return m_valid; }
	bool IsAlive() { return m_hp > 0.001; }
	bool IsActive() { return IsValid() && IsAlive(); }

	void AddKill();

	void Invalidate() { m_valid = false; }
};

class CombatField {
private:
	CombatUnit **m_field;
	sint32 m_width, m_height;
	bool m_isOffense;

public:
	CombatField(sint32 width, sint32 height, bool isOffense);

	~CombatField();

#ifndef TEST_APP
	void FillFrom(CellUnitList &units);
	void ReportUnits(Battle *battle, BattleEvent *event, bool initial);
	bool IsActiveUnit(uint32 unitId);
	void AddActiveTo(CellUnitList &units);
#endif

	// BattleOrders::LINE_WOUNDED_BACK
	void MarkWoundedReserve();
	bool HasReserve();
	void ClearReserve();

	void StartRound();

	CombatUnit &GetUnit(sint32 x, sint32 y);
	void SetUnit(sint32 x, sint32 y, CombatUnit *u);
	bool FindTargetForRangedAttackFrom(sint32 x, sint32 y, sint32 *dx, sint32 *dy);
	bool FindTargetForAttackFrom(sint32 x, sint32 y, sint32 *dx, sint32 *dy, bool isFlanker);
	sint32 NumAlive();

	void Move();
	void Sort();
	void InsertInPreferredCol(CombatUnit *u);

	sint32 CountColumn(sint32 col);
	void MoveRowForward(sint32 row);
	void MoveUnitsToFront(sint32 num);
	void FindOpponents(CombatField &opposite);
	void FillHoles();

#ifdef TEST_APP
	void Dump(bool flip);
#endif

};

class CTP2Combat
{
public:
#ifdef TEST_APP
	CTP2Combat(sint32 width, sint32 height, FILE *input);
#else
	CTP2Combat(sint32 width, sint32 height, CellUnitList &attackers, CellUnitList &defenders,
	           const BattleOrders *orders = NULL, sint32 attackersAtStart = 0);
	~CTP2Combat();

	// Fights ATTACKERS against DEFENDERS TRIALS times by the real rules and counts the
	// outcomes. It changes nothing: no hit points, no deaths, no battle view, and its dice
	// come from a generator of its own seeded with SEED, not from the synced g_rand, so a
	// game plays the same whether or not anything simulates fights. ORDERS are the
	// attacker's; ATTACKERS_AT_START, if not 0, is what RETREAT_HALF_LOST counts from.
	static CombatOdds Simulate(CellUnitList &attackers, CellUnitList &defenders,
	                           sint32 trials, uint32 seed,
	                           const BattleOrders *orders = NULL, sint32 attackersAtStart = 0);
	// The same from this fight's state now: its living units, their hit points, its orders
	CombatOdds SimulateFromNow(sint32 trials, uint32 seed);
	const CombatOdds & GetPrediction() const { return m_prediction; }
	void SetPrediction(const CombatOdds & odds) { m_prediction = odds; }
	const BattleOrders & GetOrders() const { return m_orders; }
	sint32 GetAttackersAtStart() const { return m_attackersAtStart; }
	sint32 GetDefendersAtStart() const { return m_defendersAtStart; }
	sint32 GetRound() const { return m_round; }
	bool IsRetreating() const { return m_retreating; }
	bool RetreatedByOrders() const { return m_retreatByOrders; }

	// The attack dialog's orders for ARMY's next attack on POS, this round; StartCombat
	// takes them
	static void SetPendingOrders(uint32 army, const MapPoint &pos, const BattleOrders &orders);
	static bool TakePendingOrders(uint32 army, const MapPoint &pos, BattleOrders &orders);
#endif

	bool ResolveOneRound();
	bool IsDone();

	sint32 GetNumLivingAttackers() { return m_attackers.NumAlive(); }
	sint32 GetNumLivingDefenders() { return m_defenders.NumAlive(); }

	void Retreat();
	bool IsRetreated() { return m_retreated; }
	// The battle view's Skip: the remaining rounds are fought without being played
	void SkipToResult() { m_skipping = true; }

#ifndef TEST_APP
	sint32 GetAttacker() { return m_attacker; }
	sint32 GetDefender() { return m_defender; }
	Army GetAttackerArmy();
	MapPoint GetDefenderLocation() { return m_defenderPos; }

	Battle *GetBattle() { return m_battle; }

	void KillBattle() { delete m_battle; m_battle = NULL; }
	void ClearBattle() { m_battle = NULL; }
#endif

	// A unit can be hit again (e.g. in a later ranged/normal sub-phase)
	// before FillHoles() prunes its now-dead slot out of the combat field,
	// which would insert it into m_deadUnits twice - KillUnits() would
	// then queue two GEV_KillUnit events for the same unit id.
	void AddDeadUnit(Unit &u) { if (m_deadUnits.Find(u) < 0) m_deadUnits.Insert(u); }
	void KillUnits(GAME_EVENT_INSERT priority);

private:
	CombatField m_attackers, m_defenders;
	sint32 m_width, m_height;
	sint32 m_round;
	bool m_retreated, m_retreating, m_noAttacksPossible;
	bool m_skipping;
	sint32 m_attacker, m_defender;
#ifndef TEST_APP
	uint32 m_army_id;
	MapPoint m_defenderPos;
	Battle *m_battle;
	sint32 m_roundsSinceUpdate;
	DynamicArray<Unit> m_deadUnits;
	RandomGenerator *m_simRand;     // set while simulating: the dice for this fight
	CombatOdds m_prediction;        // what Simulate expected of this fight (test runs)
	BattleOrders m_orders;          // the attacker's
	sint32 m_attackersAtStart;
	sint32 m_defendersAtStart;
	bool m_retreatByOrders;

	bool ShouldRetreat();
#endif

#ifdef TEST_APP
	void DumpState();
#endif

	void ExecuteRangedAttack(CombatField *attacker, sint32 attX, sint32 attY,
							 CombatField *defender, sint32 defX, sint32 defY);
	void DoRangedAttacks(CombatField *attacker, CombatField *defender);
	void DoRangedAttacks();
	void DoRangedCounterAttacks();

	void ExecuteAttack(CombatField *attacker, sint32 attX, sint32 attY,
					   CombatField *defender, sint32 defX, sint32 defY);
	void DoAttacks(CombatField *attacker, CombatField *defender);
	void DoAttacks();
	void DoCounterAttacks();
	// below are used only when g_theProfileDB->IsNewCombat
	void ExecuteRangedCounterAttackNC(CombatField *attacker, sint32 attX, sint32 attY,
							 CombatField *defender, sint32 defX, sint32 defY);
	void DoRangedCounterAttacksNC(CombatField *attacker, CombatField *defender);
	void ExecuteCounterAttackNC(CombatField *attacker, sint32 attX, sint32 attY,
					   CombatField *defender, sint32 defX, sint32 defY);
	void DoCounterAttacksNC(CombatField *attacker, CombatField *defender);
	// end newcombat option -Maq
	void DoMovement();

	void Balance();
};

#ifndef TEST_APP
extern CTP2Combat *g_theCurrentBattle;
#endif

#endif
