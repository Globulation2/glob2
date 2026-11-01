#!/usr/bin/env python3
"""Qualify a simulator bundle and retain localization evidence without Apple credentials."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import plistlib
import re
import shutil
import subprocess
import time
import uuid

BUNDLE = 'org.globulation2.glob2'
FILES = ('data/texts.th.txt', 'data/fonts/scripts/NotoSansThai-Regular.ttf',
         'data/fonts/LICENSE-Noto.txt', 'data/fonts/LICENSE-HarfBuzz.txt')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def select_device(runtimes, device_types):
    available = [r for r in runtimes if r.get('isAvailable') and '.iOS-' in r['identifier']]
    if not available:
        raise ValueError('No installed available iOS simulator runtime; install the pinned Xcode iOS platform')
    runtime = max(available, key=lambda r: tuple(int(x) for x in r['version'].split('.')))
    supported = {d['identifier'] for d in runtime.get('supportedDeviceTypes', [])}
    phones = [d for d in device_types if d.get('productFamily') == 'iPhone' or d['name'].startswith('iPhone')]
    if supported:
        phones = [d for d in phones if d['identifier'] in supported]
    if not phones:
        raise ValueError('Installed iOS runtime has no compatible iPhone device type')
    # Inventory IDs are authoritative; create fails rather than changing the runtime silently.
    return runtime, sorted(phones, key=lambda d: d['name'])[-1]


def localized_strings_path(app):
    expected = app/'th.lproj/InfoPlist.strings'
    if not expected.is_file():
        observed = sorted(p.relative_to(app).as_posix() for p in app.rglob('InfoPlist.strings'))
        raise ValueError('Missing root Thai permission localization: '+str(expected)+
                         '; observed bundle paths: '+repr(observed))
    return expected


def bundle_checks(root, app, localized_read):
    expected = re.search(r'<key>NSLocalNetworkUsageDescription</key><string>(.*?)</string>',
                         (root/'mobile/ios/Info.plist.in').read_text()).group(1)
    info = plistlib.loads((app/'Info.plist').read_bytes())
    if info.get('NSLocalNetworkUsageDescription') != expected:
        raise ValueError('Bundled English permission purpose differs from source')
    target_file = root/'mobile/ios/th.lproj/InfoPlist.strings'
    target = re.search(r'"NSLocalNetworkUsageDescription"\s*=\s*"([^"]+)";', target_file.read_text()).group(1)
    if localized_read != target:
        raise ValueError('Bundled Thai permission purpose differs from reviewed source')
    hashes = {}
    for name in FILES:
        if not (app/name).is_file() or sha(root/name) != sha(app/name):
            raise ValueError('Missing or changed bundled localization asset: ' + name)
        hashes[name] = sha(app/name)
    hashes['th.lproj/InfoPlist.strings'] = sha(app/'th.lproj/InfoPlist.strings')
    hashes['Glob2'] = sha(app/'Glob2')
    return hashes


def choose_xcode(root, output):
    wanted = json.loads((root/'mobile/toolchain.json').read_text())['ios']['xcode']
    if platform.system() != 'Darwin' or platform.machine() != 'arm64':
        raise ValueError('Simulator qualification requires an Apple Silicon macOS runner')
    candidates = []
    current = os.environ.get('DEVELOPER_DIR')
    if current:
        candidates.append(Path(current))
    candidates += sorted(Path('/Applications').glob('Xcode*.app/Contents/Developer'))
    probes = []
    (output/'xcode-inventory.json').write_text('[]\n')
    for developer in dict.fromkeys(candidates):
        env = dict(os.environ, DEVELOPER_DIR=str(developer))
        result = subprocess.run(['xcodebuild', '-version'], env=env, capture_output=True, text=True)
        probes.append({'developer_dir':str(developer),'returncode':result.returncode,'version':result.stdout,'error':result.stderr})
        (output/'xcode-inventory.json').write_text(json.dumps(probes,indent=2)+'\n')
        if result.returncode == 0 and result.stdout.splitlines()[0] == 'Xcode '+wanted:
            os.environ['DEVELOPER_DIR'] = str(developer)
            doctor = subprocess.check_output(['python3', 'mobile/doctor.py', 'ios', 'environment=simulator', 'release=1'], cwd=root, env=env, text=True)
            (output/'toolchain.json').write_text(doctor)
            if os.environ.get('GITHUB_ENV'):
                with open(os.environ['GITHUB_ENV'], 'a') as f:
                    f.write('DEVELOPER_DIR='+str(developer)+'\n')
            fingerprint = json.loads(doctor)['toolchain']['fingerprint']
            if os.environ.get('GITHUB_OUTPUT'):
                with open(os.environ['GITHUB_OUTPUT'], 'a') as f:
                    f.write('fingerprint='+fingerprint+'\n')
            return
    raise ValueError('Required Xcode '+wanted+' is not installed; available toolchains cannot silently replace the mobile pin')


def run_qa(root, output, locale):
    if locale != 'th':
        raise ValueError('This qualification currently validates the reviewed Thai catalog only')
    app = root/'build/ios/simulator/arm64/15.0/client/release/xcode-project/build/Release-iphonesimulator/Glob2.app'
    summary = {'locale': locale, 'passed': False, 'commands': [], 'coverage':
               ['simulator bundle assets and permission localization', 'native startup, resume and fresh relaunch'],
               'excluded': ['OS local-network permission popup', 'physical device', 'Thai IME interaction'],
               'visual_review_required': True}
    log = output/'commands.log'
    def run(parts, timeout=180, env=None):
        start = time.monotonic(); item = {'arguments': list(map(str, parts))}; summary['commands'].append(item)
        try:
            result = subprocess.run(item['arguments'], cwd=root, env=env, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
            with log.open('a') as f:
                f.write(json.dumps(item['arguments'])+'\n'+result.stdout+'\n')
            item['returncode'] = result.returncode
            result.check_returncode()
            return result.stdout.strip()
        finally:
            item['seconds'] = round(time.monotonic()-start, 3)
            (output/'result.json').write_text(json.dumps(summary, indent=2)+'\n')
    device = None
    # A unique set makes ownership independent of existing simulator names.
    device_set = root/'build/mobile-tools'/('ios-localization-'+uuid.uuid4().hex)
    base = ['xcrun', 'simctl', '--set', str(device_set)]
    def sim(*args, **kwargs): return run(base+list(args), **kwargs)
    try:
        summary['revision'] = run(['git', 'rev-parse', 'HEAD'])
        summary['source_diff'] = run(['git', 'diff', '--binary', 'HEAD'])
        summary['source_status'] = run(['git', 'status', '--porcelain', '--untracked-files=normal'])
        if summary['source_diff'] or summary['source_status']:
            raise ValueError('CI qualification requires a committed exact revision')
        summary['toolchain'] = json.loads((output/'toolchain.json').read_text())
        thai_file = localized_strings_path(app)
        run(['codesign', '--verify', '--deep', '--strict', str(app)])
        run(['plutil', '-lint', str(app/'Info.plist'), str(thai_file)])
        thai = run(['plutil', '-extract', 'NSLocalNetworkUsageDescription', 'raw', '-o', '-', str(thai_file)])
        summary['bundle_hashes'] = bundle_checks(root, app, thai)
        # simctl --set requires an existing directory, even for inventory.
        device_set.mkdir(parents=True, exist_ok=False)
        summary['simulator_set'] = str(device_set)
        runtimes = json.loads(sim('list', 'runtimes', '--json'))
        types = json.loads(sim('list', 'devicetypes', '--json'))
        (output/'runtimes.json').write_text(json.dumps(runtimes, indent=2)+'\n')
        (output/'device-types.json').write_text(json.dumps(types, indent=2)+'\n')
        runtime, phone = select_device(runtimes['runtimes'], types['devicetypes'])
        summary['runtime'] = runtime; summary['device_type'] = phone
        device = sim('create', 'Glob2-Mobile', phone['identifier'], runtime['identifier'])
        summary['device'] = device; summary['simulator_set'] = str(device_set)
        sim('boot', device); sim('bootstatus', device, '-b', timeout=300)
        sim('spawn', device, 'defaults', 'write', 'NSGlobalDomain', 'AppleLanguages', '-array', 'th', 'en')
        sim('spawn', device, 'defaults', 'write', 'NSGlobalDomain', 'AppleLocale', '-string', 'th_TH')
        summary['preferred_languages'] = sim('spawn', device, 'defaults', 'read', 'NSGlobalDomain', 'AppleLanguages')
        sim('install', device, str(app))
        container = Path(sim('get_app_container', device, BUNDLE, 'data'))
        profile = container/'Documents/LocalizationQA'; profile.mkdir(parents=True, exist_ok=False)
        # Do not prefill language: exercise the actual SDL preferred-locale selection.
        (profile/'preferences.txt').write_text('thumbSideChosen=1\naudioPreferenceChosen=1\nmute=1\nintroductionOffered=1\n')
        env = dict(os.environ, SIMCTL_CHILD_SDL_PREFERRED_LOCALES='th_TH',
                   SIMCTL_CHILD_GLOB2_USER_DATA_DIR=str(profile))
        summary['fixture'] = {'profile': str(profile), 'first_run_choices': 'hand and introduction marked offered; audio chosen as silence',
                              'locale_input': 'SDL_PREFERRED_LOCALES=th_TH via simctl child environment; AppleLanguages th,en',
                              'language_prefilled': False}
        smoke = root/'build/mobile-smoke-ios-th'
        if smoke.exists():
            raise ValueError('Smoke evidence directory already exists; refuse stale evidence')
        run(['python3', 'mobile/ios_smoke.py', '--device', device, '--simulator-set', str(device_set), '--output', str(smoke)], timeout=900, env=env)
        shutil.copytree(smoke, output/'smoke')
        summary['smoke'] = json.loads((smoke/'result.json').read_text())
        if not summary['smoke'].get('passed'):
            raise ValueError('Native simulator lifecycle smoke did not pass')
        if (profile/'preferences.txt').is_file():
            shutil.copy2(profile/'preferences.txt', output/'preferences.txt')
        summary['passed'] = True
    except Exception as exc:
        summary['error'] = str(exc)
        raise
    finally:
        # Preserve any partial smoke logs/screens even if the smoke subprocess failed.
        smoke = root/'build/mobile-smoke-ios-th'
        if smoke.exists(): shutil.copytree(smoke, output/'smoke', dirs_exist_ok=True)
        if device:
            for action in ('shutdown', 'delete'):
                try: sim(action, device, timeout=60)
                except Exception as exc: summary.setdefault('cleanup_errors', []).append(str(exc))
        (output/'result.json').write_text(json.dumps(summary, indent=2)+'\n')


def cleanup(root, output):
    record = output/'result.json'
    if not record.exists(): return
    result = json.loads(record.read_text())
    if not result.get('device'): return
    device_set = Path(result['simulator_set']).resolve()
    if device_set.parent != root/'build/mobile-tools' or not device_set.name.startswith('ios-localization-'):
        raise ValueError('Refuse cleanup outside the uniquely owned localization simulator set')
    base = ['xcrun','simctl','--set',str(device_set)]
    devices = json.loads(subprocess.check_output(base+['list','devices','--json'],text=True))['devices']
    selected = [d for group in devices.values() for d in group if d['udid']==result['device']]
    if not selected: return  # Already removed by the helper's finally block.
    if len(selected)!=1 or selected[0]['name']!='Glob2-Mobile':
        raise ValueError('Refuse cleanup of a simulator not matching recorded ownership')
    if selected[0]['state']!='Shutdown':
        subprocess.run(base+['shutdown',result['device']],check=True,timeout=60)
    subprocess.run(base+['delete',result['device']],check=True,timeout=60)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['select-xcode', 'run', 'cleanup'])
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--output', type=Path, default=Path('artifacts/ios-localization'))
    parser.add_argument('--locale', default='th')
    args = parser.parse_args(); root = args.root.resolve()
    output = (root/args.output).resolve()
    if not output.is_relative_to(root/'artifacts'):
        raise ValueError('Evidence must remain under this checkout artifacts directory')
    output.mkdir(parents=True, exist_ok=True)
    if args.command == 'select-xcode': choose_xcode(root, output)
    elif args.command == 'run': run_qa(root, output, args.locale)
    else: cleanup(root, output)


if __name__ == '__main__': main()
