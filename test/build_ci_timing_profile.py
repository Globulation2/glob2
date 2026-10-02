#!/usr/bin/env python3
"""Build a reviewable profile from successful, platform-matched observations."""
import argparse
import json
from pathlib import Path
import statistics


def build(documents, family, auxiliary, minimum=10):
    samples={}
    for doc in documents:
        if doc.get('family') == family:
            for label,seconds in doc.get('seconds',{}).items():
                if isinstance(seconds,(int,float)) and seconds>0:samples.setdefault(label,[]).append(seconds)
        elif doc.get('conclusion')=='success':
            by_name={v['step']:key for key,v in auxiliary.items()}
            for job in doc.get('jobs',[]):
                if family not in job['name']:continue
                for step in job.get('steps',[]):
                    if step.get('conclusion')=='success' and step['name'] in by_name and isinstance(step.get('seconds'),(int,float)) and step['seconds']>0:
                        samples.setdefault(by_name[step['name']],[]).append(step['seconds'])
    return {'schema':1,'family':family,'seconds':{k:statistics.median(v) for k,v in sorted(samples.items()) if len(v)>=minimum}}


def main():
    p=argparse.ArgumentParser();p.add_argument('observations',type=Path);p.add_argument('--family',required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();docs=[json.loads(path.read_text()) for path in a.observations.rglob('*.json')]
    auxiliary=json.loads((Path(__file__).parent/'ci-native-auxiliary.json').read_text())
    result=build(docs,a.family,auxiliary)
    if not result['seconds']:raise SystemExit('No platform-matched jobs with ten successful samples; keep the existing profile.')
    a.output.write_text(json.dumps(result,indent=2)+'\n')
if __name__=='__main__':main()
