"""Stable executable CI check identities and their build prerequisites.

The budget applies to ranked checks; prerequisite closure is reported separately.
Cross-platform comparison is a compound full-matrix audit, never a free-standing
12%-budget check. Its complete producer set is retained for full/affected runs.
"""
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
TOP_JOBS = ('android', 'macos', 'tsan', 'native-coverage', 'linux-gcc11',
            'linux-gcc13-producer', 'linux-gcc13', 'linux-build', 'linux-clang',
            'linux', 'linux-variants', 'linux-map-generators-standalone',
            'linux-map-generators', 'windows', 'web-build', 'web-native',
            'web-deploy', 'web-test', 'browser-determinism', 'platform',
            'platform-stack', 'music')
VARIANTS = ({'image': 'ubuntu:22.04', 'runner': 'ubuntu-22.04', 'compiler': 'g++-11'},
            {'image': 'ubuntu:24.04', 'runner': 'ubuntu-24.04', 'compiler': 'g++-13'})
ARCHES = ('arm64-v8a', 'armeabi-v7a', 'x86_64')
GENERATOR_SUITES = ('sweep-a', 'sweep-b', 'telemetry')


def browser_id(name):
    return 'browser/' + re.sub(r'[^a-z0-9]+', '-', name.lower()).strip('-')


def inventory():
    rows = []
    def add(identity, name, job, requires=(), **extra):
        rows.append(dict(id=identity, name=name, job=job, requires=list(requires),
                         history_names=[name], **extra))
    for compiler, runner, image, cc in (('gcc11','ubuntu-22.04','ubuntu:22.04','g++-11'),
                                       ('gcc13','ubuntu-24.04','ubuntu:24.04','g++-13'),
                                       ('clang','ubuntu-24.04','ubuntu:24.04','clang-18')):
        prefix='native/'+compiler
        top='linux-gcc13-producer' if compiler=='gcc13' else 'linux-'+compiler
        add(prefix+'/build', f'linux build ({image}, {cc})', top)
        add(prefix+'/regressions', f'Linux CLI and compatibility evidence ({cc})',
            'linux-'+compiler, [prefix+'/build'])
        if compiler=='clang':continue
        for shard in range(1,5):
            add(prefix+f'/shard-{shard}', f'Linux tests ({runner}, shard {shard}/4)',
                'linux-'+compiler, [prefix+'/build'])
        for suite in GENERATOR_SUITES:
            add(prefix+'/generators-'+suite, f'Map generators ({runner}, {suite})',
                'linux-'+compiler, [prefix+'/build'])
    for row in VARIANTS:
        add('variants/'+row['runner'], f'linux variants ({row["image"]})', 'linux-variants')
    add('windows', 'windows (mingw-w64)', 'windows')
    add('web-build', 'browser build (WebAssembly)', 'web-build')
    add('web-native', 'browser native WSS server, router and transport', 'web-native',
        [])
    add('web-deploy', 'browser self-hosting deployment', 'web-deploy', ['web-build'])
    entries=json.loads((ROOT/'.github/scripts/ci_browser_matrix.json').read_text())
    for entry in entries:
        add(browser_id(entry['name']), f'browser tests ({entry["name"]})', 'web-test',
            ['web-build','web-native'], browser_entry=entry)
    for arch in ARCHES:
        add('android/'+arch, f'Android release APK ({arch})', 'android')
    add('macos','macOS native build and checks','macos')
    # Names are aliases for identity migration, not invented outcomes for shards.
    rows[-1]['history_names'] += ['macOS build and tests','native']
    add('tsan','ThreadSanitizer','tsan')
    rows[-1]['history_names'] += ['thread-sanitizer','Thread sanitizer','thread sanitizer (threaded games)']
    add('coverage','Native Clang coverage','native-coverage')
    add('platform','platform (TypeScript)','platform')
    add('platform-stack','self-hosted platform stack','platform-stack')
    add('music','music pipeline (Python)','music')
    producers=[r['id'] for r in rows if r['job']=='web-test']
    producers += ['windows','native/gcc13/regressions']
    producers += [f'native/gcc13/shard-{i}' for i in range(1,5)]
    add('cross-platform','browser/native simulation equivalence','browser-determinism',
        producers, full_only=True)
    ids=[r['id'] for r in rows]
    if len(ids)!=len(set(ids)):raise ValueError('Duplicate CI check identity')
    return rows


def dependency_closure(selected, checks=None):
    by_id={r['id']:r for r in (checks or inventory())}
    result=set();active=set()
    def visit(identity):
        if identity not in by_id:raise ValueError(f'Unknown check: {identity}')
        if identity in active:raise ValueError('Cyclic check prerequisites')
        if identity in result:return
        active.add(identity)
        for dependency in by_id[identity]['requires']:visit(dependency)
        active.remove(identity);result.add(identity)
    for identity in selected:visit(identity)
    return sorted(result)


