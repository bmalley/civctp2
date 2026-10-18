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

namespace
{
	// REPLAY-<leader> beside the autosaves (CivApp::AutoSave)
	bool GetReplayPath(MBCHAR *fullpath, bool create)
	{
		if (!g_theProfileDB || !g_civPaths) return false;

		MBCHAR leaderName[k_MAX_NAME_LEN];
		strcpy(leaderName, g_theProfileDB->GetLeaderName());
		leaderName[SAVE_LEADER_NAME_SIZE] = '\0';
		c3files_StripSpaces(leaderName);

		MBCHAR path[_MAX_PATH];
		g_civPaths->GetSavePath(C3SAVEDIR_GAME, path);

		sprintf(fullpath, "%s%s%s", path, FILE_SEP, leaderName);
		if (create && !c3files_PathIsValid(fullpath) && !c3files_CreateDirectory(fullpath))
			return false;

		strcat(fullpath, FILE_SEP);
		strcat(fullpath, "REPLAY-");
		strcat(fullpath, leaderName);
		return true;
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

	MBCHAR fullpath[_MAX_PATH];
	if (!GetReplayPath(fullpath, true))
		return;

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

namespace
{
	class Reader
	{
	public:
		Reader(const std::vector<uint8> &data, size_t start)
		:   m_data(data), m_pos(start), m_ok(true) {}

		bool    AtEnd() const   { return m_pos >= m_data.size(); }
		bool    Ok() const      { return m_ok; }

		uint32 U8()
		{
			if (m_pos + 1 > m_data.size()) { m_ok = false; m_pos = m_data.size(); return 0; }
			return m_data[m_pos++];
		}

		uint32 U16()
		{
			uint32 lo = U8();
			return lo | (U8() << 8);
		}

		uint32 U32()
		{
			uint32 lo = U16();
			return lo | (U16() << 16);
		}

		std::string String()
		{
			size_t len = U8();
			if (m_pos + len > m_data.size()) { m_ok = false; m_pos = m_data.size(); return ""; }
			std::string s(m_data.begin() + m_pos, m_data.begin() + m_pos + len);
			m_pos += len;
			return s;
		}

		std::vector<uint8> RLE(size_t expected)
		{
			std::vector<uint8> out;
			size_t len = U32();
			if (m_pos + len > m_data.size() || (len & 1)) { m_ok = false; m_pos = m_data.size(); return out; }
			out.reserve(expected);
			for (size_t i = m_pos; i < m_pos + len; i += 2)
				out.insert(out.end(), m_data[i], m_data[i + 1]);
			m_pos += len;
			if (out.size() != expected) m_ok = false;
			return out;
		}

	private:
		const std::vector<uint8> &  m_data;
		size_t                      m_pos;
		bool                        m_ok;
	};
}

bool replay_LoadCurrent(ReplayGame &result)
{
	MBCHAR fullpath[_MAX_PATH];
	if (!GetReplayPath(fullpath, false))
		return false;

	FILE *file = fopen(fullpath, "rb");
	if (!file) return false;

	std::vector<uint8> data;
	uint8 buf[65536];
	size_t got;
	while ((got = fread(buf, 1, sizeof(buf), file)) > 0)
		data.insert(data.end(), buf, buf + got);
	fclose(file);

	if (data.size() < 8 || memcmp(&data[0], "CTPREPL1", 8) != 0)
		return false;

	// The current map's land mask, to pick out this game
	std::vector<uint8> currentLand;
	if (g_theWorld)
	{
		sint32 const w = g_theWorld->GetXWidth();
		sint32 const h = g_theWorld->GetYHeight();
		currentLand.resize(w * h);
		for (sint32 y = 0; y < h; ++y)
			for (sint32 x = 0; x < w; ++x)
				currentLand[y * w + x] = g_theWorld->IsWater(MapPoint(x, y)) ? 0 : 1;
	}

	// Sessions on the same map are one game (as tools/replay.py)
	std::vector<ReplayGame> games;
	bool sessionStart = false;
	Reader r(data, 8);
	while (!r.AtEnd() && r.Ok())
	{
		uint32 const kind = r.U8();
		if (kind == 'H')
		{
			ReplayGame header;
			header.w = r.U16();
			header.h = r.U16();
			uint32 const wrap = r.U8();
			header.xwrap = (wrap & 1) != 0;
			header.ywrap = (wrap & 2) != 0;
			uint32 const n = r.U8();
			for (uint32 i = 0; i < n; ++i)
			{
				ReplayPlayer player;
				player.r = static_cast<uint8>(r.U8());
				player.g = static_cast<uint8>(r.U8());
				player.b = static_cast<uint8>(r.U8());
				player.name = r.String();
				header.players.push_back(player);
			}
			header.land = r.RLE(header.w * header.h);
			if (!r.Ok()) break;

			sessionStart = true;
			if (games.empty()
			 || games.back().w != header.w || games.back().h != header.h
			 || games.back().land != header.land)
			{
				games.push_back(header);
			}
			else
			{
				games.back().players = header.players;
			}
		}
		else if (kind == 'T' && !games.empty())
		{
			ReplayGame &game = games.back();
			sint32 const round = static_cast<sint32>(r.U32());
			ReplayRound rec;
			std::string const date = r.String();
			for (size_t i = 0; i < date.size(); ++i)
			{
				// The date strings carry formatting bytes; keep the printable part
				if (date[i] >= ' ' && date[i] <= '~') rec.date += date[i];
			}
			size_t const first = rec.date.find_first_not_of(' ');
			rec.date = (first == std::string::npos) ? "" : rec.date.substr(first);
			rec.owners = r.RLE(game.w * game.h);
			uint32 const count = r.U16();
			for (uint32 i = 0; i < count; ++i)
			{
				ReplayCity city;
				city.x = static_cast<uint16>(r.U16());
				city.y = static_cast<uint16>(r.U16());
				city.owner = static_cast<uint8>(r.U8());
				city.size = static_cast<uint8>(r.U8());
				rec.cities.push_back(city);
			}
			if (!r.Ok()) break;

			// A session's first round is where a save was loaded: what was
			// recorded after it belongs to a timeline that was abandoned
			if (sessionStart)
			{
				game.rounds.erase(game.rounds.lower_bound(round), game.rounds.end());
				sessionStart = false;
			}
			game.rounds[round] = rec;
		}
		else
		{
			break;  // corrupt: keep what was read so far
		}
	}

	for (size_t i = games.size(); i-- > 0; )
	{
		if (!games[i].rounds.empty() && games[i].land == currentLand)
		{
			result = games[i];
			return true;
		}
	}
	for (size_t i = games.size(); i-- > 0; )
	{
		if (!games[i].rounds.empty())
		{
			result = games[i];
			return true;
		}
	}
	return false;
}
