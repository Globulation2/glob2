#!/usr/bin/env python3
"""Draw renderer parity scenes in the browser and measure them against the native consensus.

renderer_parity.py compares the native renderers with each other; this runner
adds the browser's WebGL2 renderer to that comparison. It draws the same
scenes with the same scene code: torus-render-benchmark built for the browser
(cmake/web.cmake, web-render-parity: the release build's game objects and
asset packages), run in Chromium by platforms/web/benchmarks/render-parity.cjs
with the same pinned inputs (save, camera, zoom, team, selection, HD artwork,
device tier, display scale) and the same settle condition (--settle-frames
paced frames at a fixed camera, then a capture of the last frame). It measures
each capture against the per-pixel median of the native GPU renderers'
captures from a renderer_parity run, with renderer_parity's metric: the share
of pixels more than 8 levels off, forgiving moves of --shift pixels.

    python3 tools/dev_build.py perf=1 glob2 torus-render-benchmark
    python3 tools/rendering/renderer_parity.py --prepare --quick --compression off --label native
    python3 tools/dev_build.py target=web release=1 web-render-parity web-package
    python3 tools/rendering/browser_parity.py --quick --native-label native --label mine

The browser always uploads RGBA (no HD block compression), so the native run
must have drawn with compression off: a --compression both run's <label>/off
pass is used, and the default bar is renderer_parity's uncompressed 2%.
Chromium renders WebGL2 headless through ANGLE on SwiftShader by default (as CI
does); a hardware backend (--angle or GLOB2_CHROMIUM_ANGLE) also needs
--allow-gpu, and must never run alongside native GPU work. WebGPU cannot run
here: the benchmark's frame loop blocks the application worker and completes
each frame with a synchronous readback, which the browser's WebGPU device
does not offer (its captures resolve asynchronously on the event loop).

Results go to artifacts/browser-parity/<label>/: index.html (every scene's
sheet), <scene>.png (native consensus, the browser capture, the heat map
against the consensus and against legacy GL, and zoomed crops of the worst
regions), report.md, summary.json and each scene's capture and log. The exit
status is 1 when a scene is over the bar or fails to draw.
"""
import argparse
import gzip
import html
import json
import os
from pathlib import Path
import platform
import re
import shutil
import socket
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
import renderer_parity as parity  # noqa: E402  (also checks numpy and Pillow)
from renderer_parity import np, Image, ImageDraw  # noqa: E402

ROOT = parity.ROOT
OUTPUT = ROOT / 'artifacts/browser-parity'
DRIVER = ROOT / 'platforms/web/benchmarks/render-parity.cjs'
RUNTIME = 'threaded/render-parity.js'
# The scene inputs a browser run takes over from renderer_parity's environment.
PASSED = re.compile(r'^(GLOB2_BENCH_|GLOB2_UI_SCALE$|GLOB2_DEVICE_TIER$|GLOB2_MOBILE_UI$|GLOB2_HD_BLOCK_COMPRESSION$)')
INPUT_DIR = '/tmp/parity'


def free_port():
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        return probe.getsockname()[1]


def serve(directory, port):
    server = subprocess.Popen([sys.executable, str(ROOT / 'platforms/web/serve.py'), str(port), '--bind', '127.0.0.1',
                               '--directory', str(directory)], cwd=ROOT, stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL)
    for _ in range(100):
        try:
            socket.create_connection(('127.0.0.1', port), timeout=0.2).close()
            return server
        except OSError:
            time.sleep(0.1)
    server.terminate()
    raise SystemExit(f'serve.py did not start on port {port}')


def native_run(label):
    """The renderer_parity pass to compare with: <label>/off when the run drew both
    compression passes, else <label> itself, which must have drawn with it off."""
    native = parity.OUTPUT / label
    if (native / 'off/summary.json').exists():
        native = native / 'off'
    if not (native / 'summary.json').exists():
        raise SystemExit(f'{native.relative_to(ROOT)}/summary.json missing: run renderer_parity.py --label {label} '
                         '(--compression off or both) first')
    summary = json.loads((native / 'summary.json').read_text())
    if summary.get('compression', 'off') != 'off':
        raise SystemExit(f'{native.relative_to(ROOT)} drew with HD block compression {summary["compression"]}; the '
                         'browser always uploads RGBA, so compare it with a --compression off (or both) run')
    return native, summary


def native_settings(summary):
    """renderer_parity's settings for the native run, as its argparse namespace."""
    return argparse.Namespace(
        settle_frames=summary['settle_frames'], interval_ms=summary['interval_ms'],
        window_size=summary['window_size'], hidpi=summary['hidpi'], device_tier=summary['device_tier'],
        hd=summary['hd'], compression='off', shader_dir=None)


