//----------------------------------------------------------------------------
//
// Project      : Call To Power 2
// File type    : C++ source
// Description  : Battle odds for players: under the mouse, and the attack window
//                (win chance, expected losses, each unit's stats, and orders)
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
// The odds come from CTP2Combat::Simulate: the real combat rules, fought many times with
// dice of their own, so asking changes nothing in the game. They are reckoned against the
// units the attacker's owner can see on the tile.
//
//----------------------------------------------------------------------------

#include "c3.h"
#include "BattleOrdersWindow.h"

#include <cstdarg>
#include <cstdio>
#include <string>

#include "aui_ldl.h"
#include "c3ui.h"
#include "ctp2_button.h"
#include "ctp2_hypertextbox.h"
#include "ctp2_Static.h"
#include "ctp2_Window.h"

#include "ArmyData.h"
#include "Cell.h"
#include "cellunitlist.h"
#include "citydata.h"
#include "ConstRecord.h"
#include "GameEventManager.h"
#include "GameSettings.h"
#include "network.h"
#include "player.h"
#include "profileDB.h"
#include "SelItem.h"
#include "StrDB.h"
#include "terrainutil.h"
#include "TurnCnt.h"
#include "UnitData.h"
#include "UnitRecord.h"
#include "World.h"

extern C3UI *   g_c3ui;

namespace
{
	BattleOrdersWindow *    s_window = NULL;

	MBCHAR const * Str(MBCHAR const *id, MBCHAR const *english)
	{
		MBCHAR const *text = g_theStringDB ? g_theStringDB->GetNameStr(id) : NULL;
		return text ? text : english;
	}

	// The attack the player last confirmed in the window: let it through once
	struct Confirmed
	{
		uint32   army;
		MapPoint pos;
		sint32   round;
		bool     set;
		Confirmed() : army(0), pos(), round(-1), set(false) {}
	};
	Confirmed s_confirmed;

	sint32 CurrentRound()
	{
		return g_turn ? g_turn->GetRound() : 0;
	}

	// The last odds reckoned, so the info bar and the window don't fight the same battle
	// again while nothing has changed
	struct OddsCache
	{
		uint32       key;
		CombatOdds   odds;
		bool         set;
		OddsCache() : key(0), odds(), set(false) {}
	};
	OddsCache s_cache;

	uint32 Mix(uint32 h, uint32 v)
	{
		return (h ^ v) * 16777619u;
	}

	uint32 FightKey(CellUnitList &attackers, CellUnitList &defenders, const BattleOrders &orders,
	                sint32 trials)
	{
		uint32 h = 2166136261u;
		h = Mix(h, CurrentRound());
		h = Mix(h, orders.line * 16 + orders.retreat);
		h = Mix(h, trials);
		for (sint32 i = 0; i < attackers.Num(); ++i)
		{
			h = Mix(h, attackers[i].m_id);
			h = Mix(h, static_cast<uint32>(attackers[i].GetHP() * 1000.0));
		}
		h = Mix(h, 0xdead);
		for (sint32 i = 0; i < defenders.Num(); ++i)
		{
			h = Mix(h, defenders[i].m_id);
			h = Mix(h, static_cast<uint32>(defenders[i].GetHP() * 1000.0));
			h = Mix(h, defenders[i].IsEntrenched() ? 1 : 0);
		}
		return h;
	}

	void Append(std::string &s, MBCHAR const *fmt, ...)
	{
		MBCHAR buf[512];
		va_list vl;
		va_start(vl, fmt);
		vsnprintf(buf, sizeof(buf), fmt, vl);
		va_end(vl);
		s += buf;
	}

	// ", +50% veteran" style items, collected into "(+50% veteran, +25% terrain)"
	void AddBonus(std::string &list, double fraction, MBCHAR const *what)
	{
		sint32 const pct = static_cast<sint32>(fraction * 100.0 + (fraction < 0 ? -0.5 : 0.5));
		if (pct == 0)
			return;
		if (!list.empty())
			list += ", ";
		Append(list, "%+d%% %s", pct, what);
	}

