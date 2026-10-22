//----------------------------------------------------------------------------
//
// Project      : Call To Power 2
// File type    : C++ source
// Description  : Continents map generator (see ContinentsGen.h)
//
//----------------------------------------------------------------------------
//
// How a map is made:
// 1. Regions: the map is cut into weighted, warped Voronoi cells, some of them
//    continents and some open ocean. Land is noise plus a bump towards each
//    continent's centre, cut at the profile's land share. Near the border of two
//    continent cells it is always sea, so continents are parted by open ocean.
// 2. Groups: each landmass belongs to the continent whose cell holds most of it.
//    An island near a coast joins that coast's group, others get their own.
//    Shallow water reaches 2-4 tiles from a group's land, and never touches
//    another group's shallow water, so only deep-water ships cross between groups.
// 3. Mountain ranges: per continent, spines as polylines. A fold belt runs along
//    one coast a few tiles inland; interior ranges cross the land; an old range
//    makes hills only. Height is the ridges plus a little noise and distance from
//    the sea; mountains and hills are the highest land by the profile's shares.
// 4. Temperature by latitude and height. Rain: winds blow along the rows,
//    easterly in the tropics and polar band, westerly in between. Air takes up
//    water over the sea, rains a little over land and a lot where the land rises,
//    so the lee of a range is dry. A latitude factor adds the wet equator, the
//    dry subtropics and the stormy mid-latitudes.
// 5. Rivers: a priority flood from the sea gives every land tile a downhill
//    route to water; rain is summed along the routes and the wettest routes are
//    rivers, so they join, reach the sea and are rare in deserts.
// 6. Terrain: rain bands by the profile's desert/plains/grass/forest/swamp
//    shares, changed by temperature as the stock generator does, swamp only on
//    low land by water.
//
//----------------------------------------------------------------------------

#include "ContinentsGen.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>
#include <utility>

namespace ContinentsGen
{

Params::Params()
:
	width      (48),
	height     (96),
	wrapX      (true),
	seed       (1),
	land       (0.3),
	continents (4),
	oceans     (3),
	gap        (6.0),
	mountains  (0.07),
	hills      (0.13),
	desert     (15),
	plains     (20),
	grass      (25),
	forest     (25),
	swamp      (15),
	white      (10),
	brown      (10),
	rivers     (0.07)
{
}

namespace
{

double const PI = 3.14159265358979323846;

class Rng
{
public:
	explicit Rng(uint64_t seed) : m_state(seed) {}

	uint64_t Next()
	{
		uint64_t z = (m_state += 0x9E3779B97F4A7C15ULL);
		z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
		z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
		return z ^ (z >> 31);
	}
	double Unit()                    { return (Next() >> 11) * (1.0 / 9007199254740992.0); }
	double Range(double a, double b) { return a + (b - a) * Unit(); }
	int    Int(int n)                { return static_cast<int>(Next() % static_cast<uint64_t>(n)); }
	bool   Chance(double p)          { return Unit() < p; }

private:
	uint64_t m_state;
};

double Smoothstep(double a, double b, double x)
{
	double t = std::min(1.0, std::max(0.0, (x - a) / (b - a)));
	return t * t * (3 - 2 * t);
}

// Gradient noise, about -1..1; periodic in x when period > 0 (lattice cells)
class Noise
{
public:
	explicit Noise(Rng & rng)
	{
		for (int i = 0; i < 256; ++i)
		{
			m_perm[i] = i;
			double a = rng.Range(0, 2 * PI);
			m_gx[i] = cos(a);
			m_gy[i] = sin(a);
		}
		for (int i = 255; i > 0; --i)
		{
			std::swap(m_perm[i], m_perm[rng.Int(i + 1)]);
		}
		for (int i = 0; i < 256; ++i)
		{
			m_perm[256 + i] = m_perm[i];
		}
	}

	double Get(double x, double y, int period) const
	{
		double fx = floor(x), fy = floor(y);
		int    ix = static_cast<int>(fx), iy = static_cast<int>(fy);
		double rx = x - fx, ry = y - fy;
		double u  = Fade(rx), v = Fade(ry);
		double n00 = Grad(ix,     iy,     period, rx,     ry);
		double n10 = Grad(ix + 1, iy,     period, rx - 1, ry);
		double n01 = Grad(ix,     iy + 1, period, rx,     ry - 1);
		double n11 = Grad(ix + 1, iy + 1, period, rx - 1, ry - 1);
		double a = n00 + u * (n10 - n00);
		double b = n01 + u * (n11 - n01);
		return 1.4 * (a + v * (b - a));
	}

	// Octaves of Get: x, y in screen tiles, cells = lattice cells across the map width
	double Fbm(double x, double y, double width, bool wrap, int cells, int octaves, double gain = 0.5) const
	{
		double sum = 0, amp = 1, norm = 0;
		for (int o = 0; o < octaves; ++o)
		{
			int    c = cells << o;
			double s = c / width;
			sum  += amp * Get(x * s, y * s + 17.3 * o, wrap ? c : 0);
			norm += amp;
			amp  *= gain;
		}
		return sum / norm;
	}

	// One-dimensional, along a spine
	double Line(double s) const { return Get(s, 0.5, 0); }

private:
	static double Fade(double t) { return t * t * t * (t * (t * 6 - 15) + 10); }

	double Grad(int ix, int iy, int period, double dx, double dy) const
	{
		if (period > 0)
		{
			ix = ((ix % period) + period) % period;
		}
		int h = m_perm[(m_perm[ix & 255] + iy) & 255];
		return m_gx[h] * dx + m_gy[h] * dy;
	}