def browser_scene(settings, name, directory, renderer):
    """The page's environment and arguments for one scene: renderer_parity's
    pinned inputs, with the saved game and the capture in the runtime's file system."""
    env = parity.scene_environment(settings, name, directory, renderer)
    passed = {key: value for key, value in env.items() if PASSED.match(key)}
    passed['GLOB2_USER_DATA_DIR'] = '/tmp/parity-profile'
    passed['GLOB2_BENCH_CAPTURE'] = f'{INPUT_DIR}/capture.ppm'
    width, height = parity.window_size(settings, parity.SCENES[name])
    scene = {'name': name, 'renderer': renderer, 'width': width, 'height': height, 'env': passed,
             'args': ['--renderer', renderer, '--no-fullscreen', '--mute', '--window-size', f'{width}x{height}'],
             'capture': str(directory / f'{renderer}.ppm'), 'log': str(directory / f'{renderer}.log'),
             'input': None}
    if 'GLOB2_BENCH_GAME' in passed:
        host = Path(passed['GLOB2_BENCH_GAME'])
        passed['GLOB2_BENCH_GAME'] = f'{INPUT_DIR}/{host.name}'
        scene['input'] = {'path': passed['GLOB2_BENCH_GAME'], 'file': str(host)}
    return scene


def native_images(native, name):
    directory = native / name
    images = {}
    for renderer in parity.RENDERERS:
        if renderer not in parity.INFORMATIONAL and (directory / f'{renderer}.png').exists():
            images[renderer] = np.asarray(Image.open(directory / f'{renderer}.png').convert('RGB'))
    return images


def compare(args, name, native, directory, renderer, record):
    """Measures one browser capture against the native consensus and each native GPU renderer."""
    images = native_images(native, name)
    shapes = {image.shape for image in images.values()}
    if not images or len(shapes) > 1:
        record.update(status='no-consensus', reason='native captures missing or of different sizes')
        return None
    consensus = np.median(np.stack(list(images.values())), axis=0).round().astype(np.uint8)
    capture = np.asarray(Image.open(directory / f'{renderer}.png').convert('RGB'))
    if capture.shape != consensus.shape:
        record.update(status='failed', reason=f'capture {capture.shape[1]}x{capture.shape[0]} differs from the '
                      f'native {consensus.shape[1]}x{consensus.shape[0]}')
        return None
    delta = parity.difference(consensus, capture, args.shift)
    record['consensus'] = parity.metrics(delta)
    record['native'] = {native_renderer: parity.metrics(parity.difference(image, capture, args.shift))
                        for native_renderer, image in images.items()}
    # How far the native renderers themselves sit from their consensus, for scale.
    record['native_spread'] = {native_renderer: parity.metrics(parity.difference(consensus, image, args.shift))
                               for native_renderer, image in images.items()}
    record['status'] = 'ok'
    record['verdict'] = 'fail' if record['consensus']['over_8_percent'] > args.max_over_8 else 'ok'
    return consensus, capture, delta, images


