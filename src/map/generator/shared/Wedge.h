// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Geometry.h"
#include "Grid.h"
#include <algorithm>
#include <cmath>
namespace MapGeneration
{
/// The wedge frame: the map divided into one equal wedge per colony round its centre, turned by
/// a random phase. A feature designed in the frame - arc offset across the wedge and radius out
/// from the centre - is stamped into every wedge alike, so whatever the roll, every colony gets
/// the same neighbourhood turned round the centre and the layout is fair for any colony count.
struct WedgeFrame
{
	Torus t;
	int cx, cy;   // the centre tile
	double phase; // where wedge 0 begins, in radians
	int teams;
	double wedge; // each wedge's angle

	WedgeFrame(const Torus &torus, double phase, int teams)
		: t(torus), cx(torus.w / 2), cy(torus.h / 2), phase(phase), teams(teams),
		  wedge(2 * kPi / teams)
	{
	}

	/// A tile seen from the frame.
	struct Cell
	{
		int dx, dy;          // offset from the centre, the short way round
		double d, theta;     // distance and angle from the centre
		double turn;         // angle past the phase, in [0, 2 pi)
		int k;               // which wedge
		double u;            // how far round the wedge, 0 at one edge and 1 at the other
		double s;            // arc offset from the wedge's middle, in tiles
	};
	Cell cell(int x, int y) const
	{
		Cell c;
		c.dx = t.offsetX(cx, x);
		c.dy = t.offsetY(cy, y);
		c.d = std::hypot(double(c.dx), double(c.dy));
		c.theta = std::atan2(double(c.dy), double(c.dx));
		c.turn = std::fmod(c.theta - phase + 4 * kPi, 2 * kPi);
		place(c);
		return c;
	}
	/// Bows the wedge boundaries sideways by `arc` tiles at the cell's radius.
	void bend(Cell &c, double arc) const
	{
		c.turn = std::fmod(c.turn - arc / std::max(1.0, c.d) + 4 * kPi, 2 * kPi);
		place(c);
	}

  private:
	void place(Cell &c) const
	{
		c.k = std::min(teams - 1, int(c.turn / wedge));
		c.u = c.turn / wedge - c.k;
		c.s = c.d * (c.u - 0.5) * wedge;
	}
};

/// A round feature in a wedge's frame: arc offset from the wedge's middle, radius from the
/// centre, a rough outline, stretched and turned. Shapes are evaluated in the wedge's frame too,
/// so every wedge gets the same.
struct Blob
{
	double s, r;
	double stretch, turn;
	RadialShape shape;
	double reach() const { return shape.maximumRadius() * stretch; }
	bool holds(double ds, double dr) const
	{
		// Rotate into the shape's own frame and undo its stretch.
		const double c = std::cos(turn), sn = std::sin(turn);
		const double px = (ds * c + dr * sn) / stretch, py = (-ds * sn + dr * c) * stretch;
		return std::hypot(px, py) < shape.radiusAt(std::atan2(py, px));
	}
};
} // namespace MapGeneration