	int    m_perm[512];
	double m_gx[256], m_gy[256];
};

class Grid
{
public:
	Grid(int w, int h, bool wrap) : m_w(w), m_h(h), m_wrap(wrap) {}

	int    Size() const              { return m_w * m_h; }
	int    W() const                 { return m_w; }
	int    H() const                 { return m_h; }
	bool   Wrap() const              { return m_wrap; }
	double SX(int i) const           { int y = i / m_w; return i % m_w + 0.5 * (y & 1); }
	double SY(int i) const           { return 0.5 * (i / m_w); }
	double ScreenHeight() const      { return 0.5 * m_h; }

	// a - b along x, the short way round when the map wraps
	double Dx(double a, double b) const
	{
		double d = a - b;
		if (m_wrap)
		{
			d -= m_w * floor(d / m_w + 0.5);
		}
		return d;
	}

	double Distance(double ax, double ay, double bx, double by) const
	{
		double dx = Dx(ax, bx), dy = ay - by;
		return sqrt(dx * dx + dy * dy);
	}

	// The tile under a screen position, -1 off the map
	int At(double sx, double sy) const
	{
		int y = static_cast<int>(floor(sy * 2 + 0.5));
		if (y < 0 || y >= m_h) return -1;
		int x = static_cast<int>(floor(sx - 0.5 * (y & 1) + 0.5));
		if (m_wrap)
		{
			x = ((x % m_w) + m_w) % m_w;
		}
		else if (x < 0 || x >= m_w)
		{
			return -1;
		}
		return y * m_w + x;
	}

	int Neighbours(int i, int out[8]) const
	{
		int y = i / m_w, x = i % m_w;
		int left = (y & 1) ? x : x - 1;
		int const cand[8][2] =
		{
			{ x - 1, y }, { x + 1, y }, { x, y - 2 }, { x, y + 2 },
			{ left, y - 1 }, { left + 1, y - 1 }, { left, y + 1 }, { left + 1, y + 1 }
		};
		int n = 0;
		for (int k = 0; k < 8; ++k)
		{
			int cx = cand[k][0], cy = cand[k][1];
			if (cy < 0 || cy >= m_h) continue;
			if (cx < 0 || cx >= m_w)
			{
				if (!m_wrap) continue;
				cx = (cx + m_w) % m_w;
			}
			out[n++] = cy * m_w + cx;
		}
		return n;
	}

private:
	int  m_w, m_h;
	bool m_wrap;
};

// Breadth-first distance in moves from the source tiles over the tiles pass() allows
std::vector<int> Distances(Grid const & g, std::vector<int> const & sources,
                           std::function<bool(int)> const & pass, int limit)
{
	std::vector<int> dist(g.Size(), 1 << 20);
	std::queue<int>  q;
	for (size_t k = 0; k < sources.size(); ++k)
	{
		dist[sources[k]] = 0;
		q.push(sources[k]);
	}
	int nb[8];
	while (!q.empty())
	{
		int i = q.front();
		q.pop();
		if (dist[i] >= limit) continue;
		int n = g.Neighbours(i, nb);
		for (int k = 0; k < n; ++k)
		{
			int j = nb[k];
			if (dist[j] > dist[i] + 1 && pass(j))
			{
				dist[j] = dist[i] + 1;
				q.push(j);
			}
		}
	}
	return dist;
}

// Connected components of the tiles where in() holds; -1 elsewhere
int Components(Grid const & g, std::function<bool(int)> const & in, std::vector<int> & comp)
{
	comp.assign(g.Size(), -1);
	int count = 0, nb[8];
	for (int s = 0; s < g.Size(); ++s)
	{
		if (comp[s] >= 0 || !in(s)) continue;
		std::vector<int> stack(1, s);
		comp[s] = count;
		while (!stack.empty())
		{
			int i = stack.back();
			stack.pop_back();
			int n = g.Neighbours(i, nb);
			for (int k = 0; k < n; ++k)
			{
				if (comp[nb[k]] < 0 && in(nb[k]))
				{
					comp[nb[k]] = count;
					stack.push_back(nb[k]);
				}
			}
		}
		++count;
	}
	return count;
}

// The value below which share (0..1) of values lie
double Quantile(std::vector<double> values, double share)
{
	if (values.empty()) return 0;
	share = std::min(1.0, std::max(0.0, share));
	size_t k = std::min(values.size() - 1, static_cast<size_t>(share * values.size()));
	std::nth_element(values.begin(), values.begin() + k, values.end());
	return values[k];
}

struct Region
{
	double x, y, weight;
	double ax, ay, stretch;   // long axis and how much longer than wide
	bool   continent;
};

class Builder
{
public:
	Builder(Params const & p, Map & m)
	:
		m_p     (p),
		m_map   (m),
		m_g     (p.width, p.height, p.wrapX),
		m_rng   (p.seed * 0x2545F4914F6CDD1DULL + 1),
		m_shape (m_rng),
		m_warpX (m_rng),
		m_warpY (m_rng),
		m_isle  (m_rng),
		m_relief(m_rng),
		m_ridge (m_rng),
		m_climate(m_rng),
		m_shelf (m_rng)
	{
	}

	void Run()
	{
		int n = m_g.Size();
		m_map.width  = m_g.W();
		m_map.height = m_g.H();
		m_map.terrain.assign(n, T_DEEP);
		m_map.river.assign(n, 0);
		m_map.group.assign(n, -1);
		m_map.elevation.assign(n, 0);
		m_map.rain.assign(n, 0);
		m_map.temperature.assign(n, 0);
		m_map.ranges.clear();

		PlaceRegions();
		MakeLand();
		MakeGroups();
		MakeRanges();
		MakeHeight();
		MakeTemperature();
		MakeRain();
		MakeRivers();
		MakeTerrain();
		MakeWater();
	}

private:
	bool Land(int i) const { return m_land[i] != 0; }

