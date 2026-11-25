//----------------------------------------------------------------------------
//
// Project      : Call To Power 2
// File type    : C++ source
// Description  : Action on the screen
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
// None
//
//----------------------------------------------------------------------------
//
// Modifications from the original Activision code:
//
//----------------------------------------------------------------------------
///
/// \file   Action.cpp
/// \brief  Action on the screen

#include "c3.h"
#include "Action.h"

#include "ActorPath.h"
#include "Anim.h"
#include "UnitActor.h"
#include "maputils.h"
#include "profileDB.h"

static const sint32 k_MAX_UNIT_MOVEMENT_ITERATIONS = 10;
// The director processes actions every 75 ms (DirectorImpl::Process); a unit move lasts as many
// of those as it used to take steps.
static const uint32 k_DIRECTOR_TICK_MS = 75;

Action::Action(
	GROUPTYPE        groupType,
	sint32           actionType,
	Anim           * animation,
	const MapPoint & start,
	const MapPoint & end,
	sint32           facing)
:
	m_groupType        (groupType),
	m_actionType       (actionType),
	m_curAnim          (animation),
	m_curPath          (NULL),
	m_maxActionCounter (0),
	m_curActionCounter (0),
	m_startMapPoint    (start),
	m_endMapPoint      (end),
	m_facing           (facing),
	m_unitsVisibility  (0),
	m_finished         (false),
	m_startTime        (0),
	m_duration         (0),
	m_easeIn           (true),
	m_easeOut          (true)
{
	if (m_startMapPoint != m_endMapPoint) {
		m_curPath = new ActorPath(m_startMapPoint.x, m_startMapPoint.y, m_endMapPoint.x, m_endMapPoint.y);
	}
	m_maxActionCounter = DetermineMaxActionCounter();

	if (m_curPath && m_groupType == GROUPTYPE_UNIT && m_actionType == UNITACTION_MOVE
	    && m_maxActionCounter > 1)
	{
		m_duration  = m_maxActionCounter * k_DIRECTOR_TICK_MS;
		m_startTime = GetTickCount();
	}
}

Action::Action(const Action & a_Original)
:
	m_groupType        (a_Original.m_groupType),
	m_actionType       (a_Original.m_actionType),
	m_curAnim          (NULL),
	m_curPath          (NULL),
	m_maxActionCounter (a_Original.m_maxActionCounter),
	m_curActionCounter (a_Original.m_curActionCounter),
	m_startMapPoint    (a_Original.m_startMapPoint),
	m_endMapPoint      (a_Original.m_endMapPoint),
	m_facing           (a_Original.m_facing),
	m_unitsVisibility  (a_Original.m_unitsVisibility),
	m_finished         (a_Original.m_finished),
	m_startTime        (a_Original.m_startTime),
	m_duration         (a_Original.m_duration),
	m_easeIn           (a_Original.m_easeIn),
	m_easeOut          (a_Original.m_easeOut)
{
	if (a_Original.m_curAnim) {
		m_curAnim = new Anim(*a_Original.m_curAnim);
	}

	/// @todo Check copying of m_curPath (NULLed in original code)
}

Action::~Action()
{
	delete m_curPath;
	delete m_curAnim;
}

void Action::Process()
{
	if (!m_curAnim) {
		m_finished = true;
		return;
	}

	m_curAnim->Process();

	if (IsTimedMove())
	{
		// Finish a tick early: the next tile's move is then set up in time to carry on from
		// the moment this one ends, and the unit's last few milliseconds are drawn from this
		// action after it is finished (UnitActor::m_glideAction).
		m_curActionCounter = m_maxActionCounter;
		if (static_cast<sint32>(GetTickCount() + k_DIRECTOR_TICK_MS - GetMoveEndTime()) >= 0) {
			m_finished = true;
		}
		return;
	}

	if (m_curActionCounter < m_maxActionCounter) {
		m_curActionCounter++;
	} else {
		m_finished = true;
	}
}

void Action::SetMoveTiming(uint32 startTime, bool easeIn, bool easeOut)
{
	m_startTime = startTime;
	m_easeIn    = easeIn;
	m_easeOut   = easeOut;
}

// Fraction of the tile crossed at time now. Constant speed between tiles; a path's first tile
// speeds up from rest and its last slows to rest, each curve meeting the cruising speed.
double Action::MoveProgress(uint32 now) const
{
	sint32 const elapsed = static_cast<sint32>(now - m_startTime);
	if (elapsed <= 0) {
		return 0.0;
	}
	if (elapsed >= static_cast<sint32>(m_duration)) {
		return 1.0;
	}

	double const t = static_cast<double>(elapsed) / m_duration;
	if (m_easeIn && m_easeOut) {
		return t * t * (3.0 - 2.0 * t);
	} else if (m_easeIn) {
		return t * t * (2.0 - t);
	} else if (m_easeOut) {
		double const u = 1.0 - t;
		return 1.0 - u * u * (2.0 - u);
	}
	return t;
}

POINT Action::CalculatePixelXY(const MapPoint & current) const
{
	POINT pos;
	if (m_curPath && IsTimedMove()) {
		static sint32 const k_STEPS = 1 << 16;
		m_curPath->CalcPosition(0, k_STEPS,
		                        static_cast<sint32>(MoveProgress(GetTickCount()) * k_STEPS + 0.5), &pos);
	} else if (m_curPath) {
		m_curPath->CalcPosition(0, m_maxActionCounter, m_curActionCounter, &pos);
	} else {
		maputils_MapXY2PixelXY(current.x, current.y, pos);
	}
	return pos;
}

sint32 Action::CalculateFacing(sint32 facing) const
{
	if (m_curPath) {
		return m_curPath->CalcFacing(0, m_maxActionCounter, m_curActionCounter);
	} else if (m_facing != k_MAX_FACINGS) {
		return m_facing;
	} else {
		return facing;
	}
}

uint16 Action::GetSpriteFrame() const
{
	if (m_curAnim) {
		return m_curAnim->GetCurrentFrame();
	} else {
		return 0;
	}
}

uint16 Action::GetTransparency() const
{
	if (m_curAnim) {
		return m_curAnim->GetCurrentTransparency();
	} else {
		return 15;
	}
}

sint32 Action::DetermineMaxActionCounter()
{
	sint32 result = m_curAnim ? m_curAnim->GetNumFrames() : 0;

	// To ensure that movement always has the same speed it is independent of the number of frames.
	//   exception: effects with movement are processed in parallel so they can have their own speed.
	if (m_startMapPoint != m_endMapPoint && (m_groupType != GROUPTYPE_EFFECT))
	{
		result = k_MAX_UNIT_MOVEMENT_ITERATIONS;
		// unit moves can be sped up or skipped by settings
		if (m_groupType == GROUPTYPE_UNIT && m_actionType == UNITACTION_MOVE)
		{
			if (g_theProfileDB->IsUnitAnim()) {
				result = k_MAX_UNIT_MOVEMENT_ITERATIONS - g_theProfileDB->GetUnitSpeed();
			} else {
				result = 1;
			}
		}
	}

	return result;
}