	bool LeaderInCell(const MapPoint &pos)
	{
		Cell *cell = g_theWorld->GetCell(pos);
		for (sint32 i = 0; i < cell->GetNumUnits(); ++i)
		{
			if (cell->AccessUnit(i).GetDBRec()->GetLeader())
				return true;
		}
		return false;
	}

	// The unit's attack against OPPONENT, and the named reasons it differs from the base:
	// the parts of UnitData::GetOffense a player can act on. The rest (bonuses against the
	// opponent's unit type, civilisation, scripts) is summed as "against <opponent>".
	std::string AttackText(Unit &u, Unit &opponent)
	{
		const UnitRecord *rec = u.GetDBRec();
		double const base = rec->GetAttack();
		std::string s;
		if (base <= 0.0)
		{
			s = Str("str_ldl_BO_NO_ATTACK", "No attack");
		}
		else
		{
			double const value = u->GetOffense(opponent);
			double named = 0.0;
			std::string bonuses;
			double const vet = g_theConstDB->Get(0)->GetVeteranCoef() * 0.01;
			if (u.IsVeteran()) { AddBonus(bonuses, vet, Str("str_ldl_BO_VETERAN", "veteran")); named += vet; }
			if (u.IsElite())   { AddBonus(bonuses, vet, Str("str_ldl_BO_ELITE", "elite")); named += vet; }
			if (LeaderInCell(u.RetPos()))
			{
				AddBonus(bonuses, 0.5, Str("str_ldl_BO_LEADER", "leader"));
				named += 0.5;
			}
			double const other = value / base - 1.0 - named;
			if (other > 0.005 || other < -0.005)
			{
				std::string what = Str("str_ldl_BO_AGAINST", "against");
				what += " ";
				what += opponent.GetName();
				AddBonus(bonuses, other, what.c_str());
			}
			Append(s, "%s %.1f", Str("str_ldl_BO_ATTACK", "Attack"), value);
			if (!bonuses.empty())
				Append(s, " (%s %g: %s)", Str("str_ldl_BO_BASE", "base"), base, bonuses.c_str());
		}

		double const ranged = rec->GetZBRangeAttack();
		if (ranged > 0.0)
			Append(s, ", %s %.1f", Str("str_ldl_BO_RANGED", "Ranged"), u->GetRanged(opponent));
		return s;
	}