	// 1. Regions ------------------------------------------------------------

	void PlaceRegions()
	{
		double sw = m_g.W(), sh = m_g.ScreenHeight();
		int    total = m_p.continents + m_p.oceans;
		m_regions.clear();
		for (int r = 0; r < total; ++r)
		{
			bool   continent = r < m_p.continents;
			double best = -1, bx = 0, by = 0;
			// best of several candidates: far from the regions placed so far
			for (int c = 0; c < 24; ++c)
			{
				double x = m_rng.Range(0, sw);
				double y = continent ? m_rng.Range(0.24 * sh, 0.76 * sh)
				                     : m_rng.Range(0.12 * sh, 0.88 * sh);
				double d = 1e9;
				for (size_t k = 0; k < m_regions.size(); ++k)
				{
					d = std::min(d, m_g.Distance(x, y, m_regions[k].x, m_regions[k].y));
				}
				if (!m_g.Wrap())
				{
					d = std::min(d, 1.6 * std::min(x, sw - x));
				}
				if (d > best)
				{
					best = d;
					bx = x;
					by = y;
				}
			}
			Region reg;
			reg.x = bx;
			reg.y = by;
			reg.weight = continent ? m_rng.Range(0.75, 1.3) : m_rng.Range(0.85, 1.15);
			double angle = m_rng.Range(0, PI);
			reg.ax = cos(angle);
			reg.ay = sin(angle);
			reg.stretch = continent ? m_rng.Range(1.0, 1.7) : 1.0;
			reg.continent = continent;
			m_regions.push_back(reg);
		}
	}

	void MakeLand()
	{
		int    n  = m_g.Size();
		double sw = m_g.W(), sh = m_g.ScreenHeight();
		double warp = 0.1 * sw;
		std::vector<double> potential(n);
		m_region.assign(n, 0);
		for (int i = 0; i < n; ++i)
		{
			double x = m_g.SX(i), y = m_g.SY(i);
			double qx = x + warp * m_warpX.Fbm(x, y, sw, m_g.Wrap(), 3, 3);
			double qy = y + warp * m_warpY.Fbm(x, y, sw, m_g.Wrap(), 3, 3);

			int    r1 = 0, r2 = 0;
			double d1 = 1e9, d2 = 1e9;
			for (size_t r = 0; r < m_regions.size(); ++r)
			{
				Region const & rg = m_regions[r];
				double dx = m_g.Dx(qx, rg.x), dy = qy - rg.y;
				double along = dx * rg.ax + dy * rg.ay, across = dy * rg.ax - dx * rg.ay;
				double s = sqrt(rg.stretch);
				double d = sqrt((along / s) * (along / s) + (across * s) * (across * s)) / rg.weight;
				if (d < d1)
				{
					d2 = d1; r2 = r1;
					d1 = d;  r1 = static_cast<int>(r);
				}
				else if (d < d2)
				{
					d2 = d; r2 = static_cast<int>(r);
				}
			}
			m_region[i] = r1;

			double t       = d1 / (d1 + d2);   // 0 at the centre, 0.5 on the border
			double border  = 0.5 * (d2 - d1) * 0.5 * (m_regions[r1].weight + m_regions[r2].weight);
			double lat     = fabs(y - 0.5 * sh) / (0.5 * sh);
			double pot;
			if (m_regions[r1].continent)
			{
				pot = 1.0 - 3.2 * t + 1.1 * m_shape.Fbm(x, y, sw, m_g.Wrap(), 5, 5, 0.55);
				if (m_regions[r2].continent && border < 0.5 * m_p.gap)
				{
					pot = -10;   // the ocean between two continents
				}
			}
			else
			{
				pot = -0.55 + 0.9 * m_isle.Fbm(x, y, sw, m_g.Wrap(), 6, 4);
			}
			pot -= 2.0 * Smoothstep(0.80, 0.97, lat);
			potential[i] = pot;
		}

		double level = Quantile(potential, 1.0 - m_p.land);
		m_land.assign(n, 0);
		for (int i = 0; i < n; ++i)
		{
			m_land[i] = potential[i] > level && potential[i] > -9;
		}

		// fill tiny lakes, leave bigger ones
		std::vector<int> comp;
		int waters = Components(m_g, [this](int i) { return !Land(i); }, comp);
		std::vector<int> size(waters, 0);
		for (int i = 0; i < n; ++i)
		{
			if (comp[i] >= 0) ++size[comp[i]];
		}
		for (int i = 0; i < n; ++i)
		{
			if (comp[i] >= 0 && size[comp[i]] <= 3) m_land[i] = 1;
		}
	}

	// 2. Groups -------------------------------------------------------------

