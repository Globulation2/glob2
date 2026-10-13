#!/usr/bin/env python3
"""Read-only curation of prior experiments; writes compact evidence, never launches builds/games."""
import hashlib,json,subprocess
from pathlib import Path
from datetime import datetime,timezone
R=Path('/home/bradley/.codex/worktrees')
I=R/'resource-profiles-integration/glob2'; S=R/'scheduler-selection-telemetry/glob2'; C=R/'resource-profile-map-tools/glob2'
OUT=C/'artifacts/engine-optimization-conclusion/pr-notes'
D=OUT/'profiles-derived';D.mkdir(exist_ok=True)
entries=[];derived=[]
def digest(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for b in iter(lambda:f.read(1048576),b''):h.update(b)
 return h.hexdigest()
def add(root,rel,why,scope='source qualified, not performance proof'):
 p=root/rel
 if not p.is_file():raise FileNotFoundError(p)
 if p.stat().st_size>1500000:raise ValueError(('oversized curation',str(p),p.stat().st_size))
 prefix={I:'profiles-foundation-ee45',S:'scheduler-candidate96',C:'final-native-d2'}[root]
 entries.append(dict(source_path=str(p),archive_path='profiles-scheduler/'+prefix+'/'+rel,size_bytes=p.stat().st_size,sha256=digest(p),reason=why,scope=scope))
def emit(name,obj,why):
 p=D/name;p.write_text(json.dumps(obj,indent=2)+'\n')
 entries.append(dict(source_path=str(p),archive_path='profiles-scheduler/derived/'+name,size_bytes=p.stat().st_size,sha256=digest(p),reason=why,scope='DERIVED compact extract, originals retained locally and hash bound'))
 derived.append(str(p));return p

def compact_receipt(p):
 raw=json.loads(p.read_text());v=raw.get('validated',{});v=dict(v)
 t=v.get('compute_telemetry_evidence')
 if t:
  t=dict(t);observed=dict(t.get('observed',{}));streams=observed.pop('trace_streams',None)
  if streams is not None:observed['trace_streams_omitted_count']=len(streams)
  producers=observed.pop('producers',None)
  if producers is not None:observed['producer_detail_omitted_names']=list(producers)
  for name,value in observed.items():
   if isinstance(value,dict) and 'histogram' in value:
    value=dict(value);hist=value.pop('histogram');value['histogram_omitted_bins']=len(hist);observed[name]=value
  t['observed']=observed;v['compute_telemetry_evidence']=t
 return dict(original_path=str(p),original_sha256=digest(p),original_bytes=p.stat().st_size,passed=raw.get('passed'),request_sha256=raw.get('request_sha256'),bindings_sha256=raw.get('bindings_sha256'),binary_sha256=raw.get('binary_sha256'),validated=v,output_sha256=raw.get('output_sha256'),finished_utc=raw.get('finished_utc'),scope=raw.get('scope'),omission='Trace event streams and detailed per-producer/histogram arrays omitted; aggregate loss/open-work/coverage and tick wait metadata preserved. Raw outputs remain local, hashes are commitments rather than uploaded raw proof.')

# Small durable audit and coverage reports; the older coverage matrix is explicitly revision qualified.
add(C,'artifacts/engine-optimization-conclusion/audit-profiles-scheduler.json','Experiment catalogue, actual counts, decisions, missing gates')
add(I,'artifacts/profiles-implementation/profile-foundation-delivery/delivery-current.json','Full source-qualified foundation delivery inventory and limits')
add(I,'artifacts/profiles-implementation/final-proposal-coverage/coverage.json','Full 15-row proposal matrix at the earlier integration revision, with unimplemented flags')
add(I,'artifacts/profiles-implementation/final-proposal-coverage/next-integration-audit.json','Earlier phase overlap/CPU quota and integration gaps')
add(I,'artifacts/profiles-implementation/every-path-integration-audit/review.json','Call-site migration audit, source and exact limitations')
add(I,'artifacts/profiles-implementation/final-proposal-coverage/completion-throughput-pilot/plan.json','Sixteen-attempt follow-up design; zero attempts launched','PREPARED_UNLAUNCHED, no completed-game/throughput result')
for q in sorted((I/'artifacts/profiles-implementation/final-proposal-coverage/completion-throughput-pilot/coordinator-manifests').glob('*.json')):
 add(I,str(q.relative_to(I)),'Frozen same-budget serial/concurrent allocations','PREPARED_UNLAUNCHED')

# Ordinary release builds and exact platform/package proof, never binaries or all source manifests.
for n in ['builds-complete.json','native-release-complete.json','web-release-complete.json','android-release-complete.json','android-apk-complete.json','android-all-binaries.json','apk-static-content.json','completion-assets-proof.json','before-after-comparison.json','execution-plan.json','shader-input-verified.json']:
 add(I,'artifacts/resource-profiles/validation/latest-master-final-builds/'+n,'Actual ee45 release build/package receipt or bound input plan','ee45 source only; Android build/package is not physical-device runtime')
add(S,'artifacts/scheduler-selection-telemetry/build/native-release-complete.json','Actual scheduler candidate release native build')
for n in ['platform-final-summary.json','builds-complete.json','web-release-complete.json','android-release-complete.json','android-apk-complete.json','android-all-binaries.json','apk-static-proof.json','native-preservation-bindings.json','execution-plan.json','shader-input-verified.json']:
 add(S,'artifacts/scheduler-selection-telemetry/platform-builds/'+n,'Actual96 web/Android build and APK compiled-to-packaged-ELF proof','96 source only; unsigned APK build proof, no physical Android runtime')

# Native suite inventories/results retain failures, separate display qualification and replay floor history.
for n in ['final-actual-summary.json','ee45-final-cause-summary.json','inherited-152-comparison.json','unit-run/junit.xml','engine-run/junit.xml','display-x11-retry/final-actual-summary.json','display-x11-retry/unit-run/junit.xml','display-x11-retry/engine-run/junit.xml']:
 add(I,'artifacts/resource-profiles/validation/d80-native-focused/'+n,'Actual native cases and original stale replay/display fixture failures','ee45 source; stale152 acceptance mismatch retained, later intentional floor154 test fix is in final PR')
for n in ['complete.json','final-conformance-summary.json','inherited-152-comparison.json','unit-run/junit.xml','engine-run/junit.xml','unit-display-run/junit.xml','engine-display-run/junit.xml','commands.json']:
 add(S,'artifacts/scheduler-selection-telemetry/focused/'+n,'Actual native executor/gradient/resource/display conformance at96','96 source, old replay152 negative boundary mismatch preserved')
add(I,'artifacts/resource-profiles/validation/final-python/receipt.json','121 Python tooling contract cases; subprocess mocks distinguished')

# Browser receipt, nonzero actual C++ inventories and softwareGPU oracles, original failure plus separate quiet pass.
for root,part in [(I,'artifacts/resource-profiles/validation/final-browser-ee45'),(S,'artifacts/scheduler-selection-telemetry/browser-validation96')]:
 for n in ['complete.json','expected-inventory.json','playwright-results.json','compute-playwright-results.json','result-files-sha256.json']:
  if (root/part/n).exists():add(root,part+'/'+n,'Browser flow/oracle completion, exact case inventory and original failed flows','ee45 or96 as receipt states; SwiftShader correctness, not physical GPU speed')
 for q in sorted((root/part/'results').rglob('actual-junit.xml')):add(root,str(q.relative_to(root)),'Actual79 compiled case executions per browser/variant, six combinations')
 for q in sorted((root/part/'results').rglob('actual-coverage.json')):add(root,str(q.relative_to(root)),'Actual compiled suite count and nonzero execution')
 for q in sorted((root/part/'results').rglob('actual-listing.xml'))[:1]:add(root,str(q.relative_to(root)),'Actual compiled listing, remaining combinations carry same expected inventory')
for n in ['complete.json','plan.json','original-startup-trace-observations.json','playwright-results.json']:
 add(S,'artifacts/scheduler-selection-telemetry/browser96-failed-startup-quiet/'+n,'Separate same-bytes quiet startup pass; original30-second failure remains','96 only; cause of original loading timeout unresolved, no clean12/12 original matrix claim')

# Real game diagnostics, compact per-case equality proofs and exact selected request identities.
for n in ['campaign-complete.json','result-review.json','plan.json','native-argv.txt']:
 add(I,'artifacts/profiles-implementation/runtime-acceptance-d80/'+n,'Actual18 native diagnostic cases;59392 checksums/world records and4096 saved suffix','ee45 exact source; correctness rather than runtime speed')
for n in ['campaign-complete.json','root-review.json']:
 add(I,'artifacts/profiles-implementation/scheduler-telemetry-plan/candidate-runtime18-v2/'+n,'Fresh96 repeat of18 conformance cases','96 exact source; corrected fresh V2, not retroactive acceptance of first attempt')
compact=[]
for role,part in [('ee45','artifacts/profiles-implementation/runtime-acceptance-d80'),('96','artifacts/profiles-implementation/scheduler-telemetry-plan/candidate-runtime18-v2')]:
 for p in sorted((I/part/'runs').glob('*/passed.json')):
  row=compact_receipt(p);row['source_role']=role;row['case_id']=p.parent.name
  request=I/part/'requests'/(p.parent.name+'.json')
  if request.exists():
   req=json.loads(request.read_text());row['request']=req;row['request_file_sha256']=digest(request)
  compact.append(row)
emit('native18-each-compact.json',dict(scope='36 completed source-qualified diagnostic invocations,18configurations repeated on two sources; not36independent hypotheses. Bounded scheduler trace losses recorded, no timing/thermal/currentd2 cross-platform claim.',records=compact),'Compact all36 native diagnostic receipts and requested setup, raw trace event payloads omitted')

p=I/'artifacts/profiles-implementation/scheduler-telemetry-plan'
for n in ['diagnostics-complete.json','plan.json','commands.json']:
 add(I,str((p/n).relative_to(I)),'Actual eight-cell16-child paired conformance or exact cell/request/seed plan','16 actual children; no measured worker-path speed')
emit('eight-cell-native-compact.json',dict(scope='16completed native diagnostic invocations,8cells,2048ticks each; total32768checksum/world records. Correctness not throughput.',records=[compact_receipt(q) for q in sorted((p/'runs').glob('diagnostic-*/passed.json'))]),'Compact all16 paired-cell conformance receipts')
for n in ['independent-result-review.json','slot-smoke.json','runs/two-slot-smoke/passed.json']:
 add(I,'artifacts/profiles-implementation/coordinator-acceptance-ee45/'+n,'Actual two concurrent game jobs, disjoint6logical shares/4participants each and7overlap observations','Actual allocation contract equality, not throughput optimality or RSS cap')
for n in ['complete.json','plan.json','commands.txt','runs/ee45/verdict.json','runs/candidate96da/verdict.json','runs/ee45/checksums.txt','runs/candidate96da/checksums.txt']:
 add(I,'artifacts/resource-profiles/validation/match-regression-ee45-96da/'+n,'Actual golden match verification,842 trace rows each and exact outcomes/replays','Two source-qualified CLI verifications; not currentd2 exported842row runtime proof')

# Exact rejected/non-adopted source patch and actual linked assembly. Small enough to inspect directly.
patch=subprocess.run(['git','diff','ee45c0a0389660bc7d2885b572b94b1a2b01d7a3','96da258e5a6b24a57a8848f2bfa0178031331322','--','src/common/ComputeExecutor.h','src/common/ComputeExecutorHarness.cpp','src/render/terrain/TerrainMaterialsTest.cpp'],cwd=S,text=True,check=True,capture_output=True).stdout
pp=D/'scheduler-ee45-to96.patch';pp.write_text(patch)
entries.append(dict(source_path=str(pp),archive_path='profiles-scheduler/derived/scheduler-ee45-to96.patch',size_bytes=pp.stat().st_size,sha256=digest(pp),reason='Exact complete3file prototype diff; production change is ComputeExecutor.h, extra changes are test fixtures',scope='NOT_ADOPTED; static/correctness proof, zero accepted worker-path timing pairs'))
for n in ['candidate-review.json','candidate-review.txt','priority-fixture-plan.json','priority-discard-all-modes.patch','priority-discard-fixture.cpp','candidate-runtime18/campaign-failure.json','candidate18-plan-drift-review/review-streaming-corrected.json','candidate18-plan-drift-review/scope-change-only-corrected.diff']:
 add(I,str((p/n).relative_to(I)),'Static scheduler review or original failed run/discard fixture','Priority fixture is PREPARED_NOT_EXECUTED; independent static review is not finished adoption')
a=I/'artifacts/scheduler-selection-telemetry/assembly'
for n in ['comparison.json','comparison.txt','extract.py','baseline-linked/binding.json','candidate-linked/binding.json','baseline-linked/worker-0.runtime.asm.txt','baseline-linked/worker-2.runtime.asm.txt','candidate-linked/worker-0.runtime.asm.txt','candidate-linked/worker-2.runtime.asm.txt']:
 add(I,str((a/n).relative_to(I)),'Actual linked Off/Trace worker2→1 selection scans with relocation/TLS qualification','Static codegen only; no runtimegain inference')
for n in ['pairs/small-ni-n1-gpuoff-r-1.json','pairs/small-ni-n1-gpuoff-r0.json','pairs/small-ni-n1-gpuoff-r1.json','timing-campaign-failure.json']:
 add(I,str((p/n).relative_to(I)),'Original owner-only negative controls and failed N12 observer qualification','N1does NOTexecute changed worker path; warm r-1 excluded; failed N12 not accepted')
rows=[]
for q in sorted((p/'timing').glob('timing-small-ni-n1-*/native-row.json')):
 d=json.loads(q.read_text());d.pop('result',None);d['original_file_sha256']=digest(q);d['original_path']=str(q);rows.append(d)
pairs=[]
for rep in [-1,0,1]:
 r={x['variant']:x for x in rows if x['repeat']==rep}
 pairs.append(dict(repeat=rep,warm_excluded=rep==-1,changed_worker_path_exercised=False,percent_changes={k:100*(r['candidate'][k]/r['baseline'][k]-1) for k in ['cpu_s','wall_s','peak_rss_bytes']}))
emit('owner-only-negative-control-rows.json',dict(source_control='ee45c0a0',source_candidate='96da258e',scope='6completed nativechildren: oneexcluded warm pair plus two measured owner-only N1pairs. No workerpath performance experiment complete.',rows=rows,pairs=pairs),'Actual native wait4 wall/user/systemCPU/RSS negative-control rows and recomputed paired percentages')
for n in ['campaign-failure.json','resumed-launch/campaign-failure.json','error-review.json','stop-race-pure-checks.json','observer-fix.diff','plan.json']:
 add(I,'artifacts/profiles-implementation/scheduler-telemetry-timing-v2/'+n,'Timing guard fault, corrected observer, two foreign-build preflight refusals','Two V2launches each0nativechildren; no worker-speed samples or causal foreignbuild claim')
add(I,'artifacts/resource-profiles/validation/timing-observer-first-n12-review/review.json','Detailed original N12 observer scope failure, sample not accepted')

coverage=json.loads((I/'artifacts/profiles-implementation/final-proposal-coverage/coverage.json').read_text())
emit('profile-coverage-current-qualification.json',dict(earlier_matrix_source=str(I/'artifacts/profiles-implementation/final-proposal-coverage/coverage.json'),earlier_matrix_sha256=digest(I/'artifacts/profiles-implementation/final-proposal-coverage/coverage.json'),final_native_source='d2ddb6793777eac9e8c62421f2803313e0b83b52',qualification='Original earlier matrix retained verbatim; supersession below is source-qualified by final PR implementation and native evidence, not rewrite of older proof.',closed_since_earlier_matrix=['Direct Pythonmap profile/softmemory request compatibility and nativefailedgeneration resource metadata are now implemented with18toolcontract tests and9realnativeCLI requests.','Live menu gradientbackend/algorithm/verify central GPUadmission now implemented without resizing frozenpool, native combined ordered/savecontinuation test plus focusedUI/settings cases.'],still_unimplemented_or_unvalidated=['Six tunedprofile tradeoffs: names still callerintent/defaults, profile_tuning_pending true.','Auto initialtier from sustained capability/headroom/memory; hysteresis/cooldowns and live adaptivepresentation.','Visible sixprofile picker and ResetAutomatic.','Aggregate simultaneousphase participant ceiling; CPUquota discovery/enforcement.','Memoryrequiredlive census, recoverable-cache retention actions, RSScap.','Current finald2 crossplatform/browser/Android builds andruntime validation.','Physical low-end/high-end Android anddesktop thermal,battery,input/framepacing,memory acceptance.','Final combinedd2 performance rebenchmark.','Scheduler workerpath timing andadoption.'],prepared_not_executed=['16same-global-budget Playground/Oazis single-completion-versus-concurrent-throughput pilot.','Scheduler Off/Waits/Trace priority-discard fixture.'],unimplemented_hypotheses=['Explicitready queues with correct deadline/order/presentation/GPUcontinuation restrictions.','Owner helps eligible CPUwork whilewaiting, preserving threadcontext restrictions.','New fieldinvalidation/reuse optimization.','Targeted wakeups beyond alreadyrejected conditionalbroadcast prototypes.']), 'Current completion versus earlier coverage matrix; unimplemented concepts explicitly distinct')

# Human-readable qualifications alongside raw receipts.
readme='''Profile and scheduler evidence — compact publication\n\nThis slice is an experiment record, not a new campaign. It contains source-qualified receipts, exact request/cell plans, compact equality extracts, prototype diff, linked assembly, and the actual timing failures. No builds, games or external publication are launched by this extraction.\n\nWhat actually ran:\n* ee45 foundation: ordinary native/web/Android/APK release builds,18native diagnostics(59,392 GCS/world records plus4,096saved-continuation records),2concurrent coordinator jobs with7observed overlap samples, standalone golden match verifier,121mock-qualified Pythoncontracts,12browser flows and79compiled cases repeated across6browser/variant combinations(474executions),36SwiftShader compute oracles.\n*96 scheduler prototype: native/platform builds, fresh repeat of18native diagnostics,16paired-cell diagnostic invocations(32,768 GCS/world records), golden verifier, native suites, six browser C++combination executions. Original browserflow matrix is11passed/1failed; separate same-bytes quiet startup qualification passed. Original30second loading failure cause is unresolved and retained.\n* Timing: ONLY two accepted measured N1owner-only negative-control pairs and an excluded warm pair. N1does not exercise the changed worker code. An N12warmup was excluded after an observer-boundary fault. Two fresh timing launches refused foreign compilation at preflight withZEROnew native samples. Workerpath timing remains UNFINISHED, not a demonstrated gain or regression.\n\nWhat the prototype proves:\nActual shipping-linked assembly reduces explicit selection scans from2to1 under the same uninterrupted mutex. Offcode3486→3395bytes;Trace4111→4137bytes. Static scans,bytes andinstructioncounts are not elapsed/CPU speed measurements. Prototype remains NOTADOPTED and is absent from finalPR1162 source. Complete3file diff included; priority-discard fixture is prepared, not executed.\n\nWhat profiles prove:\nSixnames are validated intent/provenance, NOT six empirically tunedtiers. CPUallocation is ownerinclusive, explicitcaller requests are preserved, renderGPU andcomputeGPU are separate permissions, softmemory is planning rather than anRSScap. The earlier15row coverage matrix is included verbatim and its finalnative gap closures are annotated separately. Do not read source-bound ee45/96browser/Android receipts as verification of finald2additions.\n\nWhat is still untried or incomplete:\nReadyqueue redesign, owner-help duringwaits andnew fieldcache reuse are unimplemented hypotheses. The prepared16attempt same-global-budget completion/throughputpilot was NEVERlaunched. Profilecalibration, auto-tier/hysteresis/livepresentation, visibleprofile/ResetAutomatic controls, aggregatephaseceilings, CPUquotas, ownership-based cacheactions andphysicaldevice acceptance remain unfinished.\n\nPublished evidence limitations:\nPer-case compact receipts preserve actual counts, hash commitments, replay/AIT1/saved suffix equality and telemetry loss flags, but omit potentially huge trace_streams arrays. Full per-tick traces, gigantic387MBplans/fullsource inventories, binaries/APKs, screenshots andprofilerrawdata remain local and are deliberately not included. A hash commitment is not the omitted raw file. This package supports understanding why ideas were kept/rejected/deferred and rebuilding the source-qualified experiment setup; it is not bit-for-bit recovery of all original artifacts.\n'''
rp=D/'README.txt';rp.write_text(readme)
entries.append(dict(source_path=str(rp),archive_path='profiles-scheduler/README.txt',size_bytes=rp.stat().st_size,sha256=digest(rp),reason='Human-readable completed/rejected/unmeasured distinctions',scope='Curated synthesis; original source-qualified files provide supporting receipts'))
sp=Path(__file__).resolve();entries.append(dict(source_path=str(sp),archive_path='profiles-scheduler/derived/profiles-compact-extract.py',size_bytes=sp.stat().st_size,sha256=digest(sp),reason='Reproduce compact extraction from explicitly specified prior originals',scope='Does not rebuild,run games,or publish'))
# Do not duplicate any source path across the allowlist.
assert len({e['source_path'] for e in entries})==len(entries)
assert all(Path(e['source_path']).is_file() and digest(Path(e['source_path']))==e['sha256'] for e in entries)
manifest=dict(created_utc=datetime.now(timezone.utc).isoformat(),purpose='Small evidence allowlist for authorizedPR1162 supplementary evidence branch; no source modifications or native/build/browser runs.',source_scopes={'foundation':'ee45c0a0389660bc7d2885b572b94b1a2b01d7a3','scheduler_experiment':'96da258e5a6b24a57a8848f2bfa0178031331322','final_native_PR':'d2ddb6793777eac9e8c62421f2803313e0b83b52'},status={'scheduler_adopted':False,'accepted_changed_worker_path_timing_pairs':0,'accepted_owner_only_measured_negative_control_pairs':2,'six_profile_tiers_calibrated':False,'physical_android_validated':False,'final_source_crossplatform_validated':False,'prepared_completion_throughput_attempts_executed':0},counts={'files':len(entries),'uncompressed_bytes':sum(e['size_bytes'] for e in entries)},selected_files=[dict(path=e['source_path'],sha256=e['sha256'],bytes=e['size_bytes'],reason=e['reason'],archive_path=e['archive_path'],scope=e['scope']) for e in entries],derived_files=derived,exclusions=['387MBplans','allsource/alltrace/trace_streams inventories','APKs,binaries,objects','fullscreenshots/browsertraces','allrawprofilingdata','wholeoriginalnative/gameartifacts'],publication_warning='Original paths are provenance only, not weblinks. Root should create an archive-relative index and source-qualified PR comment; do not transfer old platform results to finald2 or claim workers were timed.')
mp=OUT/'profiles-curation.json';mp.write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps({'manifest':str(mp),'sha256':digest(mp),'files':len(entries),'uncompressed_bytes':manifest['counts']['uncompressed_bytes'],'largest_bytes':max(e['size_bytes'] for e in entries)}))
