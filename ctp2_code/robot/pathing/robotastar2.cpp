//----------------------------------------------------------------------------
//
// Project      : Call To Power 2
// File type    : C++ source
// Description  : A-star AI pathing algorithm
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
// - None
//
//----------------------------------------------------------------------------
//
// Modifications from the original Activision code:
//
// - For an AI transporter it is no more checked, whether the transporter cannot
//   unload its cargo, because the cargo has not enough move points.
// - A transporter's path does not not leave the target continent, once it
//   is on the target continent. (13-Jul-2009 Martin Gühmann)
//
//----------------------------------------------------------------------------

#include "c3.h"
#include "robotastar2.h"

#include "Path.h"
#include "World.h"          // g_theWorld
#include "ArmyData.h"
#include "UnitData.h"
#include "UnitDynArr.h"
#include "Diplomat.h"
#include "player.h"
#include "ctpaidebug.h"
#include "AgreementMatrix.h"
#include "PathCacheEpoch.h"

#include <cstring>
#include <map>
#include <string>

uint32 const    INCURSION_PERMISSION_ALL    = 0xffffffffu;

uint32 g_pathCacheEpoch = 0;

namespace
{
	// A kept search: what UnitAstar::FindPath returned, and the path and cost it left.
	struct KeptPath
	{
		bool  searched;
		bool  is_broken_path;
		Path  path;
		float total_cost;
	};

	// Everything a search reads that isn't covered by g_pathCacheEpoch, as bytes.
	typedef std::map<std::string, KeptPath> KeptPaths;
	KeptPaths s_keptPaths;
	uint32    s_keptEpoch = 0;
	sint32    s_keptOwner = -1;

	template <typename T> void AddToKey(std::string & key, const T & value)
	{
		key.append(reinterpret_cast<const char *>(&value), sizeof(value));
	}

	void AddUnitToKey(std::string & key, const Unit & unit)
	{
		AddToKey(key, unit.m_id);
		AddToKey(key, unit.GetType());
		AddToKey(key, unit.GetHP());
		AddToKey(key, unit.GetMovementPoints());
		AddToKey(key, unit.GetFuel());
		AddToKey(key, unit.IsVeteran());
		AddToKey(key, unit.IsEntrenched());
		AddToKey(key, unit.GetNumCarried());
	}

	bool SamePath(Path & a, Path & b)
	{
		if (a.Num() != b.Num()) return false;
		MapPoint pa, pb;
		a.GetStartPoint(pa);
		b.GetStartPoint(pb);
		if (pa != pb) return false;
		Path ca(&a), cb(&b);
		ca.Restart(pa);
		cb.Restart(pb);
		while (!ca.IsEnd() || !cb.IsEnd())
		{
			if (ca.IsEnd() != cb.IsEnd()) return false;
			ca.Next(pa);
			cb.Next(pb);
			if (pa != pb) return false;
		}
		return true;
	}
}

RobotAstar2 RobotAstar2::s_aiPathing;

RobotAstar2::RobotAstar2()
{
	m_pathType = PATH_TYPE_DEFAULT;
}

bool RobotAstar2::IsMoveZOC(const MapPoint & start, const MapPoint & dest) const
{
	if (g_player[m_owner]->IsRobot() && !g_player[m_owner]->IsVisible(dest))
	{
		return false;
	}

	return UnitAstar::IsMoveZOC(start, dest);
}