	void MakeGroups()
	{
		int n = m_g.Size();
		std::vector<int> comp;
		int masses = Components(m_g, [this](int i) { return Land(i); }, comp);

		// the region holding most of each landmass
		std::vector<std::vector<int> > votes(masses, std::vector<int>(m_regions.size(), 0));
		std::vector<int> massSize(masses, 0);
		for (int i = 0; i < n; ++i)
		{
			if (comp[i] < 0) continue;
			++votes[comp[i]][m_region[i]];
			++massSize[comp[i]];
		}

		int groups = m_p.continents;
		std::vector<int> massGroup(masses, -1);
		for (int c = 0; c < masses; ++c)
		{
			int best = static_cast<int>(std::max_element(votes[c].begin(), votes[c].end()) - votes[c].begin());
			if (m_regions[best].continent) massGroup[c] = best;
		}

		// islands in open ocean: join a group within reach of shallow water, else a group of their own
		std::vector<int> owner(n, -1);
		for (int i = 0; i < n; ++i)
		{
			if (comp[i] >= 0) owner[i] = massGroup[comp[i]];
		}
		std::vector<int> order;
		for (int c = 0; c < masses; ++c)
		{
			if (massGroup[c] < 0) order.push_back(c);
		}
		std::sort(order.begin(), order.end(), [&massSize](int a, int b) { return massSize[a] > massSize[b]; });
		for (size_t k = 0; k < order.size(); ++k)
		{
			int c = order[k];
			std::vector<int> src;
			for (int i = 0; i < n; ++i)
			{
				if (comp[i] == c) src.push_back(i);
			}
			std::vector<int> d = Distances(m_g, src, [this](int i) { return !Land(i); }, 5);
			int near = -1, best = 1 << 20, nb[8];
			for (int i = 0; i < n; ++i)
			{
				if (owner[i] < 0 || comp[i] == c) continue;
				// a group's land tile next to water this island's shallow water reaches
				int m = m_g.Neighbours(i, nb);
				for (int q = 0; q < m; ++q)
				{
					if (d[nb[q]] + 1 < best)
					{
						best = d[nb[q]] + 1;
						near = owner[i];
					}
				}
			}
			massGroup[c] = (near >= 0 && best <= 4) ? near : groups++;
			for (size_t q = 0; q < src.size(); ++q)
			{
				owner[src[q]] = massGroup[c];
			}
		}

		m_map.groups = groups;
		m_map.groupLand.assign(groups, 0);
		for (int i = 0; i < n; ++i)
		{
			if (owner[i] >= 0)
			{
				m_map.group[i] = static_cast<int16_t>(owner[i]);
				++m_map.groupLand[owner[i]];
			}
		}
	}

	// 3. Mountain ranges ----------------------------------------------------

	struct Axis
	{
		double cx, cy;           // centroid
		double ux, uy, vx, vy;   // long and short axis
		double umin, umax, vmin, vmax;
		std::vector<int> tiles;
	};

	Axis GroupAxis(int group) const
	{
		Axis a;
		for (int i = 0; i < m_g.Size(); ++i)
		{
			if (Land(i) && m_map.group[i] == group) a.tiles.push_back(i);
		}
		// centroid with x unwrapped around the first tile
		double x0 = m_g.SX(a.tiles[0]), sx = 0, sy = 0;
		for (size_t k = 0; k < a.tiles.size(); ++k)
		{
			sx += x0 + m_g.Dx(m_g.SX(a.tiles[k]), x0);
			sy += m_g.SY(a.tiles[k]);
		}
		a.cx = sx / a.tiles.size();
		a.cy = sy / a.tiles.size();
		double xx = 0, xy = 0, yy = 0;
		for (size_t k = 0; k < a.tiles.size(); ++k)
		{
			double dx = m_g.Dx(m_g.SX(a.tiles[k]), a.cx), dy = m_g.SY(a.tiles[k]) - a.cy;
			xx += dx * dx; xy += dx * dy; yy += dy * dy;
		}
		double angle = 0.5 * atan2(2 * xy, xx - yy);
		a.ux = cos(angle); a.uy = sin(angle);
		a.vx = -a.uy;      a.vy = a.ux;
		a.umin = a.vmin = 1e9;
		a.umax = a.vmax = -1e9;
		for (size_t k = 0; k < a.tiles.size(); ++k)
		{
			double dx = m_g.Dx(m_g.SX(a.tiles[k]), a.cx), dy = m_g.SY(a.tiles[k]) - a.cy;
			double u = dx * a.ux + dy * a.uy, v = dx * a.vx + dy * a.vy;
			a.umin = std::min(a.umin, u); a.umax = std::max(a.umax, u);
			a.vmin = std::min(a.vmin, v); a.vmax = std::max(a.vmax, v);
		}
		return a;
	}

	// Along one coast, a few tiles inland, following the coast's shape
	void FoldBelt(Axis const & a, int group)
	{
		double side  = m_rng.Chance(0.5) ? 1 : -1;
		double inset = m_rng.Range(1.6, 2.8);
		std::vector<double> ts, vs;
		for (double t = a.umin; t <= a.umax; t += 0.5)
		{
			double edge = -1e9, other = 1e9;
			for (size_t k = 0; k < a.tiles.size(); ++k)
			{
				double dx = m_g.Dx(m_g.SX(a.tiles[k]), a.cx), dy = m_g.SY(a.tiles[k]) - a.cy;
				double u = dx * a.ux + dy * a.uy;
				if (fabs(u - t) > 1.0) continue;
				double v = side * (dx * a.vx + dy * a.vy);
				edge  = std::max(edge, v);
				other = std::min(other, v);
			}
			if (edge < -1e8 || edge - other < 2.5) continue;   // no land, or too thin for a range
			ts.push_back(t);
			vs.push_back(std::max(edge - inset, 0.5 * (edge + other)));
		}
		if (ts.size() < 6) return;

		// smooth, then trim the ends a little
		std::vector<double> smooth(vs.size());
		for (size_t k = 0; k < vs.size(); ++k)
		{
			double s = 0; int c = 0;
			for (int d = -3; d <= 3; ++d)
			{
				int j = static_cast<int>(k) + d;
				if (j < 0 || j >= static_cast<int>(vs.size())) continue;
				s += vs[j]; ++c;
			}
			smooth[k] = s / c;
		}
		size_t from = static_cast<size_t>(ts.size() * m_rng.Range(0.0, 0.18));
		size_t to   = static_cast<size_t>(ts.size() * m_rng.Range(0.82, 1.0));
		Range r;
		r.group = group;
		r.fold  = true;
		r.old   = false;
		for (size_t k = from; k < to; ++k)
		{
			double v = side * smooth[k];
			r.x.push_back(a.cx + ts[k] * a.ux + v * a.vx);
			r.y.push_back(a.cy + ts[k] * a.uy + v * a.vy);
		}
		if (r.x.size() >= 5) m_map.ranges.push_back(r);
	}