	// The same for defence (UnitData::GetDefense): the bonuses of the ground it stands on
	std::string DefenseText(Unit &u, Unit &opponent)
	{
		const UnitRecord *rec = u.GetDBRec();
		double const base = rec->GetDefense();
		std::string s;
		if (base <= 0.0)
			return Str("str_ldl_BO_NO_DEFENSE", "No defense");

		double const value = u->GetDefense(opponent);
		MapPoint const pos = u.RetPos();
		Cell *cell = g_theWorld->GetCell(pos);
		double named = 0.0;
		double flat = 0.0;
		std::string bonuses;

		if (g_theGameSettings->IsNewCombat())
		{
			double const vet = g_theConstDB->Get(0)->GetVeteranCoef() * 0.01;
			if (u.IsVeteran()) { AddBonus(bonuses, vet, Str("str_ldl_BO_VETERAN", "veteran")); named += vet; }
			if (u.IsElite())   { AddBonus(bonuses, vet, Str("str_ldl_BO_ELITE", "elite")); named += vet; }
			if (LeaderInCell(pos))
			{
				AddBonus(bonuses, 0.5, Str("str_ldl_BO_LEADER", "leader"));
				named += 0.5;
			}
		}

		if (!rec->GetNoDefenseBonuses())
		{
			if (u.IsEntrenched())
			{
				double const e = g_theConstDB->Get(0)->GetEntrenchmentBonus();
				AddBonus(bonuses, e, Str("str_ldl_BO_FORTIFIED", "fortified"));
				named += e;
			}

			double terrain = 0.0;
			double fort = 0.0;
			terrainutil_GetDefenseBonus(pos, terrain, fort);
			if (fort != 0.0)
			{
				AddBonus(bonuses, fort, Str("str_ldl_BO_FORT", "fort"));
				named += fort;
			}
			if (terrain > 0 &&
			    ((rec->GetMovementTypeLand() && g_theWorld->IsLand(pos)) ||
			     (rec->GetMovementTypeMountain() && g_theWorld->IsMountain(pos)) ||
			     (rec->GetMovementTypeSea() && g_theWorld->IsWater(pos)) ||
			     (rec->GetMovementTypeSpace() && g_theWorld->IsSpace(pos))))
			{
				AddBonus(bonuses, terrain, Str("str_ldl_BO_TERRAIN", "terrain"));
				named += terrain;
			}

			if (cell->GetCity().m_id != 0)
			{
				flat = cell->GetCity().CD()->GetDefendersBonus();
			}
		}

		double const other = (value - flat) / base - 1.0 - named;
		if (other > 0.005 || other < -0.005)
		{
			std::string what = Str("str_ldl_BO_AGAINST", "against");
			what += " ";
			what += opponent.GetName();
			AddBonus(bonuses, other, what.c_str());
		}
		if (flat >= 0.5 || flat <= -0.5)
		{
			if (!bonuses.empty())
				bonuses += ", ";
			Append(bonuses, "%+.0f %s", flat, Str("str_ldl_BO_CITY", "city"));
		}

		Append(s, "%s %.1f", Str("str_ldl_BO_DEFENSE", "Defense"), value);
		if (!bonuses.empty())
			Append(s, " (%s %g: %s)", Str("str_ldl_BO_BASE", "base"), base, bonuses.c_str());
		return s;
	}

	// The unit of LIST that the other side mostly fights: its strongest by STAT
	sint32 Strongest(CellUnitList &list, bool byAttack)
	{
		sint32 best = 0;
		double bestValue = -1.0;
		for (sint32 i = 0; i < list.Num(); ++i)
		{
			const UnitRecord *rec = list[i].GetDBRec();
			double const v = byAttack ? rec->GetAttack() : rec->GetDefense();
			if (v > bestValue)
			{
				bestValue = v;
				best = i;
			}
		}
		return best;
	}

	void UnitCard(std::string &s, Unit &u, Unit &opponent, bool attacking, double survival)
	{
		Append(s, "<b:1>%s<b:0>", u.GetName());
		if (u.IsElite())
			Append(s, ", %s", Str("str_ldl_BO_ELITE", "elite"));
		else if (u.IsVeteran())
			Append(s, ", %s", Str("str_ldl_BO_VETERAN", "veteran"));
		Append(s, "   %s %.0f%%\n", Str("str_ldl_BO_SURVIVES", "survives"), survival * 100.0);

		const UnitRecord *rec = u.GetDBRec();
		Append(s, "    %s %.0f/%d   ", Str("str_ldl_BO_HP", "HP"), u.GetHP(), u->CalculateTotalHP());
		if (attacking)
			s += AttackText(u, opponent);
		else
			s += DefenseText(u, opponent);
		Append(s, "   %s %d   %s %g\n", Str("str_ldl_BO_FIREPOWER", "Firepower"), rec->GetFirepower(),
		       Str("str_ldl_BO_ARMOR", "Armor"), rec->GetArmor());
	}

	MBCHAR const * LineText(sint32 line)
	{
		return (line == BattleOrders::LINE_WOUNDED_BACK)
		       ? Str("str_ldl_BO_LINE_WOUNDED", "Line: wounded units in the second rank")
		       : Str("str_ldl_BO_LINE_AUTO", "Line: everyone fights");
	}

	MBCHAR const * RetreatText(sint32 retreat)
	{
		switch (retreat)
		{
		case BattleOrders::RETREAT_OUTNUMBERED:
			return Str("str_ldl_BO_RETREAT_OUTNUMBERED", "Retreat: when outnumbered");
		case BattleOrders::RETREAT_HALF_LOST:
			return Str("str_ldl_BO_RETREAT_HALF", "Retreat: when half are lost");
		default:
			return Str("str_ldl_BO_RETREAT_NEVER", "Retreat: never");
		}
	}
}

