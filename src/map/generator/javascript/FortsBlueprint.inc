// SPDX-License-Identifier: GPL-3.0-or-later

#include "ToolkitRecords.h"
#include "ExampleBlueprints.h"
namespace MapGeneration::JavaScript
{
template <> struct Record<FortDesign>
{
	static FortDesign read(Binding &, JSValueConst)
	{
		throw TypeMismatch("Expected fort design handle");
	}
	static void attach(Binding &e, JSValueConst v)
	{
		e.field(v, "layout", &FortDesign::layout);
		e.add(v, "x",
			  [&e](JSValueConst self, int n, JSValueConst *a)
			  {
				  if (n != 2)
					  throw TypeMismatch("x(u,v)");
				  return e.write(
					  e.native<FortDesign>(self).x(e.read<int>(a[0]), e.read<int>(a[1])));
			  });
		e.add(v, "y",
			  [&e](JSValueConst self, int n, JSValueConst *a)
			  {
				  if (n != 2)
					  throw TypeMismatch("y(u,v)");
				  return e.write(
					  e.native<FortDesign>(self).y(e.read<int>(a[0]), e.read<int>(a[1])));
			  });
	}
};
template <> struct Record<FortPlan>
{
	static FortPlan read(Binding &, JSValueConst)
	{
		throw TypeMismatch("Expected fort plan handle");
	}
	static void attach(Binding &e, JSValueConst v)
	{
		e.field(v, "settleU", &FortPlan::settleU);
		e.field(v, "settleV", &FortPlan::settleV);
		e.field(v, "orchardU", &FortPlan::orchardU);
		e.field(v, "orchardV", &FortPlan::orchardV);
		e.field(v, "orchardDu", &FortPlan::orchardDu);
		e.field(v, "orchardDv", &FortPlan::orchardDv);
	}
};
template <> struct Record<Layout>
{
	static std::uint64_t footprint(Binding &e, const Layout &v)
	{
		return sizeof(v) + (e.footprint(v.terrain) - sizeof(v.terrain)) +
			   (e.footprint(v.homeOf) - sizeof(v.homeOf)) + (e.footprint(v.plot) - sizeof(v.plot)) +
			   (e.footprint(v.uplands) - sizeof(v.uplands)) +
			   (e.footprint(v.wall) - sizeof(v.wall)) + (e.footprint(v.roads) - sizeof(v.roads)) +
			   (e.footprint(v.gates) - sizeof(v.gates)) +
			   (e.footprint(v.buffer) - sizeof(v.buffer)) +
			   (e.footprint(v.towns) - sizeof(v.towns)) + (e.footprint(v.homes) - sizeof(v.homes)) +
			   (e.footprint(v.villages) - sizeof(v.villages)) +
			   (e.footprint(v.failure) - sizeof(v.failure));
	}
	static Layout read(Binding &, JSValueConst)
	{
		throw TypeMismatch("Expected fort layout handle");
	}
	static void attach(Binding &e, JSValueConst v)
	{
		e.field(v, "t", &Layout::t);
		e.field(v, "terrain", &Layout::terrain);
		e.field(v, "homeOf", &Layout::homeOf);
		e.field(v, "plot", &Layout::plot);
		e.field(v, "uplands", &Layout::uplands);
		e.field(v, "wall", &Layout::wall);
		e.field(v, "roads", &Layout::roads);
		e.field(v, "buffer", &Layout::buffer);
		e.field(v, "towns", &Layout::towns);
		e.field(v, "homes", &Layout::homes);
		e.field(v, "villages", &Layout::villages);
		e.field(v, "design", &Layout::design);
		e.field(v, "failure", &Layout::failure);
	}
};
void registerFortsBlueprint(Binding &e, JSValueConst object)
{
	e.add(object, "fortsDesign",
		  [&e](JSValueConst, int n, JSValueConst *)
		  {
			  if (n || !e.generation || e.inspecting)
				  throw TypeMismatch("fortsDesign()");
			  return e.write(design(e.request, *e.generation));
		  });
	e.add(object, "fortsPlan",
		  [&e](JSValueConst, int n, JSValueConst *a)
		  {
			  if (n != 2)
				  throw TypeMismatch("fortsPlan(layout, radius)");
			  auto kind = e.read<int>(a[0]);
			  auto radius = e.read<int>(a[1]);
			  if (kind < 0 || kind >= int(FortLayout::Count) || radius < 20 || radius > 26)
				  throw TypeMismatch("Invalid fort plan");
			  return e.write(fortPlan(FortLayout(kind), radius));
		  });
	e.add(object, "validateForts",
		  [&e](JSValueConst, int n, JSValueConst *)
		  {
			  if (n || !e.game || !e.generation || e.inspecting)
				  throw TypeMismatch("validateForts()");
			  return e.write(validateWorld(*e.game, *e.generation));
		  });
}
} // namespace MapGeneration::JavaScript