	// Across the land from a point near the middle, bending as it goes
	void Interior(Axis const & a, int group, bool old)
	{
		double px = a.cx + m_rng.Range(-0.3, 0.3) * (a.umax - a.umin) * a.ux + m_rng.Range(-0.25, 0.25) * (a.vmax - a.vmin) * a.vx;
		double py = a.cy + m_rng.Range(-0.3, 0.3) * (a.umax - a.umin) * a.uy + m_rng.Range(-0.25, 0.25) * (a.vmax - a.vmin) * a.vy;
		int start = m_g.At(px, py);
		if (start < 0 || !Land(start) || m_map.group[start] != group) return;

		double axis    = atan2(a.uy, a.ux);
		double heading = m_rng.Chance(0.5) ? axis + m_rng.Range(-0.35, 0.35)
		                                   : axis + (m_rng.Chance(0.5) ? 1 : -1) * m_rng.Range(0.7, 1.57);
		double maxLen  = m_rng.Range(0.35, 0.6) * std::max(a.umax - a.umin, 8.0);
		std::vector<double> xs[2], ys[2];
		for (int half = 0; half < 2; ++half)
		{
			double x = px, y = py, h = heading + half * PI, len = 0, bend = m_rng.Range(-0.06, 0.06);
			int    off = 0;
			while (len < maxLen)
			{
				h += bend + m_rng.Range(-0.12, 0.12);
				x += 0.7 * cos(h);
				y += 0.7 * sin(h);
				len += 0.7;
				int i = m_g.At(x, y);
				if (i < 0) break;
				if (!Land(i) && ++off > 1) break;
				xs[half].push_back(x);
				ys[half].push_back(y);
			}
		}
		Range r;
		r.group = group;
		r.fold  = false;
		r.old   = old;
		for (size_t k = xs[1].size(); k-- > 0; )
		{
			r.x.push_back(xs[1][k]);
			r.y.push_back(ys[1][k]);
		}
		r.x.push_back(px);
		r.y.push_back(py);
		r.x.insert(r.x.end(), xs[0].begin(), xs[0].end());
		r.y.insert(r.y.end(), ys[0].begin(), ys[0].end());
		if (r.x.size() >= 6) m_map.ranges.push_back(r);
	}

	void MakeRanges()
	{
		for (int g = 0; g < m_map.groups; ++g)
		{
			int land = m_map.groupLand[g];
			if (land < 30) continue;
			Axis a = GroupAxis(g);
			if (m_rng.Chance(0.7)) FoldBelt(a, g);
			else                   Interior(a, g, false);
			if (land > 120 && m_rng.Chance(0.75)) Interior(a, g, false);
			if (land > 80 && m_rng.Chance(0.5))   Interior(a, g, true);
			if (land > 300 && m_rng.Chance(0.5))  Interior(a, g, false);
		}
	}

	// Distance from (x, y) to a range's spine, and how far along it the nearest point is
	void SpineDistance(Range const & r, double x, double y, double & dist, double & along, double & length) const
	{
		dist = 1e9; along = 0; length = 0;
		double ax = m_g.Dx(r.x[0], x), ay = r.y[0] - y;
		for (size_t k = 1; k < r.x.size(); ++k)
		{
			double bx = ax + (r.x[k] - r.x[k - 1]), by = ay + (r.y[k] - r.y[k - 1]);
			double ex = bx - ax, ey = by - ay, ll = ex * ex + ey * ey;
			double t  = ll > 0 ? std::min(1.0, std::max(0.0, -(ax * ex + ay * ey) / ll)) : 0;
			double qx = ax + t * ex, qy = ay + t * ey;
			double d  = sqrt(qx * qx + qy * qy);
			double seg = sqrt(ll);
			if (d < dist)
			{
				dist  = d;
				along = length + t * seg;
			}
			length += seg;
			ax = bx; ay = by;
		}
	}

