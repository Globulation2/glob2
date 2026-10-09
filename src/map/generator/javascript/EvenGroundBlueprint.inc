// SPDX-License-Identifier: GPL-3.0-or-later

#include "ToolkitRecords.h"
#include "ExampleBlueprints.h"
namespace MapGeneration::JavaScript
{
template <> struct Record<Lattice>
{
	static Lattice read(Binding &, JSValueConst)
	{
		throw TypeMismatch("Expected solved lattice handle");
	}
	static void attach(Binding &e, JSValueConst v)
	{
		e.field(v, "w", &Lattice::w);
		e.field(v, "h", &Lattice::h);
		e.field(v, "tiles", &Lattice::tiles);
	}
};
template <> struct Record<Layout>
{
	static std::uint64_t footprint(Binding &e, const Layout &v)
	{
		return sizeof(v) + (e.footprint(v.terrain) - sizeof(v.terrain)) +
			   (e.footprint(v.kind) - sizeof(v.kind)) + (e.footprint(v.pinned) - sizeof(v.pinned)) +
			   (e.footprint(v.cellOf) - sizeof(v.cellOf)) +
			   (e.footprint(v.homeOf) - sizeof(v.homeOf)) + (e.footprint(v.home) - sizeof(v.home)) +
			   (e.footprint(v.failure) - sizeof(v.failure));
	}
	static Layout read(Binding &, JSValueConst)
	{
		throw TypeMismatch("Expected solved layout handle");
	}
	static void attach(Binding &e, JSValueConst v)
	{
		e.field(v, "t", &Layout::t);
		e.field(v, "lat", &Layout::lat);
		e.field(v, "terrain", &Layout::terrain);
		e.field(v, "kind", &Layout::kind);
		e.field(v, "pinned", &Layout::pinned);
		e.field(v, "cellOf", &Layout::cellOf);
		e.field(v, "homeOf", &Layout::homeOf);
		e.field(v, "home", &Layout::home);
		e.field(v, "failure", &Layout::failure);
	}
};
void registerEvenGroundBlueprint(Binding &e, JSValueConst object)
{
	e.add(object, "evenGroundDesign",
		  [&e](JSValueConst, int n, JSValueConst *)
		  {
			  if (n || !e.generation || e.inspecting)
				  throw TypeMismatch("evenGroundDesign()");
			  return e.write(design(e.request, *e.generation));
		  });
	e.add(object, "evenGroundRequestFailure",
		  [&e](JSValueConst, int n, JSValueConst *)
		  {
			  if (n)
				  throw TypeMismatch("evenGroundRequestFailure()");
			  return e.write(latticeFailure(e.request));
		  });
}
} // namespace MapGeneration::JavaScript
