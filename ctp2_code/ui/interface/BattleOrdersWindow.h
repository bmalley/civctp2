//----------------------------------------------------------------------------
//
// Project      : Call To Power 2
// File type    : C++ header
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

#ifdef HAVE_PRAGMA_ONCE
#pragma once
#endif

#ifndef __BATTLEORDERSWINDOW_H__
#define __BATTLEORDERSWINDOW_H__

#include "ctp2_inttypes.h"
#include "MapPoint.h"
#include "Army.h"
#include "CTP2Combat.h"
#include "aui_control.h"

class ctp2_Button;
class ctp2_HyperTextBox;
class ctp2_Static;
class ctp2_Window;
class CellUnitList;

// The units on POS that PLAYER can see and could attack: what the odds are reckoned against.
// False if there are none (or POS is out of sight).
bool battleodds_VisibleDefenders(sint32 player, const MapPoint &pos, CellUnitList &defenders);

// What ARMY attacking POS can expect, against the units its owner sees there. False if it
// can't attack them. Cached: asking again for the same fight costs nothing.
bool battleodds_ForAttack(const Army &army, const MapPoint &pos, CombatOdds &odds,
                          const BattleOrders &orders = BattleOrders(), sint32 trials = 200);

// The info bar's text for the selected army and the tile under the mouse (empty if none)
void battleodds_InfoBarText(const MapPoint &pos, MBCHAR *text, size_t size);

class BattleOrdersWindow
{
public:
	// Called where a human's attack would start a battle: opens the attack window instead
	// and returns true, unless the player has just confirmed this attack (or doesn't want
	// to be asked)
	static bool Intercept(const Army &army, const MapPoint &pos);

	static void Open(const Army &army, const MapPoint &pos);
	static void Close(void);
	static void Cleanup(void);

private:
	BattleOrdersWindow();
	~BattleOrdersWindow();

	void        Update(void);
	static void ButtonCallback(aui_Control *control, uint32 action, uint32 data, Cookie cookie);

	ctp2_Window *           m_window;
	ctp2_Static *           m_header;
	ctp2_Static *           m_odds;
	ctp2_HyperTextBox *     m_units;
	ctp2_Button *           m_lineButton;
	ctp2_Button *           m_retreatButton;
	ctp2_Button *           m_attackButton;
	ctp2_Button *           m_cancelButton;

	Army                    m_army;
	MapPoint                m_pos;
	BattleOrders            m_orders;
};

#endif