	void MakeHeight()
	{
		int n = m_g.Size();
		double sw = m_g.W();
		std::vector<int> water;
		for (int i = 0; i < n; ++i)
		{
			if (!Land(i)) water.push_back(i);
		}
		m_coast = Distances(m_g, water, [this](int i) { return Land(i); }, 12);

		m_height.assign(n, 0);
		m_oldRange.assign(n, 0);
		std::vector<double> rangeSeed(m_map.ranges.size());
		for (size_t r = 0; r < rangeSeed.size(); ++r) rangeSeed[r] = m_rng.Range(0, 200);

		for (int i = 0; i < n; ++i)
		{
			if (!Land(i)) continue;
			double x = m_g.SX(i), y = m_g.SY(i);
			double base = 0.10 * std::min(m_coast[i], 8) / 8.0
			            + 0.16 * (0.5 + 0.5 * m_relief.Fbm(x, y, sw, m_g.Wrap(), 6, 4));
			double ridge = 0;
			bool   old = false;
			for (size_t r = 0; r < m_map.ranges.size(); ++r)
			{
				Range const & rg = m_map.ranges[r];
				if (rg.group != m_map.group[i]) continue;
				double d, s, len;
				SpineDistance(rg, x, y, d, s, len);
				double width = rg.old ? 1.5 : 1.1;
				if (d > 4 * width) continue;
				double amp = rg.old ? 0.38 : 1.0;
				amp *= 0.6 + 0.4 * m_ridge.Line(rangeSeed[r] + 0.35 * s);
				if (m_ridge.Line(rangeSeed[r] + 77 + 0.22 * s) > 0.45) amp *= 0.35;   // a pass
				amp *= Smoothstep(0, 2.5, s) * Smoothstep(0, 2.5, len - s);
				double core = std::max(0.0, 1 - (d / width) * (d / width));
				double foot = exp(-(d / (2.6 * width)) * (d / (2.6 * width)));
				double h = amp * (0.75 * core + 0.3 * foot)
				         * (0.85 + 0.3 * m_ridge.Fbm(x, y, sw, m_g.Wrap(), 12, 2));
				if (h > ridge)
				{
					ridge = h;
					old   = rg.old;
				}
			}
			m_height[i] = 0.001 + base + ridge;
			m_oldRange[i] = old;
		}

		// mountains and hills: the highest land by the profile's shares; old ranges top out at hills
		std::vector<double> landHeights;
		for (int i = 0; i < n; ++i)
		{
			if (Land(i)) landHeights.push_back(m_height[i]);
		}
		m_mountainLevel = Quantile(landHeights, 1.0 - m_p.mountains);
		m_hillLevel     = Quantile(landHeights, 1.0 - m_p.mountains - m_p.hills);
		double top = *std::max_element(landHeights.begin(), landHeights.end());
		for (int i = 0; i < n; ++i)
		{
			m_map.elevation[i] = static_cast<float>(m_height[i] / top);
		}
	}

	bool Mountain(int i) const { return Land(i) && m_height[i] >= m_mountainLevel && !m_oldRange[i]; }
	bool Hill(int i) const     { return Land(i) && !Mountain(i) && m_height[i] >= m_hillLevel; }

	// 4. Climate ------------------------------------------------------------

	double Latitude(int i) const   // 0 at the equator, 1 at the poles
	{
		double sh = m_g.ScreenHeight();
		return fabs(m_g.SY(i) - 0.5 * (sh - 0.5)) / (0.5 * sh);
	}

	void MakeTemperature()
	{
		int n = m_g.Size();
		double sw = m_g.W();
		m_temp.assign(n, 0);
		for (int i = 0; i < n; ++i)
		{
			double lat = std::min(1.0, Latitude(i));
			double t = 1 - pow(lat, 1.4)
			         + 0.07 * m_climate.Fbm(m_g.SX(i), m_g.SY(i), sw, m_g.Wrap(), 5, 3);
			if (Land(i))
			{
				t -= 0.35 * std::max(0.0, m_height[i] - m_hillLevel) / std::max(1e-6, 1 - m_hillLevel);
			}
			m_temp[i] = t;
			m_map.temperature[i] = static_cast<float>(std::min(1.0, std::max(0.0, t)));
		}
		// shares of land: the poles are mostly sea here, unlike the stock maps
		std::vector<double> landTemp;
		for (int i = 0; i < n; ++i)
		{
			if (Land(i)) landTemp.push_back(m_temp[i]);
		}
		m_whiteLevel = Quantile(landTemp, m_p.white / 100.0);
		m_brownLevel = Quantile(landTemp, 1 - m_p.brown / 100.0);
	}

	// Rain from air moving along each row, dx = +1 (westerlies) or -1 (easterlies)
	void Sweep(int dx, std::vector<double> & rain) const
	{
		int w = m_g.W();
		for (int y = 0; y < m_g.H(); ++y)
		{
			int row = y * w;
			int start = dx > 0 ? 0 : w - 1;
			if (m_g.Wrap())
			{
				// start over sea if the row has any, and go round twice so it's settled
				for (int k = 0; k < w; ++k)
				{
					if (!Land(row + k)) { start = k; break; }
				}
			}
			int    laps = m_g.Wrap() ? 2 : 1;
			double air = 0.6, prev = 0;
			for (int step = 0; step < laps * w; ++step)
			{
				int x = ((start + dx * step) % w + w) % w;
				int i = row + x;
				double hold = 0.35 + 0.65 * std::min(1.0, std::max(0.0, m_temp[i]));
				double r = 0;
				if (!Land(i))
				{
					air += (hold - air) * 0.3;
					prev = 0;
				}
				else
				{
					double rise = std::max(0.0, m_height[i] - prev);
					double part = std::min(0.85, 0.06 + 2.4 * rise);
					r = air * part;
					air -= r * 0.7;   // some comes back from the land
					air = std::min(air, hold);
					prev = m_height[i];
				}
				if (step >= (laps - 1) * w) rain[i] = r;
			}
		}
	}