bool RobotAstar2::TransportPathCallback (const bool & can_enter,
                                         const MapPoint & prev,
                                         const MapPoint & pos,
                                         const bool & is_zoc,
                                         float & cost,
                                         ASTAR_ENTRY_TYPE & entry )
{
	if (can_enter)
	{
		ContinentIDs cont = g_theWorld->GetContinent(pos);
		bool is_land    = ( g_theWorld->IsLand(pos) || g_theWorld->IsMountain(pos) );
		bool wrong_cont = ( cont != m_transDestCont ) && (!g_theWorld->IsCity(pos));

		bool occupied = (m_army->HasCargo() && (!m_army.CanAtLeastOneCargoUnloadAt(pos, false, !m_is_robot)));

		// World::IsTunnel(pos) requires IsWater(pos) first, so a tunnel
		// tile also satisfies IsWater/IsShallowWater below - without this,
		// cargo standing on a tunnel (already disembarked onto its
		// walkable surface) would be treated as still aboard the ship,
		// same as if prev were open water.
		bool const prevIsTunnel = g_theWorld->IsTunnel(prev);

		// On an X-wrap map, x=0 should be seamlessly adjacent to x=width-1,
		// not a dead end. Log continent IDs on both sides whenever the
		// search actually looks at a tile near that seam, to check whether
		// GrowWater/GrowLand assign matching IDs across it. Tightly gated
		// so this doesn't flood the log for ordinary, non-seam moves.
		sint32 const mapWidth = g_theWorld->GetWidth();
		if(pos.x <= 2 || pos.x >= mapWidth - 2 || prev.x <= 2 || prev.x >= mapWidth - 2)
		{
			AI_DPRINTF(k_DBG_ASTAR, m_owner, -1, m_army.m_id,
			    ("\tWRAP_DIAG: prev(%d,%d) pos(%d,%d) start(%d,%d) dest(%d,%d) mapWidth=%d contPrev=%d/%d contPos=%d/%d transDestCont=%d/%d wrong_cont=%d occupied=%d\n",
			     prev.x, prev.y, pos.x, pos.y, m_start.x, m_start.y, m_dest.x, m_dest.y,
			     mapWidth,
			     g_theWorld->GetContinent(prev).GetLandContinent(), g_theWorld->GetContinent(prev).GetWaterContinent(),
			     cont.GetLandContinent(), cont.GetWaterContinent(),
			     m_transDestCont.GetLandContinent(), m_transDestCont.GetWaterContinent(),
			     wrong_cont, occupied));
		}

		if(occupied || wrong_cont)
		{
			if(  is_land
			   &&
			     (    (g_theWorld->IsWater(prev) && !prevIsTunnel)
			       || (g_theWorld->IsShallowWater(prev) && !prevIsTunnel)
			       || g_theWorld->IsCity(prev)
			     )
			  )
			{
				AI_DPRINTF(k_DBG_ASTAR, m_owner, -1, m_army.m_id,
				    ("\tBLOCK_DIAG: prev(%d,%d) pos(%d,%d) start(%d,%d) dest(%d,%d) occupied=%d wrong_cont=%d checkDest=%d IsCity(pos)=%d IsCity(prev)=%d contPos=%d/%d transDestCont=%d/%d\n",
				     prev.x, prev.y, pos.x, pos.y, m_start.x, m_start.y, m_dest.x, m_dest.y,
				     occupied, wrong_cont, GetCheckDest(),
				     g_theWorld->IsCity(pos), g_theWorld->IsCity(prev),
				     cont.GetLandContinent(), cont.GetWaterContinent(),
				     m_transDestCont.GetLandContinent(), m_transDestCont.GetWaterContinent()));

				cost = k_ASTAR_BIG;
				entry = ASTAR_RETRY_DIRECTION;
				return false;
			}
		}

		if(g_theWorld->IsWater(pos) || g_theWorld->IsShallowWater(pos))
		{
			// Block stepping from land back onto water once already on the
			// target continent, on the assumption that having arrived
			// there, the rest of the way should be on foot - except right
			// at the search's own start: a transport already sitting on
			// the target continent (e.g. in a coastal city) may still need
			// to leave by water for a continent shaped like a near-
			// complete ring around the world with a narrow gap, where the
			// water crossing is dramatically shorter than walking almost
			// the whole way around. Once under way, though, do not allow
			// repeatedly hopping on and off the coast elsewhere along the
			// route - only the very first step gets this exemption.
			bool const prevIsTargetContLand =
			    (   g_theWorld->IsLand(prev)
			     || g_theWorld->IsMountain(prev)
			    )
			 && !g_theWorld->IsWater(prev) // Ocean city and tunnel tiles are both land and sea, just make sure the previous one was not one of those
			 && !g_theWorld->IsShallowWater(prev)
			 &&  g_theWorld->GetContinent(prev) == m_transDestCont;

			// A tunnel tile is water for a ship, but for cargo that's
			// already disembarked it's the land bridge across a strait -
			// crossing one (or a chain of them, since each tile is
			// checked independently as pos advances) while already
			// marching on the target continent isn't "leaving" it, and
			// should cost like a land step below, not get scaled by the
			// ship's speed ratio.
			bool const posIsTargetContTunnel =
			    g_theWorld->IsTunnel(pos)
			 && g_theWorld->GetContinent(pos).GetLandContinent() == m_transDestCont.GetLandContinent();

			AI_DPRINTF(k_DBG_ASTAR, m_owner, -1, m_army.m_id,
			    ("\tLEAVE_DIAG: prev(%d,%d) pos(%d,%d) start(%d,%d) dest(%d,%d) transDestCont=%d/%d contPrev=%d/%d contPos=%d/%d prevIsTargetContLand=%d posIsTargetContTunnel=%d prev!=start=%d IsCity(prev)=%d IsCity(pos)=%d\n",
			     prev.x, prev.y, pos.x, pos.y, m_start.x, m_start.y, m_dest.x, m_dest.y,
			     m_transDestCont.GetLandContinent(), m_transDestCont.GetWaterContinent(),
			     g_theWorld->GetContinent(prev).GetLandContinent(), g_theWorld->GetContinent(prev).GetWaterContinent(),
			     g_theWorld->GetContinent(pos).GetLandContinent(), g_theWorld->GetContinent(pos).GetWaterContinent(),
			     prevIsTargetContLand, posIsTargetContTunnel, (prev != m_start), g_theWorld->IsCity(prev), g_theWorld->IsCity(pos)));

			if(prevIsTargetContLand && prev != m_start && !posIsTargetContTunnel)
			{
				// Return invalid if we leave the target continent
				cost = k_ASTAR_BIG;
				entry = ASTAR_RETRY_DIRECTION;
				return false;
			}

			if (!posIsTargetContTunnel)
			{
				cost *= m_transMaxR;
			}
		}

		if(g_theWorld->IsWater(prev) || g_theWorld->IsShallowWater(prev))
		{
			// A tunnel tile satisfies IsWater() too, so unlike a true coastal
			// landing - where this gate fires exactly once, right as the ship
			// puts cargo ashore - walking a tunnel chain re-triggers it at
			// every single hop, since prev is a tunnel (= water) the whole
			// way. Left unrestricted, that piles up danger cost for foreign
			// presence anywhere along a long corridor, far from the actual
			// target, instead of just discouraging landing too close to it.
			// Only apply it once actually next to the destination.
			bool posNextToDest = true;

			if(prevIsTunnel)
			{
				posNextToDest = false;
				MapPoint neighbor;
				for(sint32 dir = 0; dir <= SOUTH; ++dir)
				{
					if(pos.GetNeighborPosition(WORLD_DIRECTION(dir), neighbor) && neighbor == m_dest)
					{
						posNextToDest = true;
						break;
					}
				}
			}

			if(posNextToDest)
			{
				if
				  (
				       (   // If we land
				           g_theWorld->IsLand(pos)
				        || g_theWorld->IsMountain(pos)
				       )
				    && (
				          ( g_theWorld->IsOccupiedByForeigner  (pos, m_owner) // If the target is a city
				        && !g_theWorld->IsSurroundedByWater    (pos))
				        ||  g_theWorld->IsNextToForeigner(pos, m_owner)
				       )
				  )
				{
					cost += k_MOVE_ISDANGER_COST;
				}
				// If we do not land, just avoid some units next, for instance bombardment units
				else if(g_theWorld->IsNextToForeigner(pos, m_owner))
				{
					cost += k_MOVE_ISDANGER_COST;
				}
			}
		}

		return true;
	}
	else
	{
		cost = k_ASTAR_BIG;
		entry = ASTAR_BLOCKED;
		return false;
	}
}

