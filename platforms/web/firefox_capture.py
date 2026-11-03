#!/usr/bin/env python3
"""Prepare an isolated, pinned Linux Firefox test-browser capture adapter.

Juggler's document drawSnapshot omits worker WebGL canvases. Use Firefox's
compositor readback for headed visual assertions, leaving the shared installation
and game runtime untouched. See Firefox's DebuggingWebRenderScreenshots guide.
"""
import argparse
import configparser
import hashlib
import json
from pathlib import Path
import platform
import shutil
import tempfile
import zipfile


MODULE = 'chrome/juggler/content/protocol/PageHandler.js'
SOURCE_SHA256 = '53769b6f2e0307969d6dcb69216fd755ee019004d5b4f35126db726424ff3d74'
FIREFOX_VERSION = '155.0'  # Playwright 1.63.0, Firefox revision 1543.
START = '    let snapshot;\n'
END = "    if (mimeType === 'image/jpeg'"
READBACK = '''    const win = browsingContext.topChromeWindow;
    const browser = browsingContext.top.embedderElement;
    if (win.gBrowser.selectedBrowser !== browser)
      throw new Error('Compositor screenshots require the foreground tab');
    const bounds = browser.getBoundingClientRect();
    if (rect.x < 0 || rect.y < 0 || rect.right > bounds.width + 0.5 || rect.bottom > bounds.height + 0.5)
      throw new Error('Compositor screenshots require a visible viewport clip');
    const canvas = win.document.createElementNS('http://www.w3.org/1999/xhtml', 'canvas');
    canvas.width = canvasWidth;
    canvas.height = canvasHeight;
    const ctx = canvas.getContext('2d');
    ctx.scale(scale, scale);
    ctx.drawWindow(win, bounds.left + rect.x, bounds.top + rect.y, rect.width, rect.height,
      'rgb(255,255,255)', ctx.DRAWWINDOW_DRAW_CARET | ctx.DRAWWINDOW_DRAW_VIEW | ctx.DRAWWINDOW_USE_WIDGET_LAYERS);

'''


def digest(data):
    return hashlib.sha256(data).hexdigest()


def patch_handler(source, expected_sha256=SOURCE_SHA256):
    if digest(source) != expected_sha256:
        raise ValueError('Unrecognized Firefox screenshot implementation; review the pinned capture adapter')
    text = source.decode('utf-8')
    method = text.index("async ['Page.screenshot']")
    start = text.index(START, method)
    end = text.index(END, start)
    return (text[:start] + READBACK + text[end:]).encode('utf-8')


def prepare(source, output_root):
    if platform.system() != 'Linux':
        raise ValueError('Compositor capture is supported only for headed Linux test browsers')
    source, output_root = Path(source).resolve(), Path(output_root).resolve()
    configuration = configparser.ConfigParser()
    configuration.read(source / 'application.ini')
    if configuration.get('App', 'Version', fallback=None) != FIREFOX_VERSION:
        raise ValueError('Unrecognized Firefox version; install the locked Playwright browser')
    with zipfile.ZipFile(source / 'omni.ja') as archive:
        original = archive.read(MODULE)
    patched = patch_handler(original)
    identity = dict(source=str(source), version=FIREFOX_VERSION, machine=platform.machine(),
                    source_module_sha256=digest(original), patched_module_sha256=digest(patched),
                    recipe_sha256=digest(Path(__file__).read_bytes()))
    key = digest(json.dumps(identity, sort_keys=True).encode())
    destination = output_root / key
    output_root.mkdir(parents=True, exist_ok=True)
    import fcntl
    with (output_root / '.prepare.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if destination.exists():
            if json.loads((destination / 'manifest.json').read_text()) != identity:
                raise ValueError('Firefox capture cache identity mismatch')
            return destination / 'firefox' / 'firefox'
        with tempfile.TemporaryDirectory(prefix='.prepare-', dir=output_root) as temporary:
            staging = Path(temporary)
            copied = staging / 'firefox'
            shutil.copytree(source, copied, symlinks=True)
            archive_path = copied / 'omni.ja'
            with zipfile.ZipFile(archive_path) as archive, zipfile.ZipFile(copied / 'patched.ja', 'w') as output:
                for member in archive.infolist():
                    output.writestr(member, patched if member.filename == MODULE else archive.read(member.filename))
            (copied / 'patched.ja').replace(archive_path)
            (staging / 'manifest.json').write_text(json.dumps(identity, indent=2) + '\n')
            staging.rename(destination)
    return destination / 'firefox' / 'firefox'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True, help='Installed pinned Firefox directory')
    parser.add_argument('--output-root', type=Path, required=True, help='Private generated browser cache')
    arguments = parser.parse_args()
    print(prepare(arguments.source, arguments.output_root))


if __name__ == '__main__':
    main()
