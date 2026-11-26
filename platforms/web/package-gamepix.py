#!/usr/bin/env python3
"""Repackage a verified browser release package as a GamePix HTML5 upload.

GamePix hosts uploads itself, inside an iframe on its own domain: every game file
must load by a relative path, and the GamePix SDK must load before the game. The
input is the verified output of platforms/web/package-static.py, so the game
runtime is byte-identical to the browser release. Only index.html changes: the
website-root brand assets it shares with the online app are bundled under brand/,
and the SDK and platforms/web/gamepix.js are injected before its first script.
Gzip sidecars and checksums are dropped because GamePix serves the files itself.
"""

import argparse
import importlib.util
import io
from pathlib import Path
import re
import zipfile

ROOT = Path(__file__).resolve().parents[2]
SDK_URL = "https://integration.gamepix.com/sdk/v3/gamepix.sdk.js"
ADAPTER = "gamepix.js"
# Fixed timestamp so identical inputs produce an identical zip.
ZIP_TIME = (2020, 1, 1, 0, 0, 0)
# Root-absolute references index.html shares with the online app, mapped to
# (path inside the package, path inside a @glob2/design-system checkout).
SITE_ASSET = re.compile(r"""(?<=["'(])/(signin/assets/[\w.-]+|favicon-32\.png|theme\.js)(?=["')])""")
ROOT_REFERENCE = re.compile(r"""(?:\b(?:src|href)\s*=\s*["']|url\(\s*["']?)/(?!/)""")
EXTERNAL_REFERENCE = re.compile(r"""(?:\b(?:src|href)\s*=\s*["']|url\(\s*["']?)(?:https?:)?//""")
BRAND_LICENSES = {
    "licenses/brand/LICENSE-Nunito.txt": "assets/LICENSE-Nunito.txt",
    "licenses/brand/LICENSE-glob2-design-system.txt": "LICENSE",
}


def _package_static():
    spec = importlib.util.spec_from_file_location("package_static", ROOT / "platforms/web/package-static.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def site_asset(reference):
    """Where a website-root asset lives in the package and in the design system."""
    if reference == "theme.js":
        return "theme.js", "dist/bootstrap.js"
    name = reference.rsplit("/", 1)[-1]
    return "brand/" + name, "assets/" + name


def rewrite_index(html):
    """Return the GamePix entry page and the website-root assets it now bundles."""
    needed = {}

    def relative(match):
        target, source = site_asset(match.group(1))
        needed[target] = source
        return target

    html = SITE_ASSET.sub(relative, html)
    leftover = ROOT_REFERENCE.search(html)
    if leftover:
        raise ValueError("index.html keeps a root-absolute reference: " + html[leftover.start():leftover.start() + 60])
    external = EXTERNAL_REFERENCE.search(html)
    if external:
        raise ValueError("index.html loads an external resource: " + html[external.start():external.start() + 60])
    first_script = html.find("<script")
    if first_script < 0:
        raise ValueError("index.html has no script to load the SDK before")
    injection = (
        "<script>window.GamePixConfig={lifecycle:'manual'};</script>\n"
        f'<script src="{SDK_URL}"></script>\n'
        f'<script src="{ADAPTER}"></script>\n'
    )
    return html[:first_script] + injection + html[first_script:], needed


def collect(source, design_system):
    """Map every file of the GamePix upload to its bytes."""
    source, design_system = Path(source), Path(design_system)
    names = []
    for line in (source / "SHA256SUMS").read_text().splitlines():
        name = line.split("  ", 1)[1]
        if not name.endswith(".gz"):
            names.append(name)
    files = {name: (source / name).read_bytes() for name in names if name != "index.html"}
    html, needed = rewrite_index((source / "index.html").read_text(encoding="utf-8"))
    files["index.html"] = html.encode("utf-8")
    files[ADAPTER] = (ROOT / "platforms/web" / ADAPTER).read_bytes()
    for target, origin in {**needed, **BRAND_LICENSES}.items():
        if target in files:
            raise ValueError("GamePix asset collides with a release file: " + target)
        path = design_system / origin
        if not path.is_file():
            raise ValueError("Design system checkout lacks " + origin)
        files[target] = path.read_bytes()
    return files


def write_zip(files, output):
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as archive:
        for name in sorted(files):
            info = zipfile.ZipInfo(name, ZIP_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            archive.writestr(info, files[name])
    temporary = output.with_name(output.name + ".partial")
    temporary.write_bytes(buffer.getvalue())
    temporary.replace(output)


def verify(path):
    """Check an upload: entry point at the root, SDK first, only relative files."""
    with zipfile.ZipFile(path) as archive:
        names = set(archive.namelist())
        for name in names:
            if name.startswith("/") or ".." in Path(name).parts or name.endswith(".gz"):
                raise ValueError("Unexpected upload entry: " + name)
        html = archive.read("index.html").decode("utf-8")
    scripts = [(re.search(r"""\bsrc=["']([^"']+)["']""", attributes) or [None, ""])[1]
               for attributes in re.findall(r"<script\b([^>]*)>", html)]
    if len(scripts) < 4 or scripts[1] != SDK_URL or scripts[2] != ADAPTER:
        raise ValueError("The GamePix SDK and adapter must load before the game")
    if "lifecycle:'manual'" not in html.split(SDK_URL, 1)[0]:
        raise ValueError("Manual lifecycle must be configured before the SDK loads")
    without_sdk = html.replace(f'src="{SDK_URL}"', "")
    for pattern in (ROOT_REFERENCE, EXTERNAL_REFERENCE):
        match = pattern.search(without_sdk)
        if match:
            raise ValueError("index.html references a file outside the upload: " + without_sdk[match.start():match.start() + 60])
    for reference in re.findall(r"""\b(?:src|href)=["']([^"'#?]+)""", without_sdk):
        if not reference.startswith(("data:", "blob:")) and reference not in names:
            raise ValueError("index.html references a missing file: " + reference)
    for required in ("index.js", "index.wasm"):
        if not any(re.fullmatch(r"index-[0-9a-f]+\." + required.split(".")[1], name) for name in names):
            raise ValueError("Upload lacks the browser runtime " + required)
    if not any(name.startswith("assets/") and name.endswith(".data") for name in names):
        raise ValueError("Upload lacks game data packages")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source", type=Path, nargs="?", default=ROOT / "build/browser-static",
                        help="verified package-static.py output")
    parser.add_argument("--design-system", type=Path, help="checkout of Globulation2/glob2-design-system")
    parser.add_argument("--output", type=Path, help="zip to write")
    parser.add_argument("--verify", type=Path, help="check an existing GamePix zip and exit")
    arguments = parser.parse_args()
    if arguments.verify:
        verify(arguments.verify)
        print("Verified GamePix upload", arguments.verify)
        return
    if not arguments.design_system or not arguments.output:
        parser.error("--design-system and --output are required when packaging")
    _package_static().verify(arguments.source)
    write_zip(collect(arguments.source, arguments.design_system), arguments.output)
    verify(arguments.output)
    print("Wrote GamePix upload", arguments.output)


if __name__ == "__main__":
    main()