bool battleodds_VisibleDefenders(sint32 player, const MapPoint &pos, CellUnitList &defenders)
{
	defenders.Clear();
	if (player < 0 || !g_player[player] || !g_player[player]->IsVisible(pos))
		return false;

	CellUnitList *units = g_theWorld->GetCell(pos)->UnitArmy();
	if (!units || units->Num() <= 0 || units->GetOwner() == player)
		return false;

	for (sint32 i = 0; i < units->Num(); ++i)
	{
		Unit u = units->Access(i);
		if (u.IsValid() && (u.GetVisibility() & (1 << player)))
			defenders.Insert(u);
	}
	return defenders.Num() > 0;
}

bool battleodds_ForAttack(const Army &army, const MapPoint &pos, CombatOdds &odds,
                          const BattleOrders &orders, sint32 trials)
{
	if (!army.IsValid() || army.Num() <= 0)
		return false;

	CellUnitList defenders;
	if (!battleodds_VisibleDefenders(army.GetOwner(), pos, defenders))
		return false;

	ArmyData *data = army.AccessData();
	if (!data->CanFight(defenders))
		return false;

	// Where the attackers stand matters too (city attack bonuses, leaders)
	MapPoint from;
	army.GetPos(from);
	uint32 const key = Mix(FightKey(*data, defenders, orders, trials) ^ (pos.x << 16 | pos.y),
	                       from.x << 16 | from.y);
	if (s_cache.set && s_cache.key == key)
	{
		odds = s_cache.odds;
		return true;
	}

	// The test runs' seed (combatevent.cpp): the same fight gives the same numbers
	uint32 const seed = army.m_id * 2654435761u ^ (pos.x << 16 | pos.y) ^ CurrentRound();
	odds = CTP2Combat::Simulate(*data, defenders, trials, seed, &orders);

	s_cache.key  = key;
	s_cache.odds = odds;
	s_cache.set  = true;
	return true;
}

void battleodds_InfoBarText(const MapPoint &pos, MBCHAR *text, size_t size)
{
	text[0] = '\0';
	if (!g_theProfileDB || !g_theProfileDB->IsBattleOddsOnMap() || !g_selected_item)
		return;
	if (!g_selected_item->IsLocalArmy())
		return;

	Army army;
	if (!g_selected_item->GetSelectedArmy(army) || !army.IsValid())
		return;
	if (army->RetPos() == pos)
		return;

	CombatOdds odds;
	if (!battleodds_ForAttack(army, pos, odds))
		return;

	snprintf(text, size, Str("str_ldl_BO_INFOBAR", "Attack: win %.0f%%, lose ~%.1f of %d, kill ~%.1f of %d"),
	         odds.AttackerWinChance() * 100.0,
	         odds.AttackerLosses(), odds.attackers,
	         odds.DefenderLosses(), odds.defenders);
}

bool BattleOrdersWindow::Intercept(const Army &army, const MapPoint &pos)
{
	if (s_confirmed.set
	 && s_confirmed.army == army.m_id
	 && s_confirmed.pos == pos
	 && s_confirmed.round == CurrentRound())
	{
		s_confirmed.set = false;
		return false;
	}

	if (!g_theProfileDB || !g_theProfileDB->IsAskBeforeAttack() || g_theProfileDB->IsObserverMode())
		return false;
	if (g_network.IsActive())
		return false;

	sint32 const owner = army.GetOwner();
	if (!g_player[owner] || g_player[owner]->IsRobot() || owner != g_selected_item->GetVisiblePlayer())
		return false;

	CombatOdds odds;
	if (!battleodds_ForAttack(army, pos, odds))
		return false;

	Open(army, pos);
	return true;
}

