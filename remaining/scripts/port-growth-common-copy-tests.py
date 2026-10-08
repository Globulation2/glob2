from pathlib import Path
p=Path('artifacts/resource-growth/remaining/optimized-original-src/src/engine/sim/snapshot/WorldSnapshotTest.cpp');s=p.read_text();r=Path('src/engine/sim/snapshot/WorldSnapshotTest.cpp').read_text()
old='\tTEST_CASE("a single cell change copies one chunk of one component into a reused buffer")'
new='\tTEST_CASE("reused buffers copy sparse chunks and refresh dense changes contiguously")'
end='\tTEST_CASE("a retained consumer leaves the newest free buffer to absorb only later changes")'
a=r.index(new);z=r.index(end,a);x=s.index(old);y=s.index(end,x);p.write_text(s[:x]+r[a:z]+s[y:])
