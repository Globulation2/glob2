#!/usr/bin/env python3
"""Compare complete executable scenario traces across downloaded platform artifacts."""
import argparse
from pathlib import Path
from check_javascript import complete_ticks


def compare(root, required=()):
    reports=sorted(root.glob('*/test_headless_workers_and_saved_continuation_match_complete_tick_records/one-worker/game.replay.checksums'))
    # upload-artifact may preserve the cli-smoke/ prefix when multiple paths are uploaded.
    reports+=sorted(root.glob('*/cli-smoke/test_headless_workers_and_saved_continuation_match_complete_tick_records/one-worker/game.replay.checksums'))
    reports=list(dict.fromkeys(reports))
    if len(reports)<2: raise ValueError('at least two platform traces are required')
    for platform in required:
        if not any(platform in str(path.relative_to(root)) for path in reports):
            raise ValueError('missing required platform: '+platform)
    expected=complete_ticks(reports[0].read_bytes())
    if len(expected)!=64: raise ValueError('reference must contain 64 ticks')
    for path in reports[1:]:
        actual=complete_ticks(path.read_bytes())
        if actual!=expected: raise ValueError('complete per-tick simulation differs: '+str(path))
    return reports


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__); parser.add_argument('root',type=Path)
    parser.add_argument('--require-platform',action='append',default=[]); args=parser.parse_args()
    print('Matching complete tick records:',*[str(path) for path in compare(args.root,args.require_platform)],sep='\n')