def sheet(args, name, directory, renderer, record, consensus, capture, delta, images):
    spec = parity.SCENES[name]
    height, width = consensus.shape[:2]
    scale = spec['display'] if args.hidpi == 'emulate' else 1
    shown = (width // scale, height // scale)
    half = (shown[0] // 2, shown[1] // 2)
    crop = args.crop * scale
    zoom_to = args.crop * args.crop_zoom
    pad, header = 8, 40
    reference = images.get('legacy-gl')
    regions = parity.worst_regions(delta > 8, crop, args.worst_crops, (0, 0, width, height))
    columns = ['consensus', renderer] + (['legacy-gl'] if reference is not None else []) + ['difference']
    sheet_w = max(2 * (shown[0] + pad), 3 * (half[0] + pad), 160 + len(columns) * (zoom_to + pad)) + pad
    sheet_h = header + shown[1] + 20 + half[1] + 20 + len(regions) * (zoom_to + 20) + pad
    out = Image.new('RGB', (sheet_w, sheet_h), (24, 24, 32))
    draw = ImageDraw.Draw(out)
    value = record['consensus']['over_8_percent']
    failed = record['verdict'] != 'ok'
    parity.label(draw, (pad, 6), f'{name}: {spec["description"]}')
    parity.label(draw, (pad, 22), f'{renderer} {value:.2f}% of pixels more than 8 levels from the native GPU '
                 f'consensus (bar {args.max_over_8}%, shift {args.shift})',
                 fill=(255, 120, 120) if failed else (140, 255, 140))
    y = header
    for column, (text, pixels) in enumerate((('native consensus', consensus), (renderer, capture))):
        x = pad + column * (shown[0] + pad)
        image = Image.fromarray(pixels)
        if scale != 1:
            image = image.resize(shown, Image.BOX)
        out.paste(image, (x, y + 16))
        parity.label(draw, (x, y), text + (' (shown at 1/2)' if scale != 1 else ''))
    y += shown[1] + 20
    panels = [(f'{renderer} vs consensus', parity.heat_map(consensus, delta))]
    if reference is not None:
        panels.append((f'{renderer} vs legacy-gl', parity.heat_map(reference, parity.difference(reference, capture,
                                                                                                args.shift))))
    for column, (text, pixels) in enumerate(panels):
        x = pad + column * (half[0] + pad)
        out.paste(Image.fromarray(pixels).resize(half, Image.BOX), (x, y + 16))
        parity.label(draw, (x, y), text)
    y += half[1] + 20
    for index, (rx, ry, count) in enumerate(regions):
        parity.label(draw, (pad, y + 2), f'worst #{index + 1}', fill=(255, 220, 120))
        parity.label(draw, (pad, y + 18), f'at {rx},{ry}')
        parity.label(draw, (pad, y + 34), f'{count} px')
        for column, text in enumerate(columns):
            pixels = {'consensus': consensus, renderer: capture, 'legacy-gl': reference,
                      'difference': parity.heat_map(consensus, delta)}[text]
            tile = Image.fromarray(pixels[ry:ry + crop, rx:rx + crop]).resize((zoom_to, zoom_to), Image.NEAREST)
            x = pad + 160 + column * (zoom_to + pad)
            out.paste(tile, (x, y + 16))
            parity.label(draw, (x, y), text)
        y += zoom_to + 20
    path = directory.parent / f'{name}-{renderer}.png'
    out.save(path, compress_level=6)
    return path


def table(results, renderers):
    lines = ['| Scene | ' + ' | '.join(f'{r} vs consensus' for r in renderers)
             + ' | vs legacy-gl | native GPU spread (max) | Result |',
             '| --- | ' + ' | '.join('---:' for _ in renderers) + ' | ---: | ---: | --- |']
    for name, records in results.items():
        cells, legacy, spread, verdict = [], '', '', 'ok'
        for renderer in renderers:
            record = records.get(renderer, {})
            if record.get('status') != 'ok':
                cells.append(record.get('status', ''))
                verdict = '**fail**'
                continue
            value = record['consensus']['over_8_percent']
            cells.append(f'**{value:.2f}%**' if record['verdict'] != 'ok' else f'{value:.2f}%')
            if record['verdict'] != 'ok':
                verdict = '**fail**'
            if 'legacy-gl' in record['native']:
                legacy = f'{record["native"]["legacy-gl"]["over_8_percent"]:.2f}%'
            worst = max(record['native_spread'].items(), key=lambda item: item[1]['over_8_percent'])
            spread = f'{worst[1]["over_8_percent"]:.2f}% ({worst[0]})'
        lines.append(f'| {name} | ' + ' | '.join(cells) + f' | {legacy} | {spread} | {verdict} |')
    return '\n'.join(lines)


def write_index(output, results, args, renderers, gpu, wall):
    rows = []
    for name, records in results.items():
        for renderer in renderers:
            record = records.get(renderer, {})
            ok = record.get('verdict') == 'ok'
            text = (f'{record["consensus"]["over_8_percent"]:.2f}% from the native consensus'
                    if record.get('status') == 'ok' else f'{record.get("status")}: {record.get("reason", "")}')
            image = (f'<a href="{html.escape(Path(record["sheet"]).name)}"><img loading="lazy" '
                     f'src="{html.escape(Path(record["sheet"]).name)}"></a>' if record.get('sheet') else '')
            rows.append(f'<section class="{"ok" if ok else "fail"}"><h2>{html.escape(name)} ({renderer}) '
                        f'<span>{html.escape(text)}</span></h2><p>{html.escape(parity.SCENES[name]["description"])}</p>'
                        f'{image}</section>')
    page = f'''<!doctype html><meta charset="utf-8"><title>Browser parity {html.escape(args.label)}</title>
<style>body{{background:#16161c;color:#ddd;font:14px sans-serif;margin:16px}} img{{max-width:100%}}
.fail h2 span{{color:#f88}} .ok h2 span{{color:#8d8}} section{{border-top:1px solid #444;padding:8px 0}}
h2 span{{font-size:13px;font-weight:normal;margin-left:12px}}</style>
<h1>Browser parity: {html.escape(args.label)}</h1>
<p>{len(results)} scenes against native run {html.escape(args.native_label)}; bar {args.max_over_8}% of pixels
more than 8 levels from the native GPU consensus. Browser GPU: {html.escape(gpu or "?")}; wall time {wall:.0f} s.</p>
{''.join(rows)}'''
    (output / 'index.html').write_text(page)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--label', default='latest', help='Result name under artifacts/browser-parity/')
    parser.add_argument('--native-label', default='latest',
                        help='renderer_parity result (artifacts/renderer-parity/<label>) holding the native captures; '
                             'its settings (settle frames, window size, HD, device tier) are reused')
    parser.add_argument('--renderers', default='webgl2', help='Browser renderers to draw (webgl2)')
    parser.add_argument('--scenes', help='Comma-separated scenes (default: every scene of the native run)')
    parser.add_argument('--quick', action='store_true', help='Only the everyday subset (about ten scenes)')
    parser.add_argument('--filter', help='Regular expression the scene names must match')
    parser.add_argument('--build-dir', default=str(ROOT / 'build/emscripten/client/release'),
                        help='Release web build with threaded/render-parity.js (web-render-parity)')
    parser.add_argument('--angle', default=os.environ.get('GLOB2_CHROMIUM_ANGLE', 'swiftshader'),
                        help='Chromium ANGLE backend (default swiftshader, as CI; GLOB2_CHROMIUM_ANGLE)')
    parser.add_argument('--allow-gpu', action='store_true',
                        help='Allow a hardware ANGLE backend (--angle); never alongside native GPU work')
    parser.add_argument('--settle-frames', type=int, help='Override the native run\'s settle frames')
    parser.add_argument('--shift', type=int, default=1, help='Pixels a difference may move and still match')
    parser.add_argument('--max-over-8', type=float, default=2.0,
                        help='Bar: percent of pixels more than 8 levels from the native consensus (default 2.0)')
    parser.add_argument('--jobs', type=int, default=2, help='Browsers at once (each renders on several CPU threads)')
    parser.add_argument('--timeout', type=int, default=1200, help='Seconds one scene may take (default 1200)')
    parser.add_argument('--crop', type=int, default=64)
    parser.add_argument('--crop-zoom', type=int, default=4)
    parser.add_argument('--worst-crops', type=int, default=3)
    parser.add_argument('--reanalyse', action='store_true', help='Compare the captures already under the label again')
    args = parser.parse_args()
    native, native_summary = native_run(args.native_label)
    settings = native_settings(native_summary)
    if args.angle != 'swiftshader' and not args.allow_gpu:
        raise SystemExit('--angle other than swiftshader draws on the GPU; pass --allow-gpu on a machine where no '
                         'other GPU work runs (a browser sharing the GPU with native benchmarks has hung drivers)')
    if args.settle_frames is not None:
        settings.settle_frames = args.settle_frames
    args.hidpi = settings.hidpi
    names = [name for name in native_summary['scenes'] if name in parity.SCENES]
    if args.scenes:
        wanted = args.scenes.split(',')
        names = [name for name in wanted if name in names]
        missing = sorted(set(wanted) - set(names))
        if missing:
            raise SystemExit(f'Not in native run {args.native_label}: {missing}')
    if args.quick:
        names = [name for name in names if parity.SCENES[name]['quick']]
    if args.filter:
        names = [name for name in names if re.search(args.filter, name)]
    if not names:
        raise SystemExit('No scenes selected')
    renderers = [r for r in args.renderers.split(',') if r]
    if any(r != 'webgl2' for r in renderers):
        raise SystemExit('Only webgl2 can run: the browser WebGPU device has no synchronous capture (see --help)')
    build = Path(args.build_dir).resolve()
    if not args.reanalyse and not (build / RUNTIME).exists():
        raise SystemExit(f'{build / RUNTIME} missing; run: python3 tools/dev_build.py target=web release=1 '
                         'web-render-parity web-package')
    output = OUTPUT / args.label
    output.mkdir(parents=True, exist_ok=True)
    chromium = ['--use-angle=' + args.angle] if args.angle else []
    started = time.monotonic()
    scenes = []
    for name in names:
        directory = output / name
        directory.mkdir(parents=True, exist_ok=True)
        for renderer in renderers:
            scenes.append(browser_scene(settings, name, directory, renderer))
    records = {}
    if args.reanalyse:
        previous = json.loads((output / 'summary.json').read_text()) if (output / 'summary.json').exists() else {}
        for scene in scenes:
            old = previous.get('scenes', {}).get(scene['name'], {}).get(scene['renderer'], {})
            records[(scene['name'], scene['renderer'])] = {k: v for k, v in old.items()
                                                           if k in ('gpu', 'seconds', 'backend', 'log')}
        gpu = previous.get('browser_gpu')
    else:
        port = free_port()
        server = serve(build, port)
        job = output / 'job.json'
        results_file = output / 'driver-results.json'
        job.write_text(json.dumps({'url': f'http://127.0.0.1:{port}', 'buildDir': str(build), 'runtime': RUNTIME,
                                   'chromiumArgs': chromium, 'jobs': args.jobs, 'timeoutSeconds': args.timeout,
                                   'results': str(results_file), 'scenes': scenes}, indent=1))
        print(f'{len(scenes)} browser draws, {args.jobs} at once, Chromium {" ".join(chromium)}; '
              f'settle {settings.settle_frames} frames; native {native.relative_to(ROOT)}', flush=True)
        try:
            subprocess.run(['node', str(DRIVER), str(job)], cwd=ROOT / 'platforms/web', check=True)
        finally:
            server.terminate()
            server.wait()
        gpu = None
        for result in json.loads(results_file.read_text()):
            records[(result['name'], result['renderer'])] = result
            gpu = gpu or result.get('gpu')
    results = {}
    for scene in scenes:
        name, renderer = scene['name'], scene['renderer']
        directory = output / name
        record = records.get((name, renderer), {})
        record = {key: value for key, value in record.items() if key not in ('name', 'renderer', 'code')}
        log = Path(scene['log'])
        if log.exists():
            text = log.read_text(errors='replace')
            with gzip.open(str(log) + '.gz', 'wt') as packed:
                packed.write(text)
            log.unlink()
        elif Path(str(log) + '.gz').exists():
            text = gzip.open(str(log) + '.gz', 'rt').read()
        else:
            text = ''
        record['log'] = str(log.relative_to(ROOT)) + '.gz'
        backend = re.search(r'^BACKEND requested=(\S+) active=(\S+) fallback_reason=(.*)$', text, re.M)
        if backend:
            record['backend'] = backend.group(2)
        capture = Path(scene['capture'])
        if capture.exists():
            Image.open(capture).convert('RGB').save(directory / f'{renderer}.png')
            capture.unlink()
        if not (directory / f'{renderer}.png').exists():
            tail = '; '.join(text.strip().splitlines()[-3:])
            record.update(status='failed', reason=(record.pop('error', '') + ' ' + tail).strip()[-300:])
        else:
            record.pop('error', None)
            measured = compare(args, name, native, directory, renderer, record)
            if measured:
                record['sheet'] = str(sheet(args, name, directory, renderer, record, *measured).relative_to(ROOT))
        results.setdefault(name, {})[renderer] = record
        value = f'{record["consensus"]["over_8_percent"]:.2f}%' if record.get('status') == 'ok' else \
            f'{record.get("status")}: {record.get("reason", "")}'
        print(f'{name} {renderer}: {value}', flush=True)
    wall = time.monotonic() - started
    report = table(results, renderers)
    failures = sorted({name for name, records in results.items() for record in records.values()
                       if record.get('verdict') != 'ok'})
    summary = {'native_label': args.native_label, 'native_binary': native_summary.get('binary'),
               'runtime': str(build / RUNTIME), 'browser_gpu': gpu, 'chromium_args': chromium,
               'platform': platform.platform(), 'settle_frames': settings.settle_frames,
               'interval_ms': settings.interval_ms, 'window_size': settings.window_size, 'hidpi': settings.hidpi,
               'device_tier': settings.device_tier, 'hd': settings.hd, 'shift': args.shift,
               'max_over_8': args.max_over_8, 'wall_seconds': round(wall, 1), 'scenes': results,
               'failures': failures}
    (output / 'summary.json').write_text(json.dumps(summary, indent=1) + '\n')
    (output / 'report.md').write_text(report + '\n')
    write_index(output, results, args, renderers, gpu, wall)
    print('\n' + report)
    print(f'\nBrowser GPU: {gpu}\n{len(names)} scenes in {wall:.0f} s; sheets: {output.relative_to(ROOT)}/index.html')
    if failures:
        print(f'\nFAILED ({len(failures)}): ' + ', '.join(failures))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