	void MakeRain()
	{
		int n = m_g.Size();
		std::vector<double> east(n, 0), west(n, 0);
		Sweep(-1, east);
		Sweep(+1, west);
		m_rain.assign(n, 0);
		for (int i = 0; i < n; ++i)
		{
			double deg  = 90 * std::min(1.0, Latitude(i));
			double wW   = Smoothstep(25, 35, deg) * (1 - Smoothstep(55, 65, deg));
			double band = 0.55 + 0.75 * exp(-(deg / 12) * (deg / 12))
			                   - 0.35 * exp(-((deg - 27) / 8) * ((deg - 27) / 8))
			                   + 0.30 * exp(-((deg - 52) / 10) * ((deg - 52) / 10))
			                   - 0.25 * Smoothstep(65, 85, deg);
			m_rain[i] = ((1 - wW) * east[i] + wW * west[i]) * band;
		}
		// rows are one tile apart, so share some rain with the rows around
		int nb[8];
		for (int pass = 0; pass < 2; ++pass)
		{
			std::vector<double> next(m_rain);
			for (int i = 0; i < n; ++i)
			{
				if (!Land(i)) continue;
				double s = 2 * m_rain[i];
				int    c = 2;
				int    m = m_g.Neighbours(i, nb);
				for (int k = 0; k < m; ++k)
				{
					if (Land(nb[k])) { s += m_rain[nb[k]]; ++c; }
				}
				next[i] = s / c;
			}
			m_rain.swap(next);
		}
		// for drawing: rank among land tiles, as the terrain bands use it
		std::vector<std::pair<double, int> > rank;
		for (int i = 0; i < n; ++i)
		{
			if (!Land(i)) continue;
			m_rain[i] += 0.02 * exp(-m_coast[i] / 2.5);
			rank.push_back(std::make_pair(m_rain[i], i));
		}
		std::sort(rank.begin(), rank.end());
		for (size_t k = 0; k < rank.size(); ++k)
		{
			m_map.rain[rank[k].second] = static_cast<float>((k + 0.5) / rank.size());
		}
	}

	// 5. Rivers -------------------------------------------------------------

	void MakeRivers()
	{
		int n = m_g.Size();
		typedef std::pair<double, int> Entry;
		std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry> > open;
		std::vector<double> fill(n, 0);
		std::vector<int>    down(n, -1), order;
		std::vector<char>   done(n, 0);
		for (int i = 0; i < n; ++i)
		{
			if (!Land(i))
			{
				done[i] = 1;
				open.push(Entry(0.0, i));
			}
		}
		int nb[8];
		while (!open.empty())
		{
			Entry e = open.top();
			open.pop();
			int i = e.second;
			if (Land(i)) order.push_back(i);
			int m = m_g.Neighbours(i, nb);
			for (int k = 0; k < m; ++k)
			{
				int j = nb[k];
				if (done[j]) continue;
				done[j] = 1;
				fill[j] = std::max(m_height[j], e.first + 1e-5);
				down[j] = i;
				open.push(Entry(fill[j], j));
			}
		}

		// route each tile to its lowest neighbour on the filled surface, roughened a little
		// so rivers over flat land wander instead of running straight
		double sw = m_g.W();
		std::vector<double> route(n, -1);
		for (int i = 0; i < n; ++i)
		{
			if (Land(i))
			{
				route[i] = fill[i] + 0.04 * (1 + m_climate.Fbm(m_g.SX(i) + 50, m_g.SY(i), sw, m_g.Wrap(), 10, 2));
			}
		}
		for (size_t k = 0; k < order.size(); ++k)
		{
			int i = order[k];
			int best = down[i];
			int m = m_g.Neighbours(i, nb);
			for (int q = 0; q < m; ++q)
			{
				int j = nb[q];
				// only to tiles flooded earlier: no loops, and the sum below sees each tile before its outlet
				if ((!Land(j) || fill[j] < fill[i]) && route[j] < route[best]) best = j;
			}
			down[i] = best;
		}

		std::vector<double> flow(n, 0);
		for (int i = 0; i < n; ++i)
		{
			if (Land(i)) flow[i] = m_rain[i];
		}
		for (size_t k = order.size(); k-- > 0; )
		{
			int i = order[k];
			if (down[i] >= 0 && Land(down[i])) flow[down[i]] += flow[i];
		}

		std::vector<double> landFlow;
		for (int i = 0; i < n; ++i)
		{
			if (Land(i)) landFlow.push_back(flow[i]);
		}
		double level = Quantile(landFlow, 1 - m_p.rivers);
		for (int i = 0; i < n; ++i)
		{
			if (Land(i) && flow[i] >= level && !Mountain(i)) m_map.river[i] = 1;
		}
	}

	// 6. Terrain ------------------------------------------------------------

	void MakeTerrain()
	{
		int n = m_g.Size();
		std::vector<double> landRain;
		for (int i = 0; i < n; ++i)
		{
			if (Land(i)) landRain.push_back(m_rain[i]);
		}
		double total = m_p.desert + m_p.plains + m_p.grass + m_p.forest + m_p.swamp;
		if (total <= 0) total = 1;
		double desertLevel = Quantile(landRain, m_p.desert / total);
		double dryLevel    = Quantile(landRain, 0.5 * m_p.desert / total);
		double plainsLevel = Quantile(landRain, (m_p.desert + m_p.plains) / total);
		double grassLevel  = Quantile(landRain, (m_p.desert + m_p.plains + m_p.grass) / total);
		double forestLevel = Quantile(landRain, (m_p.desert + m_p.plains + m_p.grass + m_p.forest) / total);
		std::vector<double> landHeights;
		for (int i = 0; i < n; ++i)
		{
			if (Land(i)) landHeights.push_back(m_height[i]);
		}
		double lowLevel   = Quantile(landHeights, 0.4);
		double glacierLevel = m_whiteLevel - 0.5 * (m_whiteLevel - *std::min_element(m_temp.begin(), m_temp.end()));

		int nb[8];
		for (int i = 0; i < n; ++i)
		{
			if (!Land(i)) continue;
			double r = m_rain[i], t = m_temp[i];
			bool cold = t <= m_whiteLevel, hot = t >= m_brownLevel;
			int  ter;
			if (Mountain(i))
			{
				ter = cold ? T_WHITE_MOUNTAIN : (hot || r < desertLevel) ? T_BROWN_MOUNTAIN : T_MOUNTAIN;
			}
			else if (Hill(i))
			{
				ter = cold ? T_WHITE_HILL : (hot || r < desertLevel) ? T_BROWN_HILL : T_HILL;
			}
			else
			{
				if      (r < desertLevel) ter = T_DESERT;
				else if (r < plainsLevel) ter = T_PLAINS;
				else if (r < grassLevel)  ter = T_GRASSLAND;
				else if (r < forestLevel) ter = T_FOREST;
				else
				{
					// the wettest band: swamp on low land by the sea or a river, forest elsewhere
					bool byWater = m_coast[i] <= 1 || m_map.river[i];
					int  m = m_g.Neighbours(i, nb);
					for (int k = 0; k < m && !byWater; ++k)
					{
						byWater = m_map.river[nb[k]] != 0;
					}
					ter = (m_height[i] <= lowLevel && byWater) ? T_SWAMP : T_FOREST;
				}

				if (cold)
				{
					if      (ter == T_DESERT || ter == T_PLAINS) ter = t <= glacierLevel ? T_GLACIER : T_TUNDRA;
					else if (ter == T_GRASSLAND || ter == T_SWAMP) ter = T_TUNDRA;
					else if (ter == T_FOREST && t <= glacierLevel) ter = T_TUNDRA;
				}
				else if (hot)
				{
					if      (ter == T_FOREST)    ter = T_JUNGLE;
					else if (ter == T_GRASSLAND) ter = T_PLAINS;
					else if (ter == T_PLAINS)    ter = T_DESERT;
				}
				else if (ter == T_DESERT && r >= dryLevel)
				{
					ter = T_PLAINS;   // only the driest of temperate land stays desert
				}
			}
			m_map.terrain[i] = static_cast<uint8_t>(ter);
		}
	}