bool RobotAstar2::AirliftPathCallback (const bool & can_enter,
                                       const MapPoint & prev,
                                       const MapPoint & pos,
                                       const bool & is_zoc,
                                       float & cost,
                                       ASTAR_ENTRY_TYPE & entry )
{
	if (can_enter)
	{
		if
		  (
		        pos == m_dest
		    &&
		   (
		        g_theWorld->GetCell(pos)->GetNumUnits() > 0
		    &&  g_theWorld->GetArmyPtr(pos)->GetOwner() != m_army->GetOwner()
		    && !m_army->CanFight(*g_theWorld->GetArmyPtr(pos))
		   )
		    ||
		   (
		        g_theWorld->HasCity(pos)
		    &&  g_theWorld->GetCity(pos).GetOwner() != m_army->GetOwner()
		    && !m_army->CanAtLeastOneCaptureCity()
		   )
		  )
		{
			bool occupied = (m_army->HasCargo() && !m_army.CanAtLeastOneCargoUnloadAt(pos, false, !m_is_robot));

			if(occupied)
			{
				if(!m_army.CanAtLeastOneCargoUnloadAt(prev, false, !m_is_robot))
				{
					cost = k_ASTAR_BIG;
					entry = ASTAR_RETRY_DIRECTION;
					return false;
				}
			}
		}

		// Protects the transporter itself, not its cargo - unlike a landing
		// preference (m_dest is a single fixed tile, so cost added there
		// can't steer anything), this applies to every other tile still in
		// flight, where genuine alternate routes exist. Mirrors
		// TransportPathCallback's "if we do not land, just avoid some units
		// next" branch, which does the same for a ship still at sea.
		if(pos != m_dest && g_theWorld->IsNextToForeigner(pos, m_owner))
		{
			cost += k_MOVE_ISDANGER_COST;
		}

		return true;
	}
	else
	{
		cost = k_ASTAR_BIG;
		entry = ASTAR_BLOCKED;
		return false;
	}
}

