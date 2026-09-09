#!/usr/bin/env python3
"""Paired real-map runs for barrier access and defensive placement changes.

Each pair shares a map, seed, opponent and starting position. The saved checkpoint
permits an independent native connectivity audit; telemetry alone must not be
interpreted as proof that a base escaped. These short runs measure regressions
and behaviour, not a statistically established win-rate advantage.
"""
from pathlib import Path
import argparse, concurrent.futures, hashlib, json, shutil, subprocess, time

ROOT = Path(__file__).resolve().parents[1]
MAP_TEAMS = {"Holiday_Island_2":4,"Archipelago":5,"Isles":4,"Migration":4,
             "Garden_3":4,"A_big_pond":3,"Wild_River":6,"Sand_River":6}

def fields(line):
    return dict(item.split("=",1) for item in line.split("\t") if "=" in item)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--baseline",type=Path,required=True)
    p.add_argument("--candidate",type=Path,required=True)
    p.add_argument("--output-dir",type=Path,required=True)
    p.add_argument("--strategy-dir",type=Path,default=ROOT/"data/maxima")
    p.add_argument("--steps",type=int,default=40000)
    p.add_argument("--jobs",type=int,default=2)
    args=p.parse_args();args.output_dir.mkdir(parents=True,exist_ok=True)
    binaries={name:getattr(args,name).resolve() for name in ("baseline","candidate")}
    manifest={"steps":args.steps,"cases":[],"binaries":{
        n:{"path":str(b),"sha256":hashlib.sha256(b.read_bytes()).hexdigest()}
        for n,b in binaries.items()}}
    # The macOS executable changes cwd to its bundle/binary directory. Pin
    # inputs with absolute paths: cwd=ROOT alone does not isolate an experiment
    # from concurrent edits in the compiled-in source directory.
    frozen=args.output_dir.resolve()/"input-strategies"
    frozen.mkdir(parents=True,exist_ok=True)
    for path in sorted(args.strategy_dir.glob("*.strategy")):
        shutil.copy2(path,frozen/path.name)
    manifest["strategy_directory"]=str(frozen)
    inputs={"strategy/"+p.name:p for p in frozen.glob("*.strategy")}
    inputs.update({"maps/"+name+".map":ROOT/"maps"/(name+".map") for name in MAP_TEAMS})
    manifest["input_hashes"]={name:hashlib.sha256(path.read_bytes()).hexdigest()
        for name,path in inputs.items()}
    cases=[(m,s,42 if s==0 else 74241) for m,n in MAP_TEAMS.items() for s in (0,n-1)]
    def run(case):
        m,seat,seed=case;results=[]
        for variant,binary in binaries.items():
            d=args.output_dir/f"{m}-{seat}-{seed}"/variant;d.mkdir(parents=True,exist_ok=True)
            checkpoint=d/"checkpoint.game"
            # Reapply the full frozen base after the automatic format layer,
            # then the frozen duel layer. This cancels any changed automatic
            # layer loaded through the engine's source/bundle search paths.
            command=[str(binary),"-nicowar-telemetry",
                "--maxima-base",str(frozen/"base.strategy"),
                "--maxima-layer",str(frozen/"base.strategy"),
                "--maxima-layer",str(frozen/"duel.strategy"),
                "--maxima-checkpoint-save",str(checkpoint.resolve()),
                str(args.steps),"-nicowar-scenario-match-nox",str((ROOT/"maps"/(m+".map")).resolve()),str(seed),
                "2","7","5","0",str(seat),str(args.steps)]
            start=time.monotonic()
            with (d/"run.log").open("w") as out:
                completed=subprocess.run(command,cwd=ROOT,stdout=out,stderr=subprocess.STDOUT,timeout=300)
            lines=(d/"run.log").read_text(errors="replace").splitlines()
            policy=[fields(l) for l in lines if "\tfarming_policy\t" in l]
            # Position offsets preserve the map's team IDs. Candidate player
            # zero can therefore belong to team 3, 4, or 5, not just team zero.
            candidate_result=next((l.split("\t") for l in lines
                if l.startswith("NICOWAR_SCENARIO_PLAYER_RESULT\tcandidate\t")),None)
            candidate_team=candidate_result[4] if candidate_result else str(seat)
            obs=[fields(l) for l in lines if l.startswith("NICOWAR_OBSERVER_TELEMETRY\t")
                 and l.split("\t")[2]==candidate_team]
            result=dict(map=m,position=seat,seed=seed,variant=variant,command=command,exit=completed.returncode,
                seconds=round(time.monotonic()-start,2),policy_samples=len(policy),
                seed_violations=sum("\tfarming_seed_stability_violation\t" in l for l in lines),
                clearing_samples=sum(int(o.get("clearing_flag_units",0))>0 for o in obs),
                max_towers=max([int(o.get("towers",0)) for o in obs] or [0]),
                population=int(obs[-1].get("population",0)) if obs else None,
                won=int(obs[-1].get("won",0)) if obs else None,
                lost=int(obs[-1].get("lost",0)) if obs else None,
                maximum_policy_us=max([int(o["microseconds"]) for o in policy] or [0]),
                checkpoint=str(checkpoint) if checkpoint.exists() else None,
                log=str(d/"run.log"),completed=candidate_result is not None)
            results.append(result)
        print(json.dumps(results),flush=True)
        return results
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for pair in pool.map(run,cases):
            manifest["cases"].extend(pair)
            (args.output_dir/"results.json").write_text(json.dumps(manifest,indent=2)+"\n")
    return int(any(r["exit"] or not r["completed"] or r["seed_violations"] for r in manifest["cases"]))
if __name__=="__main__":raise SystemExit(main())