BattleOrdersWindow::BattleOrdersWindow()
:
	m_window        (static_cast<ctp2_Window *>(aui_Ldl::BuildHierarchyFromRoot("BattleOrdersDialog"))),
	m_header        (static_cast<ctp2_Static *>(aui_Ldl::GetObject("BattleOrdersDialog.Header"))),
	m_odds          (static_cast<ctp2_Static *>(aui_Ldl::GetObject("BattleOrdersDialog.Odds"))),
	m_units         (static_cast<ctp2_HyperTextBox *>(aui_Ldl::GetObject("BattleOrdersDialog.Units"))),
	m_lineButton    (static_cast<ctp2_Button *>(aui_Ldl::GetObject("BattleOrdersDialog.LineButton"))),
	m_retreatButton (static_cast<ctp2_Button *>(aui_Ldl::GetObject("BattleOrdersDialog.RetreatButton"))),
	m_attackButton  (static_cast<ctp2_Button *>(aui_Ldl::GetObject("BattleOrdersDialog.AttackButton"))),
	m_cancelButton  (static_cast<ctp2_Button *>(aui_Ldl::GetObject("BattleOrdersDialog.CancelButton"))),
	m_army          (),
	m_pos           (),
	m_orders        ()
{
	Assert(m_window && m_header && m_odds && m_units && m_lineButton && m_retreatButton
	       && m_attackButton && m_cancelButton);

	if (m_lineButton)    m_lineButton->SetActionFuncAndCookie(ButtonCallback, this);
	if (m_retreatButton) m_retreatButton->SetActionFuncAndCookie(ButtonCallback, this);
	if (m_attackButton)  m_attackButton->SetActionFuncAndCookie(ButtonCallback, this);
	if (m_cancelButton)  m_cancelButton->SetActionFuncAndCookie(ButtonCallback, this);
}

BattleOrdersWindow::~BattleOrdersWindow()
{
	if (m_window)
	{
		aui_Ldl::DeleteHierarchyFromRoot("BattleOrdersDialog");
	}
}

void BattleOrdersWindow::Update(void)
{
	if (!m_window || !m_army.IsValid())
		return;

	CellUnitList defenders;
	CombatOdds odds;
	if (!battleodds_VisibleDefenders(m_army.GetOwner(), m_pos, defenders)
	 || !battleodds_ForAttack(m_army, m_pos, odds, m_orders, 1000))
	{
		Close();
		return;
	}

	CellUnitList &attackers = *m_army.AccessData();
	sint32 const defOwner = defenders.GetOwner();

	MBCHAR attName[_MAX_PATH];
	MBCHAR defName[_MAX_PATH];
	attName[0] = defName[0] = '\0';
	g_player[m_army.GetOwner()]->GetPluralCivName(attName);
	if (g_player[defOwner])
		g_player[defOwner]->GetPluralCivName(defName);

	std::string header;
	Unit city = g_theWorld->GetCity(m_pos);
	if (city.IsValid())
		Append(header, Str("str_ldl_BO_HEADER_CITY", "%s (%d) attack %s (%d) in %s"),
		       attName, attackers.Num(), defName, defenders.Num(), city.CD()->GetName());
	else
		Append(header, Str("str_ldl_BO_HEADER", "%s (%d) attack %s (%d)"),
		       attName, attackers.Num(), defName, defenders.Num());
	m_header->SetText(header.c_str());

	std::string line;
	Append(line, Str("str_ldl_BO_ODDS", "Win %.0f%%   Lose %.0f%%"),
	       odds.AttackerWinChance() * 100.0, odds.DefenderWinChance() * 100.0);
	if (odds.retreats > 0)
		Append(line, Str("str_ldl_BO_ODDS_RETREAT", "   Retreat %.0f%%"), odds.RetreatChance() * 100.0);
	Append(line, Str("str_ldl_BO_ODDS_LOSSES", "      Our losses ~%.1f of %d, theirs ~%.1f of %d"),
	       odds.AttackerLosses(), odds.attackers, odds.DefenderLosses(), odds.defenders);
	m_odds->SetText(line.c_str());

	Unit theirBest = defenders[Strongest(defenders, false)];
	Unit ourBest   = attackers[Strongest(attackers, true)];

	std::string cards;
	Append(cards, "<c:0,0,128><b:1>%s<b:0><c:0,0,0>\n", Str("str_ldl_BO_OURS", "Our units"));
	for (sint32 i = 0; i < attackers.Num(); ++i)
	{
		UnitCard(cards, attackers[i], theirBest, true, odds.AttackerSurvival(i));
	}
	Append(cards, "\n<c:128,0,0><b:1>%s<b:0><c:0,0,0>\n", Str("str_ldl_BO_THEIRS", "Their units"));
	for (sint32 i = 0; i < defenders.Num(); ++i)
	{
		UnitCard(cards, defenders[i], ourBest, false, odds.DefenderSurvival(i));
	}
	sint32 const hidden = g_theWorld->GetCell(m_pos)->GetNumUnits() - defenders.Num();
	if (hidden > 0)
		Append(cards, "\n%s\n", Str("str_ldl_BO_HIDDEN", "Units you can't see may also be there."));
	m_units->SetHyperText(cards.c_str());

	m_lineButton->SetText(LineText(m_orders.line));
	m_retreatButton->SetText(RetreatText(m_orders.retreat));

	// The texts have no background of their own: redraw the window under them
	m_window->ShouldDraw(TRUE);
}

