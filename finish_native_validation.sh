#!/bin/bash
set -euo pipefail
cd /Users/bradley/.codex/worktrees/integrity-live-views/glob2
out="$PWD/artifacts/integrity-live-views"
while ! grep -q 'scons: done building targets.' "$out/native-build.log"; do
 if grep -q 'scons: \*\*\*' "$out/native-build.log"; then exit 1; fi
 sleep 2
done
python3 tools/dev_build.py --site-dir=artifacts/integrity-live-views/focused-site --build=artifacts/integrity-live-views/build engine-tests -j4 > "$out/native-final-build.log" 2>&1
GLOB2_BUILD_DIR="$out/build" python3 test/run_tests.py --binary engine --filter 'TeamLiveLists/*' --verbose --junit "$out/team-live-lists.junit.xml" > "$out/team-live-lists.log" 2>&1
python3 tools/dev_build.py --build=artifacts/integrity-live-views/build -j4 > "$out/candidate-game-build.log" 2>&1
cp "$out/build/src/glob2" "$out/candidate-glob2"
cp src/team/Team.cpp "$out/Team.candidate.cpp"
trap 'cp "$out/Team.candidate.cpp" src/team/Team.cpp' EXIT
git show HEAD^:src/team/Team.cpp > src/team/Team.cpp
python3 tools/dev_build.py --build=artifacts/integrity-live-views/build -j4 > "$out/baseline-game-build.log" 2>&1
cp "$out/build/src/glob2" "$out/baseline-glob2"
cp "$out/Team.candidate.cpp" src/team/Team.cpp
trap - EXIT
for stage in 12 24; do
 if [ "$stage" = 12 ]; then save=/Users/bradley/glob2/artifacts/sim-callgrind/midgame/final.game.gz; stop=13153; else save=/Users/bradley/glob2/artifacts/sim-callgrind/midgame24k/final.game.gz; stop=25153; fi
 for variant in baseline candidate; do
  "$out/$variant-glob2" game run --load-game "$save" --compute-threads 8 --gpu off --ticks "$stop" --save final --telemetry checksums --write-replay --output-dir "$out/$variant-$stage" > "$out/$variant-$stage.log" 2>&1
  "$out/$variant-glob2" game run --load-game "$out/$variant-$stage/final.game.gz" --compute-threads 8 --gpu off --ticks "$((stop+128))" --telemetry checksums --output-dir "$out/$variant-$stage-resumed" > "$out/$variant-$stage-resumed.log" 2>&1
 done
done
python3 - <<'PY'
import json,pathlib
p=pathlib.Path('artifacts/integrity-live-views');results=[]
for stage in ['12','24']:
 for suffix in ['', '-resumed']:
  a=p/('baseline-'+stage+suffix);b=p/('candidate-'+stage+suffix)
  x=json.loads((a/'result.json').read_text());y=json.loads((b/'result.json').read_text())
  for k in ['ticks','initialChecksum','finalChecksum']:assert x[k]==y[k],(stage,suffix,k,x[k],y[k])
  traces=[]
  for f in sorted(a.glob('*.checksums')):
   g=b/f.name;assert f.read_bytes()==g.read_bytes(),(stage,suffix,f.name)
   traces.append({'file':f.name,'lines':len(f.read_text().splitlines())})
  assert traces,(stage,suffix,'missing per-tick checksum traces')
  results.append({'stage':stage,'continuation':bool(suffix),'ticks':x['ticks'],'initialChecksum':x['initialChecksum'],'finalChecksum':x['finalChecksum'],'matching_traces':traces})
(p/'equivalence.json').write_text(json.dumps(results,indent=2)+'\n');print(json.dumps(results,indent=2))
PY
python3 tools/dev_build.py --build=artifacts/integrity-live-views/build -j4 > "$out/restored-candidate-build.log" 2>&1
