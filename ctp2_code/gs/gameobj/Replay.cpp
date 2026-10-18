//----------------------------------------------------------------------------
//
// Project      : Call To Power 2
// File type    : C++ source
// Description  : Map timelapse recording
//
//----------------------------------------------------------------------------

#include "c3.h"
#include "Replay.h"

#include <stdio.h>
#include <string.h>
#include <vector>

#include "c3files.h"            // c3files_StripSpaces, c3files_CreateDirectory
#include "citydata.h"
#include "Civilisation.h"
#include "CivPaths.h"           // g_civPaths
#include "colorset.h"           // g_colorSet
#include "Diffcly.h"            // diffutil_GetYearStringFromTurn
#include "gamefile.h"           // SAVE_LEADER_NAME_SIZE
#include "GameSettings.h"       // g_theGameSettings
#include "network.h"            // g_network
#include "player.h"             // g_player
#include "profileDB.h"          // g_theProfileDB
#include "Unit.h"
#include "UnitData.h"
#include "UnitDynArr.h"
#include "World.h"              // g_theWorld

namespace
{
	sint32 s_lastRound = -2;    // last round appended in this session

	void PutU8(std::vector<uint8> &out, uint32 v)
	{
		out.push_back(static_cast<uint8>(v));
	}

	void PutU16(std::vector<uint8> &out, uint32 v)
	{
		PutU8(out, v & 0xff);
		PutU8(out, (v >> 8) & 0xff);
	}

	void PutS32(std::vector<uint8> &out, sint32 v)
	{
		uint32 u = static_cast<uint32>(v);
		PutU16(out, u & 0xffff);
		PutU16(out, u >> 16);
	}

	void PutString(std::vector<uint8> &out, const char *s)
	{
		size_t len = s ? strlen(s) : 0;
		if (len > 255) len = 255;
		PutU8(out, static_cast<uint32>(len));
		out.insert(out.end(), s, s + len);
	}

	// Run-length encoded bytes: u32 encoded length, then (count 1..255, value) pairs
	void PutRLE(std::vector<uint8> &out, const std::vector<uint8> &data)
	{
		std::vector<uint8> rle;
		for (size_t i = 0; i < data.size(); )
		{
			size_t run = 1;
			while (i + run < data.size() && run < 255 && data[i + run] == data[i])
				++run;
			rle.push_back(static_cast<uint8>(run));
			rle.push_back(data[i]);
			i += run;
		}
		PutU16(out, rle.size() & 0xffff);
		PutU16(out, rle.size() >> 16);
		out.insert(out.end(), rle.begin(), rle.end());
	}

	// 'H': map size and wrapping, each player's colour and name, the land mask
	void PutHeader(std::vector<uint8> &out)
	{
		sint32 const w = g_theWorld->GetXWidth();
		sint32 const h = g_theWorld->GetYHeight();

		PutU8(out, 'H');
		PutU16(out, w);
		PutU16(out, h);
		PutU8(out, (g_theWorld->IsXwrap() ? 1 : 0) | (g_theWorld->IsYwrap() ? 2 : 0));
		PutU8(out, k_MAX_PLAYERS);
		for (sint32 p = 0; p < k_MAX_PLAYERS; ++p)
		{
			COLORREF const color = g_colorSet->GetColorRef(g_colorSet->ComputePlayerColor(p));
			PutU8(out, GetRValue(color));
			PutU8(out, GetGValue(color));
			PutU8(out, GetBValue(color));

			MBCHAR name[k_MAX_NAME_LEN] = "";
			if (g_player[p] && g_player[p]->GetCivilisation())
				g_player[p]->GetCivilisation()->GetSingularCivName(name);
			PutString(out, name);
		}

		std::vector<uint8> land(w * h);
		for (sint32 y = 0; y < h; ++y)
			for (sint32 x = 0; x < w; ++x)
				land[y * w + x] = g_theWorld->IsWater(MapPoint(x, y)) ? 0 : 1;
		PutRLE(out, land);
	}

	// 'T': round, date, each tile's owner (255: nobody), the cities
	void PutRound(std::vector<uint8> &out, sint32 round)
	{
		sint32 const w = g_theWorld->GetXWidth();
		sint32 const h = g_theWorld->GetYHeight();

		PutU8(out, 'T');
		PutS32(out, round);
		PutString(out, diffutil_GetYearStringFromTurn(g_theGameSettings->GetDifficulty(), round));

		std::vector<uint8> owners(w * h);
		for (sint32 y = 0; y < h; ++y)
		{
			for (sint32 x = 0; x < w; ++x)
			{
				sint32 const owner = g_theWorld->GetCell(MapPoint(x, y))->GetOwner();
				owners[y * w + x] = (owner >= 0 && owner < k_MAX_PLAYERS) ? owner : 255;
			}
		}
		PutRLE(out, owners);

		std::vector<uint8> cities;
		uint32 count = 0;
		for (sint32 p = 0; p < k_MAX_PLAYERS; ++p)
		{
			if (!g_player[p] || !g_player[p]->m_all_cities) continue;

			for (sint32 c = 0; c < g_player[p]->m_all_cities->Num(); ++c)
			{
				Unit city = g_player[p]->m_all_cities->Access(c);
				MapPoint pos = city.RetPos();
				CityData *data = city.GetData() ? city.GetData()->GetCityData() : NULL;
				sint32 size = data ? data->PopCount() : 0;
				PutU16(cities, pos.x);
				PutU16(cities, pos.y);
				PutU8(cities, p);
				PutU8(cities, size > 255 ? 255 : size);
				++count;
			}
		}
		PutU16(out, count);
		out.insert(out.end(), cities.begin(), cities.end());
	}
}

void replay_RecordRound(sint32 round)
{
	if (!g_theProfileDB || !g_theProfileDB->IsRecordReplay()
	 || !g_theWorld || !g_theGameSettings || !g_civPaths
	 || g_network.IsActive() || g_network.IsNetworkLaunch())
	{
		return;
	}

	// Beside the autosaves (CivApp::AutoSave)
	MBCHAR leaderName[k_MAX_NAME_LEN];
	strcpy(leaderName, g_theProfileDB->GetLeaderName());
	leaderName[SAVE_LEADER_NAME_SIZE] = '\0';
	c3files_StripSpaces(leaderName);

	MBCHAR path[_MAX_PATH];
	g_civPaths->GetSavePath(C3SAVEDIR_GAME, path);

	MBCHAR fullpath[_MAX_PATH];
	sprintf(fullpath, "%s%s%s", path, FILE_SEP, leaderName);
	if (!c3files_PathIsValid(fullpath) && !c3files_CreateDirectory(fullpath))
		return;

	strcat(fullpath, FILE_SEP);
	strcat(fullpath, "REPLAY-");
	strcat(fullpath, leaderName);

	std::vector<uint8> out;
	FILE *file = fopen(fullpath, "rb");
	if (file)
	{
		fclose(file);
	}
	else
	{
		static const char magic[] = "CTPREPL1";
		out.insert(out.end(), magic, magic + 8);
		s_lastRound = -2;
	}

	// A new or loaded game starts a new section with a header. A reader
	// takes sections with the same map as one game and keeps the last record
	// of each round, so playing on from an older save overwrites.
	if (round != s_lastRound + 1)
	{
		PutHeader(out);
	}
	s_lastRound = round;
	PutRound(out, round);

	file = fopen(fullpath, "ab");
	if (!file) return;
	fwrite(&out[0], 1, out.size(), file);
	fclose(file);
}