	// Shallow water 2-4 tiles out from a group's land; it never touches another group's
	void MakeWater()
	{
		int n = m_g.Size();
		double sw = m_g.W();
		std::vector<std::vector<int> > dist(m_map.groups);
		for (int g = 0; g < m_map.groups; ++g)
		{
			std::vector<int> src;
			for (int i = 0; i < n; ++i)
			{
				if (Land(i) && m_map.group[i] == g) src.push_back(i);
			}
			dist[g] = Distances(m_g, src, [this](int i) { return !Land(i); }, 6);
		}

		// narrow shelves along fold belts, as off the Andes
		std::vector<char> active(n, 0);
		for (size_t r = 0; r < m_map.ranges.size(); ++r)
		{
			Range const & rg = m_map.ranges[r];
			if (!rg.fold) continue;
			for (int i = 0; i < n; ++i)
			{
				if (Land(i)) continue;
				double d, s, len;
				SpineDistance(rg, m_g.SX(i), m_g.SY(i), d, s, len);
				if (d < 5) active[i] = 1;
			}
		}

		std::vector<int> owner(n, -1);
		for (int i = 0; i < n; ++i)
		{
			if (Land(i)) continue;
			int g1 = -1, d1 = 1 << 20;
			for (int g = 0; g < m_map.groups; ++g)
			{
				if (dist[g][i] < d1) { d1 = dist[g][i]; g1 = g; }
			}
			int reach = 2 + static_cast<int>(floor(1.5 + 1.5 * m_shelf.Fbm(m_g.SX(i), m_g.SY(i), sw, m_g.Wrap(), 5, 3)));
			reach = std::min(4, std::max(2, reach));
			if (active[i]) reach = 2;
			if (g1 >= 0 && d1 <= reach) owner[i] = g1;
		}
		// where two groups' shallow water would meet, both sides are deep
		int nb[8];
		std::vector<int> shallow(owner);
		for (int i = 0; i < n; ++i)
		{
			if (owner[i] < 0) continue;
			int m = m_g.Neighbours(i, nb);
			for (int k = 0; k < m; ++k)
			{
				int j = nb[k];
				int other = Land(j) ? m_map.group[j] : owner[j];
				if (other >= 0 && other != owner[i]) shallow[i] = -1;
			}
		}

		for (int i = 0; i < n; ++i)
		{
			if (Land(i)) continue;
			m_map.group[i] = static_cast<int16_t>(shallow[i]);
			if (shallow[i] < 0)
			{
				m_map.terrain[i] = T_DEEP;
				continue;
			}
			bool byLand = false, byDeep = false;
			int  m = m_g.Neighbours(i, nb);
			for (int k = 0; k < m; ++k)
			{
				if (Land(nb[k]))          byLand = true;
				else if (shallow[nb[k]] < 0) byDeep = true;
			}
			int ter = byLand ? T_BEACH : byDeep ? T_SHELF : T_SHALLOW;
			if (ter == T_SHALLOW)
			{
				double v = m_shelf.Fbm(m_g.SX(i) + 31, m_g.SY(i), sw, m_g.Wrap(), 9, 2);
				if      (m_temp[i] >= m_brownLevel - 0.1 && v > 0.25) ter = T_REEF;
				else if (m_temp[i] < m_brownLevel - 0.1 && m_temp[i] > m_whiteLevel && v < -0.3) ter = T_KELP;
			}
			m_map.terrain[i] = static_cast<uint8_t>(ter);
		}
	}

	Params const &      m_p;
	Map &               m_map;
	Grid                m_g;
	Rng                 m_rng;
	Noise               m_shape, m_warpX, m_warpY, m_isle, m_relief, m_ridge, m_climate, m_shelf;
	std::vector<Region> m_regions;
	std::vector<int>    m_region;
	std::vector<char>   m_land, m_oldRange;
	std::vector<int>    m_coast;
	std::vector<double> m_height, m_temp, m_rain;
	double              m_mountainLevel, m_hillLevel, m_whiteLevel, m_brownLevel;
};

} // namespace

void Generate(Params const & params, Map & map)
{
	Builder b(params, map);
	b.Run();
}

} // namespace ContinentsGen
