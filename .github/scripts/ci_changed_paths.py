#!/usr/bin/env python3
"""Select predictive PR checks, explicit overrides and full master verification."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys


JOBS = ("native", "browser", "map_generators", "deployment", "cross_platform", "platform", "platform_stack", "music")
# Implementation-only drawing changes retain native, browser, and equivalence
# checks. Shared headers, file I/O, fonts and unknown library paths stay full CI.
RENDER_IMPLEMENTATIONS = {
    "libs/gag/src/RenderBackend.cpp",
    "libs/gag/src/SoftwareRenderBackend.cpp",
    "libs/gag/src/SurfaceRaster.cpp",
}
CI_TOOL_TESTS = {
    "test/test_run_tests.py",
    "test/test_ci_failure_aggregation.py",
    "test/build_system/test_ci_changed_paths.py",
    "test/build_system/test_ci_tiers.py",
    "test/build_system/test_ci_run_metrics.py",
    "test/build_system/test_ci_concurrency.py",
}
# Outside the drawing and AI directories before they gained these files: every job.
FULL_PATHS = ("src/render/scene/", "src/ai/AIThreading.h", "src/hud/AllyTeamWidgetIndex.h")


def platform_stack_changed(paths):
    from ci_policy import PLATFORM_STACK_PATHS, cheap_path, is_test_source
    return any(path.startswith(PLATFORM_STACK_PATHS) and not cheap_path(path) and not is_test_source(path) for path in paths)


TRANSPORT_TESTS = {
    "src/net/NetConnectionHarness.cpp",
    "test/NativeMultiplayerPeer.cpp",
    "src/net/WssTransportHarness.cpp",
    "src/net/WssListenerHarness.cpp",
    "src/net/LANDiscoveryHarness.cpp",
    "test/run-network-transport-tests.py",
}


def classify(paths):
    if not paths:
        return {job: True for job in JOBS}

    from ci_policy import MUSIC_TOOL_PATHS, PRESENTATION, TOOLING_TESTS, cheap_path, is_test_source, music_set_path, unclassified
    native = browser = map_generators = deployment = cross_platform = platform = music = False
    platform_stack = platform_stack_changed(paths)
    for path in paths:
        # These Python suites execute directly in the selector job, without
        # compiling a client or launching platform/browser regressions.
        if path in CI_TOOL_TESTS or cheap_path(path):
            continue
        if path.startswith("docs/") or path.endswith(".md"):
            continue
        if path.startswith("services/online/"):
            # Selected by ci_policy's platform flag; protocol fixtures also feed
            # the C++ contract tests.
            platform = True
            native = native or path.startswith("services/online/packages/protocol/fixtures/")
            continue
        if path.startswith(MUSIC_TOOL_PATHS):
            music = True
            continue
        if music_set_path(path):
            # Soundtrack set data: packaged by the native and browser builds.
            native = browser = True
            continue
        if path == "test/map-generator-golden.txt":
            map_generators = True
            continue
        if path in TRANSPORT_TESTS:
            browser = True
            continue
        if path.startswith(("test/fixtures/javascript/", "test/fixtures/unit-catalog/", "test/maxima/fixtures/save-continuation/", "test/support/ScriptCorpus")) or is_test_source(path) and (Path(path).name.startswith("Script") or Path(path).name in {'UnitCatalogTest.cpp', 'UnitCustomizationTest.cpp'}) or path in {
            "test/check_javascript.py", "test/check_javascript_corpus.py", "test/check_javascript_evidence.py",
            "test/check_telemetry_simulation.py", "test/test_javascript_evidence.py",
            "test/maxima/check_save_continuation_fixture.py",
            "test/fixtures/resources/seeded-compositions-units.trace",
            "test/build_provenance.py", "test/support/TestMain.cpp",
            "libs/gag/src/ImageAssetTest.cpp",
        }:
            # These cases and fixtures are compiled/executed in the production
            # WebAssembly harness too; native-only CI would leave that boundary untested.
            native = browser = cross_platform = True
            continue
        if path.startswith("test/fixtures/multiplayer/"):
            # The committed match record is verified natively and in every browser,
            # and the comparison job requires identical traces.
            native = browser = cross_platform = True
            continue
        if path.startswith("platforms/web/") and browser_only(path):
            browser = True
            continue
        if path.startswith("deploy/") or path.startswith("test/deployment/"):
            browser = True
            deployment = True
            continue
        if path.startswith(("test/transport/",)):
            browser = True
            continue
        # The match relay builds and runs in the native-programs job only.
        if path.startswith(("src/relay/", "test/relay_service/", "test/fixtures/relay-tickets/")):
            browser = True
            continue
        if (path.startswith("test/") and not path.startswith(TOOLING_TESTS) or is_test_source(path)) and path not in {
            "test/run-browser-determinism.py",
        } and not Path(path).name.startswith("MapGenerator"):
            native = True
            continue
        if unclassified(path) or is_test_source(path) or path.startswith(FULL_PATHS):
            return {**{job: True for job in JOBS}, "platform_stack": platform_stack}
        if path in RENDER_IMPLEMENTATIONS or path.startswith(("src/ai/",) + PRESENTATION):
            native = browser = cross_platform = True
            continue
        if path.startswith(("src/net/", "src/yog/")):
            native = browser = deployment = cross_platform = True
            continue
        return {**{job: True for job in JOBS}, "platform_stack": platform_stack}

    return {
        "native": native,
        "browser": browser,
        "map_generators": map_generators,
        "deployment": deployment,
        "cross_platform": cross_platform,
        "platform": platform,
        "platform_stack": platform_stack,
        "music": music,
    }


def browser_only(path):
    if path.startswith("platforms/web/tests/fixtures/") or path == "platforms/web/tests/determinism.spec.js":
        return False
    if path == "platforms/web/toolchain.json":
        return False
    return Path(path).suffix in {".js", ".css", ".html", ".json", ".md"}



def coverage_profile(paths, event, selected):
    from ci_policy import MUSIC_TOOL_PATHS, PRESENTATION, TOOLING_TESTS, cheap_path, is_test_source, unclassified
    compatibility = event != 'pull_request' or not paths
    browsers_all = compatibility
    android = event != 'pull_request' or not paths
    reasons = []
    for path in paths:
        if path.startswith('docs/') or path.endswith('.md') or path in CI_TOOL_TESTS or cheap_path(path):
            continue
        if path.startswith(MUSIC_TOOL_PATHS):
            continue
        if not is_test_source(path) and path.startswith(('src/', 'libs/gag/', 'libs/usl/', 'platforms/mobile/', 'scons/', 'content/data/', 'platforms/macos/', 'platforms/windows/', 'platforms/linux/flatpak/', 'snap/', 'platforms/stores/fdroid/', 'fastlane/')) or path in ('SConstruct','vcpkg.json','tools/package_assets.py','tools/asset-requirements.txt','tools/image_encoding.json','.github/workflows/mobile.yml','.github/scripts/ci_changed_paths.py','.github/scripts/ci_coverage_baseline.py'):
            android = True
        if path.startswith(('platforms/web/',) + PRESENTATION) or path in RENDER_IMPLEMENTATIONS:
            browsers_all = True
        # Conservative omissions only for known test-only and implementation-only boundaries.
        primary_safe = ((path.startswith('test/') and not path.startswith(('test/fixtures/','test/maxima/fixtures/','test/support/') + TOOLING_TESTS)
                         or is_test_source(path) and not Path(path).name.startswith('Script'))
                        and path not in ('test/run-browser-determinism.py','test/check_javascript.py','test/check_javascript_corpus.py','test/check_javascript_evidence.py','test/maxima/check_save_continuation_fixture.py','test/build_provenance.py','libs/gag/src/ImageAssetTest.cpp'))
        primary_safe = primary_safe or path in RENDER_IMPLEMENTATIONS or (path.startswith(PRESENTATION) and not unclassified(path) and not path.startswith(FULL_PATHS) and path.endswith('.cpp')) or (path.startswith('platforms/web/') and browser_only(path)) or path.startswith(('deploy/','test/deployment/','test/transport/'))
        # Shard/runtime configuration changes must exercise the oldest supported
        # platform too, even though their files live under test/.
        if path.startswith('test/ci-timings/') or path in {
            'test/run_tests.py', 'test/ci_native_shard_plan.py',
            'test/ci-native-auxiliary.json', 'test/build_ci_timing_profile.py',
        }:
            primary_safe = False
        if path.endswith(('.h','.hpp','.hh')) or not primary_safe:
            compatibility = True
            if not path.startswith(('test/', '.github/')) and not is_test_source(path):
                android = True
            reasons.append('compatibility or unknown path: '+path)
    browsers_all = browsers_all or compatibility
    return {'compatibility': compatibility, 'browsers_all': browsers_all,
            'android': android, 'android_arches': ['arm64-v8a','armeabi-v7a','x86_64'] if compatibility else ['arm64-v8a'],
            'profile': 'compatibility' if compatibility else ('primary' if any(on for job, on in selected.items() if job != 'music') else 'lightweight'),
            'reasons': reasons or ['known relevant boundaries; primary platforms suffice']}

def changed_paths(base):
    subprocess.run(
        ["git", "fetch", "--no-tags", "--depth=1", "origin", base],
        check=True,
        stdout=subprocess.DEVNULL,
    )
    diff = subprocess.run(
        ["git", "diff", "--name-only", "--no-renames", "-z", base, "HEAD"],
        check=True,
        capture_output=True,
    )
    return [os.fsdecode(path) for path in diff.stdout.split(b"\0") if path]


def choose_checks(
    paths,
    known,
    event,
    labels=(),
    *,
    branch='',
    history=None,
    model=None,
    budget=0.12,
    prediction_time=None,
    disabled=False,
    audit=False,
    browser_only=False,
    called_full=False,
    reused=False,
):
    """Resolve the policy into one exact, dependency-complete execution plan."""
    from ci_policy import CHEAP_FLAGS, CHEAP_PATHS, full, select, browser_matrix
    from ci_check_inventory import (
        inventory,
        execution_plan,
        flags_from_plan,
        from_flags,
        critical_change,
    )
    from ci_predictive import select_checks

    checks = inventory()
    desired, reasons = select(paths, labels, known)
    complete = (
        event in ('push', 'schedule', 'workflow_dispatch')
        or called_full
        or 'ci:full' in labels
    )
    if complete:
        desired = full()
    if browser_only:
        desired = {flag: flag in ('browser', 'deployment') for flag in desired}
    report = None
    exceptions = []
    mode = 'affected'
    selected_ids = []
    # Resolve mandatory/full modes before prediction so it cannot weaken them.
    if reused:
        mode = 'nightly-reused'
    elif event == 'pull_request' and (
        not known or critical_change(paths) or disabled or audit
    ):
        selected_ids = [c['id'] for c in checks]
        mode = 'full'
        if not known:
            reason = 'diff unavailable'
        elif critical_change(paths):
            reason = 'CI/build inventory changed'
        elif disabled:
            reason = 'predictive reduction disabled'
        else:
            reason = 'full PR audit'
        exceptions = [reason]
    elif complete:
        mode = 'affected' if browser_only else 'full'
        entries = browser_matrix(desired, paths, True)
        selected_ids = from_flags(
            desired, entries, ['arm64-v8a', 'armeabi-v7a', 'x86_64'], 'full', checks
        )
    elif event == 'pull_request' and 'ci:run' not in labels:
        engine = any(v for k, v in desired.items() if k not in CHEAP_FLAGS)
        if engine:
            mode = 'predictive'
            candidates = [c for c in checks if not c.get('full_only')]
            report = select_checks(
                candidates,
                paths if known else None,
                history,
                model,
                budget=budget,
                event=event,
                branch=branch,
                prediction_time=prediction_time,
            )
            # The quota counts ranked leaves; prerequisites are added below.
            selected_ids = list(report['selected_ids'])
            # New checks need one observed result; never infer a pass for them.
            selected_ids += report['unknown_ids']
            if report['unknown_ids']:
                exceptions.append('new checks: ' + ', '.join(report['unknown_ids']))
            if 'ci:windows' in labels:
                selected_ids.append('windows')
            if 'ci:android' in labels:
                selected_ids += [c['id'] for c in checks if c['job'] == 'android']
            if 'ci:browsers' in labels:
                selected_ids += [c['id'] for c in checks if c['job'] == 'web-test']
        else:
            mode = 'cheap-contracts'
        for flag in CHEAP_FLAGS:
            if desired.get(flag) and any(
                p.startswith(CHEAP_PATHS[flag]) for p in paths
            ):
                selected_ids.append(flag)
    else:
        # Explicit ci:run retains affected verification; primary GCC13 stays complete.
        entries = browser_matrix(
            desired, paths, desired['coverage'] or 'ci:browsers' in labels
        )
        selected_ids = from_flags(
            desired, entries, ['arm64-v8a', 'armeabi-v7a', 'x86_64'], 'full', checks
        )
    selected_ids = sorted(set(selected_ids))
    plan = execution_plan(selected_ids, checks)
    plan.update(
        mode=mode,
        requested_budget=budget,
        predictor=report,
        exceptions=exceptions,
        inventory_count=len(checks),
        ranked_count=report['selected_count'] if report else len(selected_ids),
        executed_count=len(plan['executed_ids']),
    )
    # A prediction does not imply the compound full-matrix comparison ran.
    full_matrix = set(plan['executed_ids']) == {c['id'] for c in checks}
    plan['full_matrix'] = full_matrix
    return flags_from_plan(plan), plan, dict(desired, reasons=reasons)


def _prediction_inputs():
    """Latest trusted master bundle, with the committed inert seed as fallback."""
    from ci_history import fetch_latest_bundle, load_history
    directory=Path(__file__).resolve().parents[2]/'.github/ci';reasons=[]
    repo=os.environ.get('GITHUB_REPOSITORY')
    if repo and os.environ.get('GH_TOKEN'):
        try:
            target=Path('artifacts/ci-predictor')
            if fetch_latest_bundle(repo,target,token=os.environ['GH_TOKEN']):directory=target
            else:reasons.append('no trusted refreshed bundle; using committed seed')
        except (OSError,ValueError,KeyError,RuntimeError) as error:
            reasons.append('bundle unavailable; using committed seed: '+str(error))
    try:history=load_history(directory/'history.json.gz')
    except (OSError,ValueError,KeyError,EOFError) as error:
        history={'schema':1,'records':[]};reasons.append('history unavailable: '+str(error))
    return history,directory/'model.json.gz',reasons


def main():
    import hashlib
    import time
    from ci_policy import CHEAP_FLAGS, fingerprint, select
    from ci_coverage_baseline import successful_full_run
    from ci_check_inventory import fingerprint as inventory_fingerprint
    parser=argparse.ArgumentParser()
    parser.add_argument('--base',help='PR base revision; master still runs full coverage')
    args=parser.parse_args()
    event=os.environ.get('GITHUB_EVENT_NAME','workflow_dispatch')
    payload=json.loads(Path(os.environ['GITHUB_EVENT_PATH']).read_text()) if os.environ.get('GITHUB_EVENT_PATH') else {}
    pr=payload.get('pull_request',{})
    labels=[x['name'] for x in pr.get('labels',[])]
    branch=pr.get('head',{}).get('ref') or os.environ.get('GITHUB_REF_NAME','master')
    draft=bool(pr.get('draft') or os.environ.get('DRAFT')=='true')
    paths=[];known=False
    base=args.base if event=='pull_request' else payload.get('before') or os.environ.get('BASE_SHA')
    if base and event in ('pull_request','push'):
        try:paths=changed_paths(base);known=True
        except (OSError,subprocess.CalledProcessError) as error:print('Diff unavailable; selecting full coverage:',error,file=sys.stderr)
    prediction_time=time.time()
    budget=float(os.environ.get('CI_PR_CHECK_BUDGET') or '.12')
    if not 0 < budget <= 1:raise ValueError('CI_PR_CHECK_BUDGET must be in (0, 1]')
    percent=float(os.environ.get('CI_PR_FULL_AUDIT_PERCENT') or '0')
    if not 0 <= percent <= 100:raise ValueError('CI_PR_FULL_AUDIT_PERCENT must be in [0, 100]')
    sample=f'{os.environ.get("GITHUB_REPOSITORY","")}:{os.environ.get("GITHUB_SHA","")}:{os.environ.get("GITHUB_RUN_ID","")}'
    audit=int(hashlib.sha256(sample.encode()).hexdigest()[:16],16)/2**64 < percent/100
    history=model=None;input_reasons=[]
    # Full and explicit affected runs need neither the model nor a network restore.
    if event=='pull_request' and not ({'ci:run','ci:full'} & set(labels)):
        from ci_check_inventory import critical_change
        desired_flags,_=select(paths,labels,known)
        engine=any(value for flag,value in desired_flags.items() if flag not in CHEAP_FLAGS)
        if (known and engine and not critical_change(paths) and not audit
                and os.environ.get('CI_PREDICTIVE_ENABLED','true')!='false'):
            history,model,input_reasons=_prediction_inputs()
    reused_run=None
    if event=='schedule':reused_run=successful_full_run(os.environ.get('GITHUB_REPOSITORY',''),os.environ.get('GITHUB_SHA',''),os.environ.get('GH_TOKEN',''))
    selected,plan,desired=choose_checks(paths,known,event,labels,branch=branch,history=history,model=model,
        budget=budget,prediction_time=prediction_time,disabled=os.environ.get('CI_PREDICTIVE_ENABLED','true')=='false',
        audit=audit,browser_only=event=='workflow_dispatch' and os.environ.get('BROWSER_ONLY')=='true',
        called_full=os.environ.get('CI_CALLED_FULL')=='true',reused=reused_run is not None)
    if plan['predictor']:plan['predictor']['fallback_reasons'] += input_reasons
    options=plan['options'];entries=options['browser_matrix']['include'];arches=options['android_arches']
    policy=fingerprint()
    inventory_hash=hashlib.sha256(json.dumps(
        {'catalog':inventory_fingerprint(),'checks':plan['executed_ids'],'options':options},
        sort_keys=True,separators=(',',':')).encode()).hexdigest()
    sha=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip() if os.environ.get('CI_CALLED_FULL')=='true' else os.environ.get('GITHUB_SHA')
    observation=dict(schema=4,verification_mode=plan['mode'],reused_run_id=reused_run,selection=selected,event=event,
        sha=sha,branch=branch,prediction_time=prediction_time,base_sha=base,full_matrix=plan['full_matrix'],desired=desired,
        effective=dict(selected,browsers_all=bool(entries and {e['browsers'] for e in entries}=={'chromium','firefox','webkit'}),android_arches=arches),
        tiers_enabled=False,predictive_enabled=plan['mode']=='predictive',draft=draft,paths=paths,checkpoint=None,policy_fingerprint=policy,
        inventory={'checks':plan['executed_ids'],'jobs':selected,'browsers':entries,'android_arches':arches},
        inventory_fingerprint=inventory_hash,execution_plan=plan,
        checkpoint_eligible=plan['full_matrix'] and event in ('push','schedule','workflow_dispatch') and os.environ.get('CI_CALLED_FULL')!='true')
    target=Path('artifacts/ci-selection.json');target.parent.mkdir(parents=True,exist_ok=True);target.write_text(json.dumps(observation,indent=2)+'\n')
    outputs=dict(selected,verification_mode=plan['mode'],browsers_all=observation['effective']['browsers_all'],
        android_arches=json.dumps(arches,separators=(',',':')),browser_matrix=json.dumps(options['browser_matrix'],separators=(',',':')),
        variants_matrix=json.dumps(options['variants_matrix'],separators=(',',':')),secondary_profile='full',draft=draft,
        full_matrix=plan['full_matrix'],inventory_fingerprint=inventory_hash,job_plan=json.dumps(plan,separators=(',',':')))
    for name in ('gcc11','gcc13'):
        outputs[name+'_shards']=json.dumps(options[name+'_shards'],separators=(',',':'))
        outputs[name+'_generators']=json.dumps(options[name+'_generators'],separators=(',',':'))
        outputs[name+'_regressions']=options[name+'_regressions']
    outputs['clang_regressions']=options['clang_regressions']
    for job,on in plan['jobs'].items():outputs['run_'+job.replace('-','_')]=on
    lines=''.join(f'{key}={str(value).lower() if isinstance(value,bool) else value}\n' for key,value in outputs.items())
    print(json.dumps(observation,indent=2))
    if os.environ.get('GITHUB_OUTPUT'):
        with open(os.environ['GITHUB_OUTPUT'],'a') as f:f.write(lines)
    if os.environ.get('GITHUB_STEP_SUMMARY'):
        with open(os.environ['GITHUB_STEP_SUMMARY'],'a') as f:
            f.write('## Selected development checks\n\n'+f'Verification mode: **{plan["mode"]}**.\n\n')
            if plan['predictor']:
                eligible=plan['predictor']['inventory_count']
                f.write(f'Ranked budget: {budget:.0%}; {plan["ranked_count"]} of {eligible} eligible checks. ')
            f.write(f'{plan["executed_count"]} of {plan["inventory_count"]} checks execute after prerequisites and policy additions.\n\n')
            if plan['predictor']:f.write('Model: `'+plan['predictor']['model_id']+'`.\n\n')
            for reason in plan['exceptions']+input_reasons:f.write('- '+reason+'\n')
            f.write('\n| Check | Decision |\n|---|---|\n')
            from ci_check_inventory import inventory
            for check in inventory():
                cid=check['id']
                ranked=plan['predictor']['selected_ids'] if plan['predictor'] else ()
                decision=('ranked' if cid in ranked else
                          'policy addition' if plan['predictor'] and cid in plan['ranked_ids'] else
                          'selected' if cid in plan['ranked_ids'] else
                          'prerequisite' if cid in plan['executed_ids'] else 'not selected')
                f.write(f'| {check["name"]} | {decision} |\n')
            if not any(v for k,v in selected.items() if k not in CHEAP_FLAGS):f.write('\nCheap-only success does not establish engine verification or acceptance of PR evidence.\n')


if __name__=='__main__':main()