bool RobotAstar2::DefensivePathCallback (const bool & can_enter,
                                         const MapPoint & prev,
                                         const MapPoint & pos,
                                         const bool & is_zoc,
                                         float & cost,
                                         ASTAR_ENTRY_TYPE & entry)
{
	PLAYER_INDEX pos_owner;
	PLAYER_INDEX prev_owner;

	pos_owner = g_theWorld->GetCell(pos)->GetOwner();
	if (!can_enter)
	{
		cost = k_ASTAR_BIG;
		entry = ASTAR_ENTRY_TYPE(0);
		return false;
	}

	if ((pos_owner < 0) || (m_incursionPermission & (0x1 << pos_owner)))
		return true;

	prev_owner = g_theWorld->GetCell(prev)->GetOwner();
	if((prev_owner == pos_owner) &&
	   !(m_incursionPermission & (0x1 << prev_owner)))
	{
		cost += k_MOVE_TREASPASSING_COST;
	}

	return true;
}

bool RobotAstar2::FindPath( const PathType & pathType,
                            const Army & army,
                            const uint32 & army_move_type,
                            const MapPoint & start,
                            const MapPoint & dest,
                            const bool & check_dest,
                            const float & trans_max_r,
                            Path & new_path,
                            float & total_cost,
                            sint32 additionalUnits)
{
	sint32 cutoff = Astar_MaxSearchNodes();

	sint32 nodes_opened = 0;
	bool is_broken_path = false;
	Path bad_path;

	m_pathType = pathType;
	m_transDestCont = g_theWorld->GetContinent(dest);
	m_transMaxR = trans_max_r;

	sint32 nUnits;
	uint32 move_intersection;
	uint32 move_union;
	m_is_robot = true;

	bool isspecial, cancapture, haszoc, canbombard;
	bool isstealth, canthrowparty, canestablishembassy;
	sint32 maxattack, maxdefense;
	army->CharacterizeArmy( isspecial,
	    isstealth,
	    maxattack,
	    maxdefense,
	    cancapture,
	    haszoc,
	    canbombard,
	    canthrowparty,
	    canestablishembassy);

	if(isspecial && maxattack == 0 && !haszoc)
	{
		m_incursionPermission = INCURSION_PERMISSION_ALL;
	}
	else
	{
		m_incursionPermission =
			Diplomat::GetDiplomat(army.GetOwner()).GetIncursionPermission();
	}

	if (army_move_type != 0x0)
	{
		nUnits = 1;
		move_intersection = army_move_type;
		move_union = 0;
		m_army_minmax_move = 300.0;
		m_army_can_expel_stealth = false;
	}
	else
	{
		UnitAstar::InitArmy (army, nUnits, move_intersection, move_union,
			m_army_minmax_move);
	}

	AI_DPRINTF(k_DBG_ASTAR, army.GetOwner(), -1, army.m_id,("\n"));

	// The scheduler matches and moves several times a turn (MAX_MATCH_LIST_CYCLES),
	// and an army that didn't move is searched again from the same tile to the
	// same place: nearly half of all searches. Keep each result while nothing
	// the search reads has changed. The map, vision, hit points and the like
	// bump g_pathCacheEpoch; the army, the request and the owner's diplomacy
	// are the key.
	PLAYER_INDEX const owner = army.GetOwner();
	if (s_keptEpoch != g_pathCacheEpoch || s_keptOwner != owner)
	{
		s_keptPaths.clear();
		s_keptEpoch = g_pathCacheEpoch;
		s_keptOwner = owner;
	}

	std::string key;
	key.reserve(256);
	AddToKey(key, pathType);
	AddToKey(key, army.m_id);
	AddToKey(key, army_move_type);
	AddToKey(key, start);
	AddToKey(key, dest);
	AddToKey(key, check_dest);
	AddToKey(key, trans_max_r);
	AddToKey(key, additionalUnits);
	AddToKey(key, nUnits);
	AddToKey(key, move_intersection);
	AddToKey(key, move_union);
	AddToKey(key, m_army_minmax_move);
	AddToKey(key, m_army_can_expel_stealth);
	AddToKey(key, m_incursionPermission);
	AddToKey(key, g_player[owner]->GetMaskAlliance());
	uint32 carry_flags, carry_cargo_pod;
	GetCarryOver(carry_flags, carry_cargo_pod);
	AddToKey(key, carry_flags);
	AddToKey(key, carry_cargo_pod);
	for (sint32 i = 0; i < army.Num(); i++)
	{
		AddUnitToKey(key, army[i]);
		UnitDynamicArray const * cargo = army[i].GetData()->GetCargoList();
		for (sint32 j = 0; cargo && j < cargo->Num(); j++)
		{
			AddUnitToKey(key, cargo->Get(j));
		}
	}
	Diplomat const & diplomat = Diplomat::GetDiplomat(owner);
	for (PLAYER_INDEX p = 0; p < k_MAX_PLAYERS; p++)
	{
		if (p == owner || !g_player[p]) continue;
		AddToKey(key, p);
		AddToKey(key, diplomat.GetBaseRegard(p));
		AddToKey(key, diplomat.HasWarOrDesiresPreemptivelyWith(p));
		AddToKey(key, AgreementMatrix::s_agreements.TurnsAtWar(owner, p));
	}

	// (start == dest returns before the path is cleared, so it isn't kept)
	KeptPaths::iterator kept = (start == dest) ? s_keptPaths.end() : s_keptPaths.find(key);
	if (kept != s_keptPaths.end())
	{
#ifdef CTP2_PATHCACHE_CHECK
		static unsigned long n_kept = 0, n_different = 0;
		Path  check_path(&new_path);
		Path  check_bad;
		float check_cost = 0.0f;
		bool  check_broken = false;
		bool const check_searched = UnitAstar::FindPath(army, nUnits + additionalUnits, move_intersection,
		    move_union, start, owner, dest, check_path, check_broken, check_bad, check_cost,
		    true, cutoff, nodes_opened, check_dest);
		if (check_searched != kept->second.searched || check_broken != kept->second.is_broken_path
		 || (check_searched && (check_cost != kept->second.total_cost || !SamePath(check_path, kept->second.path))))
		{
			fprintf(stderr, "path cache check: different, army 0x%lx (%d,%d) to (%d,%d), found %d/%d, cost %f/%f\n",
			        army.m_id, start.x, start.y, dest.x, dest.y, check_searched, kept->second.searched,
			        check_cost, kept->second.total_cost);
			++n_different;
		}
		if (++n_kept % 100000 == 0)
		{
			fprintf(stderr, "path cache check: %lu kept, %lu different\n", n_kept, n_different);
		}
#endif
		new_path       = kept->second.path;
		total_cost     = kept->second.total_cost;
		is_broken_path = kept->second.is_broken_path;
		EndKeptSearch();
		return kept->second.searched && !is_broken_path;
	}

	bool const searched = UnitAstar::FindPath(army,
	                        nUnits + additionalUnits,
	                        move_intersection,
	                        move_union,
	                        start,
	                        owner,
	                        dest, new_path,
	                        is_broken_path,
	                        bad_path,
	                        total_cost,
	                        true,
	                        cutoff,
	                        nodes_opened,
	                        check_dest);

	if (start == dest)
	{
		return searched && !is_broken_path;
	}

	KeptPath & entry     = s_keptPaths[key];
	entry.searched       = searched;
	entry.is_broken_path = is_broken_path;
	entry.path           = new_path;
	entry.total_cost     = total_cost;

	return searched && !is_broken_path;
}

