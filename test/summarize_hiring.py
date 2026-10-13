#!/usr/bin/env python3
"""Combine reference-pass shadow screens without claiming a live gameplay gate."""
import argparse
import json
from pathlib import Path


def identity(config):
    return tuple(config[k] for k in ("K", "R", "L", "policy"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--study", action="append", required=True, help="NAME=HIRING_SHADOW_JSON")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--agreement-target",type=float,default=0.90)
    args = parser.parse_args()
    if not 0<=args.agreement_target<=1:parser.error("agreement target must be within 0..1")
    studies = {}
    for assignment in args.study:
        name, path = assignment.split("=", 1)
        if name in studies:
            parser.error(f"duplicate study: {name}")
        studies[name] = json.loads(Path(path).read_text())
    common = set.intersection(*(set(map(identity, s["configurations"])) for s in studies.values()))
    cases = {}
    for name, study in studies.items():
        configs = [c for c in study["configurations"] if c["K"]+c["R"]-1 <= 8]
        best = max(configs, key=lambda c: (c["coverage"], -c["K"]-c["R"]+1, -c["L"], c["policy"] == "combined"))
        cases[name] = {"observations": study["observations"], "best": best,
                       "planning_elapsed_ns": study["planning_ns"],
                       "peak_accounted_bytes": study["peak_accounted_bytes"]}
        if all("independent_exact_worker_agreement" in c for c in configs):
            cases[name]["best_worker_agreement"]=max(configs,key=lambda c:c["independent_exact_worker_agreement"])
            cases[name]["agreement_controls"]=[c for c in configs if c["L"]==128 and c["policy"]=="split" and (c["K"],c["R"]) in [(0,1),(1,1),(4,4),(8,1)]]
    passed = []
    for key in sorted(common):
        configs = [next(c for c in s["configurations"] if identity(c) == key) for s in studies.values()]
        if key[0]+key[1]-1 <= 8 and all(c["opportunities"] >= 200 and c["hits"]*100 >= c["opportunities"]*95 for c in configs):
            passed.append(dict(zip(("K", "R", "L", "policy"), key)))
    agreed=[]
    for key in sorted(common):
        configs=[next(c for c in s["configurations"] if identity(c)==key) for s in studies.values()]
        if key[0]+key[1]-1<=8 and all(c["opportunities"]>=200 and c.get("independent_exact_worker_agreement",-1)>=args.agreement_target for c in configs):
            agreed.append(dict(zip(("K","R","L","policy"),key)))
    report = {"worker_agreement_target":args.agreement_target,"worker_agreement_passed_configurations":agreed,
              "worker_agreement_semantics":"same worker incarnation at the reference building/job attempt; misses count as disagreement; not full counterfactual job selection","schema_version": 1, "stage": "reference_pass_shadow_screen", "cases": cases,
              "coverage_screen_passed_configurations": passed,
              "coverage_screen_passed": bool(passed),
              "live_trial_approved": False,
              "limitations": ["conservative eligibility follows authoritative prior assignments and quotas",
                              "no counterfactual economy, persistent-exclusion or added-delay proof",
                              "elapsed planner timings include host descheduling; matrix refreshes every tick",
                              "shared-pool and live gates are not established by this screen"]}
    args.output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
