from pathlib import Path
p=Path(__file__).resolve().parent;root=p.parents[1]
files=['src/map/gradient/MapGradientBuilding.cpp','src/map/gradient/MapGradientField.cpp']
for f in files:
 path=root/f;s=path.read_text();(p/path.name).write_text(s)
 s='#include "'+str(p/'Profile.h')+'"\n'+s
 if path.name=='MapGradientBuilding.cpp':
  s=s.replace('PERF_SCOPE_TIME(BuildingGradient);','PERF_SCOPE_TIME(BuildingGradient);\n\tGradientProfile::Active profileActive;\n\tGradientProfile::Timer profileInit(GradientProfile::metrics.init);')
  s=s.replace('\n\tif (lazy)\n','\n\tprofileInit.stop();\n\tif (lazy)\n',1)
 else:
  s=s.replace('std::vector<int> &bucket = queue[cur % BUCKETS];','std::vector<int> &bucket = queue[cur % BUCKETS];\n\t\tif (GradientProfile::active) ++GradientProfile::metrics.layers;')
  s=s.replace('pending--;','pending--;\n\t\t\tif (GradientProfile::active) ++GradientProfile::metrics.pops;',1)
  s=s.replace('continue; // stale entry, a cheaper path was found later','{ if (GradientProfile::active) ++GradientProfile::metrics.stale; continue; } // stale entry')
  s=s.replace('destination.push_back((int)n);','GradientProfile::push(destination, (int)n);')
  s=s.replace('buckets[cost % BUCKETS].push_back((int)i);','GradientProfile::push(buckets[cost % BUCKETS], (int)i);')
  s=s.replace('buckets[0].push_back(static_cast<int>(i));','GradientProfile::push(buckets[0], static_cast<int>(i));')
  s=s.replace('static_assert(BucketCount == BUCKETS);','static_assert(BucketCount == BUCKETS);\n\tGradientProfile::Timer profileSetup(GradientProfile::metrics.setup);')
  s=s.replace('// Building fields have only zero-cost seeds, so no deferred seeds are needed.', '''profileSetup.stop();
	++GradientProfile::metrics.builds;
	GradientProfile::metrics.cells += cells;
	if(weighted){++GradientProfile::metrics.weightedBuilds;GradientProfile::metrics.weightedCells+=cells;}
	GradientProfile::Timer profileSeeds(weighted ? GradientProfile::metrics.weightedSeeds : GradientProfile::metrics.seeds);
	// Building fields have only zero-cost seeds, so no deferred seeds are needed.''')
  s=s.replace('PERF_SCOPE_TIME(BuildingGradientResume);','PERF_SCOPE_TIME(BuildingGradientResume);\n\tGradientProfile::Active profileActive;\n\tGradientProfile::Timer profilePropagation(GradientProfile::metrics.propagation);')
 path.write_text(s)