void BattleOrdersWindow::ButtonCallback(aui_Control *control, uint32 action, uint32 data, Cookie cookie)
{
	if (action != static_cast<uint32>(AUI_BUTTON_ACTION_EXECUTE))
		return;

	BattleOrdersWindow *window = static_cast<BattleOrdersWindow *>(cookie.m_voidPtr);
	if (!window)
		return;

	if (control == window->m_cancelButton)
	{
		Close();
	}
	else if (control == window->m_lineButton)
	{
		window->m_orders.line = (window->m_orders.line + 1) % 2;
		window->Update();
	}
	else if (control == window->m_retreatButton)
	{
		window->m_orders.retreat = (window->m_orders.retreat + 1) % 3;
		window->Update();
	}
	else if (control == window->m_attackButton)
	{
		Army const army = window->m_army;
		MapPoint const pos = window->m_pos;
		BattleOrders const orders = window->m_orders;
		Close();
		if (!army.IsValid())
			return;

		MapPoint from;
		army.GetPos(from);
		WORLD_DIRECTION const dir = from.GetNeighborDirection(pos);
		if (dir == NOWHERE)
			return;

		s_confirmed.army  = army.m_id;
		s_confirmed.pos   = pos;
		s_confirmed.round = CurrentRound();
		s_confirmed.set   = true;
		CTP2Combat::SetPendingOrders(army.m_id, pos, orders);

		// A normal one-step move, so every check of an attack applies as before
		g_gevManager->AddEvent(GEV_INSERT_Tail, GEV_MoveToOrder,
		                       GEA_Army,      army,
		                       GEA_Direction, dir,
		                       GEA_End);
	}
}

void BattleOrdersWindow::Open(const Army &army, const MapPoint &pos)
{
	if (!s_window)
		s_window = new BattleOrdersWindow();

	if (!s_window->m_window)
		return;

	// Orders carry over from the last attack: a player who wants them usually wants them again
	s_window->m_army = army;
	s_window->m_pos  = pos;
	s_window->Update();
	if (!s_window->m_army.IsValid())
		return;

	g_c3ui->AddWindow(s_window->m_window);
	s_window->m_window->Show();
}

void BattleOrdersWindow::Close(void)
{
	if (!s_window || !s_window->m_window)
		return;

	s_window->m_army = Army();
	s_window->m_window->Hide();
	g_c3ui->RemoveWindow(s_window->m_window->Id());
}

void BattleOrdersWindow::Cleanup(void)
{
	Close();
	delete s_window;
	s_window = NULL;
	s_cache.set = false;
	s_confirmed.set = false;
}
