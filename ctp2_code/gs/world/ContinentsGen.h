//----------------------------------------------------------------------------
//
// Project      : Call To Power 2
// File type    : C++ header
// Description  : Continents map generator: continents parted by deep ocean,
//                linear mountain ranges, wind-driven rain with rain shadows,
//                rivers that follow the drainage
//
//----------------------------------------------------------------------------
//
// Self-contained: no game headers, so it also builds outside the game for
// looking at maps (ctp/tests/mapgen in the gamedev repo). World::GenerateContinentsMap
// in wldgen.cpp turns its result into cells, goods and huts.
//
// The map is the flat grid of World::GenerateRandMap: w x h, row y shifted half
// a tile right when y is odd, so a tile's screen position is (x + (y & 1) / 2, y / 2).
// Its eight neighbours are (x +- 1, y), (x, y +- 2) and the four tiles of rows y +- 1
// that touch it. The poles are the top and bottom rows; x may wrap.
//
//----------------------------------------------------------------------------

#ifndef CONTINENTS_GEN_H__
#define CONTINENTS_GEN_H__

#include <cstdint>
#include <vector>

namespace ContinentsGen
{

enum Terrain
{
	// water, shallowest last: beach is next to land, shelf is shallow water next to deep
	T_DEEP,
	T_SHELF,
	T_SHALLOW,
	T_KELP,
	T_REEF,
	T_BEACH,
	// land
	T_GLACIER,
	T_TUNDRA,
	T_DESERT,
	T_PLAINS,
	T_GRASSLAND,
	T_FOREST,
	T_JUNGLE,
	T_SWAMP,
	T_HILL,
	T_BROWN_HILL,
	T_WHITE_HILL,
	T_MOUNTAIN,
	T_BROWN_MOUNTAIN,
	T_WHITE_MOUNTAIN,
	T_COUNT
};

inline bool IsWater(int t)    { return t <= T_BEACH; }
inline bool IsShallow(int t)  { return t > T_DEEP && t <= T_BEACH; }

struct Params
{
	int      width, height;   // flat grid
	bool     wrapX;
	uint64_t seed;
	double   land;            // share of tiles that are land, 0..1
	int      continents;      // continents parted by deep ocean
	int      oceans;          // open-ocean regions between them (islands only)
	double   gap;             // least ocean between two continents, in tiles
	double   mountains;       // share of land that is mountain
	double   hills;           // share of land that is hill
	// shares of land by wetness, percent, driest first (the profile's sliders)
	int      desert, plains, grass, forest, swamp;
	// shares of all tiles that are cold (white) and hot (brown), percent
	int      white, brown;
	double   rivers;          // share of land with a river

	Params();
};

struct Range
{
	std::vector<double> x, y;  // spine, screen coordinates
	int      group;
	bool     fold;             // along a coast, with a narrow shelf off it
	bool     old;              // worn down: hills, no peaks
};

struct Map
{
	int                   width, height;
	std::vector<uint8_t>  terrain;     // Terrain, index y * width + x
	std::vector<uint8_t>  river;
	// Which continent (landmass group) a tile belongs to: land, and the shallow water
	// around it. Islands near a coast join its group; -1 = deep ocean.
	std::vector<int16_t>  group;
	std::vector<float>    elevation, rain, temperature;   // 0..1, for drawing and checks
	int                   groups;
	std::vector<int>      groupLand;   // land tiles in each group
	std::vector<Range>    ranges;
};

void Generate(Params const & params, Map & map);

} // namespace ContinentsGen

#endif