def from_flags(flags, entries, arches, profile='full', checks=None):
    """Translate existing full/affected policies into exact selectable check IDs."""
    checks=checks or inventory();chosen=[];browser_names={e['name'] for e in entries}
    for row in checks:
        identity=row['id'];take=False
        if identity.startswith('native/gcc13/'):take=flags.get('native',False)
        elif identity.startswith('native/gcc11/'):take=flags.get('native',False) and flags.get('compatibility',False)
        elif identity.startswith('native/clang/'):take=flags.get('native',False) and flags.get('variants',False)
        elif identity.startswith('variants/'):take=flags.get('native',False) and flags.get('variants',False)
        elif identity.startswith('browser/'):take=flags.get('browser',False) and row['browser_entry']['name'] in browser_names
        elif identity.startswith('android/'):take=flags.get('android',False) and identity.split('/',1)[1] in arches
        else:
            flag={'windows':'windows','web-build':'browser','web-native':'browser',
                  'web-deploy':'deployment','coverage':'coverage','platform-stack':'platform_stack',
                  'cross-platform':'cross_platform'}.get(identity,identity)
            take=flags.get(flag,False)
        if '/generators-' in identity:
            take=flags.get('map_generators',False) and ('gcc13' in identity or flags.get('compatibility',False))
            if profile!='full' and identity.endswith('sweep-b'):take=False
        if take:chosen.append(identity)
    return dependency_closure(chosen,checks)


def execution_plan(selected, checks=None):
    """Translate direct requests into executable jobs and matrix prerequisites.

    ``ranked_ids`` includes policy additions such as labels and unknown checks;
    the predictor report separately retains the IDs chosen within its budget.
    """
    checks=checks or inventory();by_id={r['id']:r for r in checks}
    executed=dependency_closure(selected,checks)
    jobs={job:False for job in TOP_JOBS}
    for identity in executed:jobs[by_id[identity]['job']]=True
    # These jobs verify reusable-call completion; they do no substantive work.
    jobs['linux-build']=any(jobs[k] for k in ('linux-gcc11','linux-gcc13','linux-gcc13-producer'))
    jobs['linux']=jobs['linux-build'] or jobs['linux-clang']
    jobs['linux-map-generators']=any('/generators-' in i for i in executed)
    options={}
    for compiler in ('gcc11','gcc13'):
        options[compiler+'_shards']=[n for n in range(1,5) if f'native/{compiler}/shard-{n}' in executed]
        options[compiler+'_generators']=[s for s in GENERATOR_SUITES if f'native/{compiler}/generators-{s}' in executed]
        options[compiler+'_regressions']=f'native/{compiler}/regressions' in executed
    options['clang_regressions']='native/clang/regressions' in executed
    options['browser_matrix']={'include':[r['browser_entry'] for r in checks if r['id'] in executed and 'browser_entry' in r]}
    options['android_arches']=[a for a in ARCHES if 'android/'+a in executed]
    options['variants_matrix']={'include':[r for r in VARIANTS if 'variants/'+r['runner'] in executed]}
    return {'schema':1,'ranked_ids':list(selected),'executed_ids':executed,
            'dependency_ids':sorted(set(executed)-set(selected)), 'jobs':jobs,'options':options,
            'expected_checks':[dict(id=i,name=by_id[i]['name'],history_names=by_id[i]['history_names']) for i in executed]}


def flags_from_plan(plan):
    jobs=plan['jobs']
    return {'native':jobs['linux'],'browser':any(jobs[j] for j in ('web-build','web-native','web-test')),
            'map_generators':jobs['linux-map-generators'],'deployment':jobs['web-deploy'],
            'cross_platform':jobs['browser-determinism'],'android':jobs['android'],
            'windows':jobs['windows'],'compatibility':jobs['linux-gcc11'],
            'variants':jobs['linux-clang'] or jobs['linux-variants'],
            'coverage':jobs['native-coverage'],'tsan':jobs['tsan'],'macos':jobs['macos'],
            'platform':jobs['platform'],'platform_stack':jobs['platform-stack'],'music':jobs['music']}


def fingerprint(checks=None):
    value=checks or inventory()
    return hashlib.sha256(json.dumps(value,sort_keys=True,separators=(',',':')).encode()).hexdigest()


def critical_change(paths):
    """New CI/build/test machinery gets a full validation before ranking it."""
    from ci_policy import cheap_path
    # Documentation does not change executable CI machinery. Release packaging
    # and mirror-only deployment have their own workflows.
    paths=[p for p in paths if not p.endswith('.md')
           and not (p.startswith('.github/workflows/') and cheap_path(p))]
    prefixes=('.github/','scons/','test/build_system/')
    files={'SConstruct','vcpkg.json','requirements-dev.txt','test/run_tests.py',
           'test/ci_native_shard_plan.py','test/ci-compatibility.json','test/ci-native-auxiliary.json'}
    return any(p.startswith(prefixes) or p in files or p.endswith('/SConscript') for p in paths)