bool RobotAstar2::EntryCost( const MapPoint &prev,
                               const MapPoint &pos,
                               float & cost,
                               bool &is_zoc,
                               ASTAR_ENTRY_TYPE &entry )
{
	if(m_pathType == PATH_TYPE_TRANSPORT || m_pathType == PATH_TYPE_AIRLIFT)
	{
		m_isTransporter = true;
	}
	else
	{
		m_isTransporter = false;
	}

	bool r = UnitAstar::EntryCost(prev, pos, cost, is_zoc, entry);

	if (r)
	{
		switch (m_pathType)
		{
		case PATH_TYPE_TRANSPORT:
			r = TransportPathCallback(r, prev, pos, is_zoc, cost, entry);
			break;
		case PATH_TYPE_AIRLIFT:
			r = AirliftPathCallback  (r, prev, pos, is_zoc, cost, entry);
			break;
		case PATH_TYPE_DEFENSIVE:
			r = DefensivePathCallback(r, prev, pos, is_zoc, cost, entry);
			break;
		default:
			break;
		}

		if(m_is_robot && pos != m_army->RetPos() && pos != m_dest && cost < k_ASTAR_BIG)
		{
			if(CheckIsDangerForPos(pos))
			{
				cost      += k_MOVE_ISDANGER_COST;
			}
		}

		AI_DPRINTF(k_DBG_ASTAR, m_owner, -1, m_army.m_id,
		    ("\tCheckEnter, StartPos (%d, %d), DestPos (%d, %d), ThisPos (%d, %d), NextPos (%d, %d), IsZoc: %d, EntryType: %d, Cost: %f\n", m_start.x, m_start.y, m_dest.x, m_dest.y, prev.x, prev.y, pos.x, pos.y, is_zoc, entry, cost));

		if (cost < 1.0)
		{
			cost = 1.0;
		}
		else if ((k_ASTAR_BIG <= cost) && (entry != ASTAR_RETRY_DIRECTION))
		{
			return false;
		}
	}

	return r;
}

void RobotAstar2::RecalcEntryCost(AstarPoint *parent,
    AstarPoint *node, float &new_entry_cost,
    bool &new_is_zoc, ASTAR_ENTRY_TYPE &new_entry)
{
	new_entry = ASTAR_CAN_ENTER;
	UnitAstar::RecalcEntryCost(parent,
							   node,
							   new_entry_cost,
							   new_is_zoc,
							   new_entry);

	if ((new_entry_cost < k_ASTAR_BIG) && (new_entry == ASTAR_CAN_ENTER))
	{
		switch (m_pathType)
		{
			case PATH_TYPE_TRANSPORT:
				TransportPathCallback(true, parent->m_pos, node->m_pos, new_is_zoc,
									  new_entry_cost, new_entry);
				break;
			case PATH_TYPE_AIRLIFT:
				AirliftPathCallback  (true, parent->m_pos, node->m_pos, new_is_zoc,
									  new_entry_cost, new_entry);
				break;
			case PATH_TYPE_DEFENSIVE:
				DefensivePathCallback(true, parent->m_pos, node->m_pos, new_is_zoc,
									  new_entry_cost, new_entry);
				break;
			default:
				break;
		}
	}
}
