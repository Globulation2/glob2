from pathlib import Path
import json,gzip,hashlib,shutil,subprocess,platform,xml.etree.ElementTree as E
r=Path.cwd();b=r/'artifacts/resource-growth/cleanup';out=r/'artifacts/resource-growth/published/cleanup';out.mkdir(exist_ok=True)
summary={}
for n in ['engine','golden','unit']:
 cases=list(E.parse(b/(n+'.xml')).getroot().iter('testcase'))
 failed=[c for c in cases if c.find('failure') is not None or c.find('error') is not None];skipped=[c for c in cases if c.find('skipped') is not None]
 summary[n]={'passed':len(cases)-len(failed)-len(skipped),'failed':[(c.get('classname'),c.get('name')) for c in failed],'skipped':len(skipped)}
(b/'test-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();base=subprocess.check_output(['git','rev-parse','origin/master'],text=True).strip()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
freeze={'revision':head,'master':base,'platform':platform.platform(),'compiler':subprocess.check_output(['g++','--version'],text=True),'flags':'release=1 server=0 optimized_assets=0; CCACHE=1; GLOB2_SDL3_PREFIX=/tmp/glob2-sdl3/prefix; GLOB2_RECORDING_PREFIX=/home/bradley/glob2-terrain-art2/build/linux/client/release/recording/prefix','binaries':{str(p.relative_to(r)):sha(p) for p in [r/'build/linux/client/release/src/glob2',r/'build/linux/client/release/test/glob2-engine-tests',r/'build/linux/client/release/test/glob2-unit-tests']},'reference':json.loads((b/'reference-binary.json').read_text())}
previous=json.loads((r/'artifacts/resource-growth/final-master/freeze.json').read_text());freeze['libraries_sha256']={p:sha(Path(p)) for p in previous['libraries_sha256']};(b/'freeze.json').write_text(json.dumps(freeze,indent=2)+'\n')
(b/'README.md').write_text(f'''# Shared-only growth cleanup

Source `{head}`, based on master `{base}`. Master advanced only with checksum-reference repairs; no engine source changed relative to the performance baseline `68aa1075b`.

## Implementation and review

Removed the growth execution-mode CLI option, pipeline placement flag and the executor's OwnerOnly placement extension. The executor now differs from master only in its bounded growth queue capacity. Growth always submits to the same background executor; only zero workers invokes its existing owner fallback. `--compute-threads` counts the owner: 1 means zero workers, and 4 means three workers plus the owner. This corrects the earlier performance report's imprecise phrase “four compute workers”; its actual commands, core reservation and measured numbers are unchanged.

Delay remains 8. Delay setters only validate/assign and neither dispatch nor finish work. Tests cover both reserved and unfinished work, rejected delay changes, real shared execution, the zero-worker fallback and ordered publication with a gated earlier completion fence. There is no simulation/save/protocol change in this cleanup; the feature remains SIM34 / format149 / protocol67 / save floor58.

The independent read-only subagent reviewed the entire PR and cleanup, then signed off `7d35a0c81` with no blocking findings. That signoff includes the corrected lightweight unit-test reset and the source hygiene check. Review findings fixed include removed placement plumbing, side-effect-free delay setters, unused/misplaced includes, clear lifecycle/reference comments and stale original-deposit/owner-mode documentation. Legacy save readers and the immediate ecology/generator reference remain intentionally: they are compatibility/test code, not a second runtime execution mode.

## Validation

{json.dumps(summary,indent=2)}

The known ImageAssets native-SDL 16-bit decoding failure is independent of this change and already reproduced with master’s unchanged fixture without engine code; see the preceding final-master report. No other failure is accepted by this report.

Legacy-load reference traces were generated using the archived **pre-cleanup** executable `61b6ff740` and committed for SIM34. Both zero-worker and shared-worker runs of the cleaned-up executable must match those exact full sidecars, including the retained v108 checkpoint. The old executable and its hash are identified in `reference-binary.json`; generation did not use the new implementation to bless its own output.

All eight fixed 1,024-tick fixtures are additionally compared with pre-cleanup per-tick world/replay traces at compute sizes 1 and 4. `continuation.json` records each result. The updated benchmark runner also verifies delays 1/3/8 with compute sizes 1/2/4/8 on a 32-tick dense fixture. The removed CLI flag must be rejected. Golden verification runs without updating fixtures. Build, exact test commands, logs and JUnit are attached; the initial build was deliberately interrupted after the final API changes, and only `build-final.log` establishes the settled build.

This round is Linux x86-64 GCC15.2 only. No new Windows/macOS/Android/browser/threadless-build or display coverage is claimed. Zero runtime workers on Linux are tested, which is not a threadless-platform build. Existing performance measurements belong to pre-cleanup `61b6ff740`; no fresh throughput claim is made here. Old owner/shared ablations remain historical evidence, not supported commands in this head.
''')
for p in b.rglob('*'):
 if not p.is_file() or p.name=='reference-glob2' or '/profile/' in str(p):continue
 if p.suffix not in {'.json','.jsonl','.xml','.md','.log','.txt','.checksums','.gz'}:continue
 q=out/p.relative_to(b);q.parent.mkdir(parents=True,exist_ok=True)
 if p.suffix in {'.log','.checksums'} and p.stat().st_size>250000:q=Path(str(q)+'.gz');q.write_bytes(gzip.compress(p.read_bytes(),mtime=0))
 else:shutil.copy2(p,q)
for n in ['verify-growth-cleanup.py','package-growth-cleanup.py']:
 q=out/'scripts'/n;q.parent.mkdir(exist_ok=True);shutil.copy2(r/'docs/.work'/n,q)
(out/'SHA256.json').write_text(json.dumps({str(p.relative_to(out)):sha(p) for p in sorted(out.rglob('*')) if p.is_file() and p.name!='SHA256.json'},indent=2)+'\n')
print(head,summary)
