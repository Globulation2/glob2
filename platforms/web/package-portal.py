#!/usr/bin/env python3
"""Repackage a verified browser release package as a web-portal HTML5 upload.

Portals (GamePix, CrazyGames, Newgrounds, Game Jolt, GameDistribution, Yandex
Games, Poki) host uploads themselves, inside an iframe on their own domain: every
game file must load by a relative path, and a portal SDK must load before the
game. The input is the verified output of platforms/web/package-static.py, so the
game runtime is byte-identical to the browser release. Only index.html changes:
the website-root brand assets it shares with the online app are bundled under
brand/, and the portal policy (window.GLOB2_PORTAL), any SDK configuration, the
SDK, platforms/web/portal-core.js and the portal's adapter are injected before its
first script, after a style that keeps taps and long presses from selecting text
or opening the magnifier and callout and stops the page itself from panning, and
the viewport meta gains user-scalable=no (CrazyGames and GamePix rules). The policy's `ads` field marks the uploads whose adapter shows the
portal's ads at natural breaks between matches. Gzip sidecars and checksums are dropped because portals serve the
files themselves. Packaging fails when the upload exceeds the portal's documented
size or file-count limit.
"""

import argparse
from dataclasses import dataclass, field
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[2]
WEB = ROOT / "platforms/web"
CORE = "portal-core.js"
MB = 1000 * 1000
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
GAME_ID_PLACEHOLDER = "00000000000000000000000000000000"
# Every portal page: no text selection, magnifier or callout on a long press
# (CrazyGames technical requirements), no page panning or zoom by touch (GamePix
# submission guidelines). The game's DOM text fields (shell.html) stay selectable.
PAGE_STYLE = ("<style>body{-webkit-user-select:none;-moz-user-select:none;-ms-user-select:none;user-select:none;"
              "-webkit-touch-callout:none;-ms-touch-action:none;touch-action:none}"
              "input,textarea{-webkit-user-select:text;-moz-user-select:text;-ms-user-select:text;"
              "user-select:text;-webkit-touch-callout:default}</style>\n")
VIEWPORT = re.compile(r"""<meta\s+name=["']viewport["']\s+content=["']([^"']*)["']\s*/?>""")
NO_ZOOM = ("maximum-scale=1", "user-scalable=no")


@dataclass(frozen=True)
class Portal:
    """What one portal's upload contains and the limits it must meet."""

    name: str
    title: str
    # Scripts injected before the game's first script, in order. "SDK" stands for
    # sdk_url; every other entry is a file from platforms/web bundled at the root.
    scripts: tuple
    external_links: bool
    online_sign_in: bool
    sources: tuple
    sdk_url: str = None
    # JavaScript run in the policy script, before the SDK loads.
    configuration: str = ""
    # Upload limits; None where the portal documents none.
    max_bytes: int = None
    max_files: int = None
    # Measured against the zip, or against the files once unpacked.
    size_basis: str = "zip"
    # What the player may download before the game is interactive. The packager
    # cannot see the runtime's load order, so this is documented, not enforced.
    first_download_bytes: int = None
    # {path inside the upload: path relative to the repository root}
    extra_files: dict = field(default_factory=dict)
    # Environment variable holding the portal's game ID, which the SDK needs.
    game_id_env: str = None
    # Pattern every file path in the upload must match, where the portal restricts it.
    file_name: str = None
    # Whether the upload shows the portal's ads at natural breaks between matches
    # (portal-core.js createBreaks). True only where the portal requires or expects
    # ads; our own site, itch, the stores, Game Jolt and Newgrounds never carry them.
    ads: bool = False

    @property
    def adapters(self):
        return tuple(script for script in self.scripts if script != "SDK")

    @property
    def policy(self):
        return {"name": self.name, "externalLinks": self.external_links, "onlineSignIn": self.online_sign_in,
                "ads": self.ads}


PORTALS = {portal.name: portal for portal in (
    Portal(
        "gamepix", "GamePix",
        sdk_url="https://integration.gamepix.com/sdk/v3/gamepix.sdk.js",
        configuration="window.GamePixConfig={lifecycle:'manual'};",
        scripts=("SDK", CORE, "gamepix.js"),
        external_links=False, online_sign_in=False, ads=True,
        # Not in the published docs; the Testing Toolkit and dashboard reject
        # larger zips.
        max_bytes=200 * MB,
        sources=("https://partners.gamepix.com/sdk/doc/javascript",
                 "https://partners.gamepix.com/guidelines/submission"),
    ),
    Portal(
        "crazygames", "CrazyGames",
        sdk_url="https://sdk.crazygames.com/crazygames-sdk-v3.js",
        scripts=("SDK", CORE, "crazygames.js"),
        external_links=False, online_sign_in=False, ads=True,
        max_bytes=250 * MB, max_files=1500, size_basis="unpacked",
        first_download_bytes=50 * MB,
        sources=("https://docs.crazygames.com/requirements/technical/",
                 "https://docs.crazygames.com/sdk/intro/",
                 "https://docs.crazygames.com/sdk/game/",
                 "https://docs.crazygames.com/sdk/video-ads/",
                 "https://docs.crazygames.com/requirements/ads/"),
    ),
    Portal(
        "newgrounds", "Newgrounds",
        scripts=(CORE,),
        # Online play stays off: the game would reach app.glob2.online from the
        # portal's origin, and the online service sends no CORS headers (its
        # preflight answers 404), so Play online could never connect there.
        external_links=True, online_sign_in=False,
        # About 250 MB; no official page we could read states it, so the figure
        # is unconfirmed (docs/releases/newgrounds.md).
        max_bytes=250 * MB,
        sources=("https://www.newgrounds.com/wiki/help-information/content-submission/games-and-movies",),
    ),
    Portal(
        "gamejolt", "Game Jolt",
        scripts=(CORE,),
        # Online play off for the same reason as Newgrounds.
        external_links=True, online_sign_in=False,
        # Game Jolt documents no size or file-count limit for browser builds.
        # Game Jolt publishes no browser-build limits (its help docs have no
        # article on them); a 132 MB, 494-file upload was accepted on 2026-11-26.
        sources=("https://gamejolt.com/help-docs",),
    ),
    Portal(
        "gamedistribution", "GameDistribution",
        sdk_url="https://html5.api.gamedistribution.com/main.min.js",
        configuration="window.GD_OPTIONS={gameId:%(game_id)s};",
        scripts=(CORE, "gamedistribution.js", "SDK"),
        external_links=False, online_sign_in=False, ads=True,
        game_id_env="GLOB2_GAMEDISTRIBUTION_GAME_ID",
        # GameDistribution documents no size or file-count limit.
        sources=("https://github.com/GameDistribution/GD-HTML5/wiki/SDK-Implementation",),
    ),
    Portal(
        "yandex", "Yandex Games",
        sdk_url="/sdk.js",
        scripts=("SDK", CORE, "yandex.js"),
        external_links=False, online_sign_in=False, ads=True,
        # Requirement 1.21: at most 100 MB uncompressed; 1.22: one index.html at
        # the root and no spaces or Cyrillic in file names.
        max_bytes=100 * MB, size_basis="unpacked",
        file_name=r"[A-Za-z0-9._/-]+",
        sources=("https://yandex.com/dev/games/doc/en/concepts/requirements",
                 "https://yandex.com/dev/games/doc/en/sdk/sdk-about",
                 "https://yandex.com/dev/games/doc/en/sdk/sdk-game-events",
                 "https://yandex.com/dev/games/doc/en/sdk/sdk-environment",
                 "https://yandex.com/dev/games/doc/en/sdk/sdk-adv"),
    ),
    Portal(
        "poki", "Poki",
        sdk_url="https://game-cdn.poki.com/scripts/v2/poki-sdk.js",
        scripts=("SDK", CORE, "poki.js"),
        external_links=False, online_sign_in=False, ads=True,
        # Poki states no limit, only that players leave after ten seconds of
        # loading; the full package is too large, so Poki gets a small build.
        sources=("https://developers.poki.com/guide/sdk-html5",
                 "https://developers.poki.com/guide/requirements-quality"),
    ),
)}


def _package_static():
    spec = importlib.util.spec_from_file_location("package_static", WEB / "package-static.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def portal(name):
    try:
        return PORTALS[name]
    except KeyError:
        raise ValueError(f"Unknown portal {name!r}; expected one of {', '.join(sorted(PORTALS))}") from None


def game_id(profile, value=None):
    """The portal game ID from the flag, the environment or the placeholder."""
    if not profile.game_id_env:
        return None
    value = value or os.environ.get(profile.game_id_env) or GAME_ID_PLACEHOLDER
    if not re.fullmatch(r"[0-9A-Za-z-]{1,64}", value):
        raise ValueError(f"Invalid {profile.title} game ID: {value!r}")
    return value


def site_asset(reference):
    """Where a website-root asset lives in the package and in the design system."""
    if reference == "theme.js":
        return "theme.js", "dist/bootstrap.js"
    name = reference.rsplit("/", 1)[-1]
    return "brand/" + name, "assets/" + name


def head_scripts(profile, identifier=None):
    """The scripts injected before the game's first script."""
    configuration = profile.configuration % {"game_id": json.dumps(identifier)} if profile.game_id_env else profile.configuration
    policy = json.dumps(profile.policy, separators=(",", ":"))
    lines = [f"<script>window.GLOB2_PORTAL={policy};{configuration}</script>\n"]
    for script in profile.scripts:
        lines.append(f'<script src="{profile.sdk_url if script == "SDK" else script}"></script>\n')
    return "".join(lines)


def without_zoom(html):
    """index.html with user-scalable=no in its viewport meta, adding one if it has none."""
    match = VIEWPORT.search(html)
    if not match:
        content = ",".join(("width=device-width", "initial-scale=1") + NO_ZOOM)
        head = html.find("<script")
        return html[:head] + f'<meta name="viewport" content="{content}">\n' + html[head:]
    keys = [part.strip() for part in match.group(1).split(",") if part.strip()]
    keys = [key for key in keys if key.split("=", 1)[0].strip() not in ("maximum-scale", "user-scalable")]
    content = ",".join(keys + list(NO_ZOOM))
    return html[:match.start(1)] + content + html[match.end(1):]


def rewrite_index(html, profile, identifier=None):
    """Return the portal entry page and the website-root assets it now bundles."""
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
    if html.find("<script") < 0:
        raise ValueError("index.html has no script to load the portal scripts before")
    html = without_zoom(html)
    first_script = html.find("<script")
    return html[:first_script] + PAGE_STYLE + head_scripts(profile, identifier) + html[first_script:], needed


def collect(source, design_system, profile, identifier=None):
    """Map every file of the portal upload to its bytes."""
    source, design_system = Path(source), Path(design_system)
    names = []
    for line in (source / "SHA256SUMS").read_text().splitlines():
        name = line.split("  ", 1)[1]
        if not name.endswith(".gz"):
            names.append(name)
    files = {name: (source / name).read_bytes() for name in names if name != "index.html"}
    html, needed = rewrite_index((source / "index.html").read_text(encoding="utf-8"), profile,
                                 game_id(profile, identifier))
    files["index.html"] = html.encode("utf-8")
    bundled = {script: WEB / script for script in profile.adapters}
    bundled.update({target: ROOT / origin for target, origin in profile.extra_files.items()})
    bundled.update({target: design_system / origin for target, origin in {**needed, **BRAND_LICENSES}.items()})
    for target, path in bundled.items():
        if target in files:
            raise ValueError(f"{profile.title} file collides with a release file: {target}")
        if not path.is_file():
            raise ValueError(f"Missing {target} source: {path}")
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


def measure(path):
    """Zip size, unpacked size and file count of an upload."""
    with zipfile.ZipFile(path) as archive:
        entries = [info for info in archive.infolist() if not info.is_dir()]
    return {"zip": Path(path).stat().st_size, "unpacked": sum(info.file_size for info in entries),
            "files": len(entries)}


def check_limits(path, profile):
    sizes = measure(path)
    size = sizes[profile.size_basis]
    if profile.max_bytes is not None and size > profile.max_bytes:
        raise ValueError(f"Upload is {size} bytes {profile.size_basis}; "
                         f"{profile.title} accepts at most {profile.max_bytes}")
    if profile.max_files is not None and sizes["files"] > profile.max_files:
        raise ValueError(f"Upload has {sizes['files']} files; {profile.title} accepts at most {profile.max_files}")
    return sizes


def verify(path, profile):
    """Check an upload: limits, entry point at the root, portal scripts first, only relative files."""
    check_limits(path, profile)
    with zipfile.ZipFile(path) as archive:
        names = set(archive.namelist())
        for name in names:
            if name.startswith("/") or ".." in Path(name).parts or name.endswith(".gz"):
                raise ValueError("Unexpected upload entry: " + name)
            if profile.file_name and not re.fullmatch(profile.file_name, name):
                raise ValueError(f"{profile.title} rejects the file name {name!r}")
        html = archive.read("index.html").decode("utf-8")
    blocks = re.findall(r"<script\b([^>]*)>(.*?)</script>", html, re.S)
    scripts = [(re.search(r"""\bsrc=["']([^"']+)["']""", attributes) or [None, ""])[1] for attributes, _ in blocks]
    expected = [profile.sdk_url if script == "SDK" else script for script in profile.scripts]
    if len(scripts) <= len(expected) + 1 or scripts[0] or scripts[1:len(expected) + 1] != expected:
        raise ValueError(f"The {profile.title} policy, SDK and adapters must load before the game")
    policy = json.dumps(profile.policy, separators=(",", ":"))
    if f"window.GLOB2_PORTAL={policy};" not in blocks[0][1]:
        raise ValueError(f"index.html must set the {profile.title} portal policy first")
    if profile.configuration and profile.configuration.split("%", 1)[0] not in blocks[0][1]:
        raise ValueError(f"The {profile.title} SDK configuration must run before the SDK loads")
    if PAGE_STYLE not in html:
        raise ValueError("index.html lacks the portal page style")
    viewport = VIEWPORT.search(html)
    if not viewport or not all(key in viewport.group(1).replace(" ", "") for key in NO_ZOOM):
        raise ValueError("index.html must set user-scalable=no in its viewport meta")
    without_sdk = html.replace(f'src="{profile.sdk_url}"', "") if profile.sdk_url else html
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


def summary(path, profile):
    """One line for the CI step summary."""
    sizes = measure(path)
    limits = []
    if profile.max_bytes is not None:
        limits.append(f"{profile.max_bytes / MB:g} MB {profile.size_basis}")
    if profile.max_files is not None:
        limits.append(f"{profile.max_files} files")
    return (f"{profile.title} upload: `{Path(path).name}` {sizes['zip'] / MB:.1f} MB zip, "
            f"{sizes['unpacked'] / MB:.1f} MB unpacked, {sizes['files']} files "
            f"(limit: {', '.join(limits) or 'none documented'})")


def main(argv=None, default_portal=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source", type=Path, nargs="?", default=ROOT / "build/browser-static",
                        help="verified package-static.py output")
    if default_portal is None:
        parser.add_argument("--portal", required=True, choices=sorted(PORTALS))
    parser.add_argument("--design-system", type=Path, help="checkout of Globulation2/glob2-design-system")
    parser.add_argument("--output", type=Path, help="zip to write")
    parser.add_argument("--verify", type=Path, help="check an existing upload zip and exit")
    parser.add_argument("--game-id", help="portal game ID (GameDistribution; default: "
                        "$GLOB2_GAMEDISTRIBUTION_GAME_ID, else a placeholder)")
    parser.add_argument("--summary", action="store_true", help="print a one-line size summary")
    arguments = parser.parse_args(argv)
    profile = portal(default_portal or arguments.portal)
    if arguments.verify:
        verify(arguments.verify, profile)
        print(f"Verified {profile.title} upload", arguments.verify)
        if arguments.summary:
            print(summary(arguments.verify, profile))
        return
    if not arguments.design_system or not arguments.output:
        parser.error("--design-system and --output are required when packaging")
    identifier = game_id(profile, arguments.game_id)
    if identifier == GAME_ID_PLACEHOLDER:
        print(f"warning: {profile.title} game ID not set; using the placeholder {identifier}", file=sys.stderr)
    _package_static().verify(arguments.source)
    write_zip(collect(arguments.source, arguments.design_system, profile, identifier), arguments.output)
    verify(arguments.output, profile)
    print(f"Wrote {profile.title} upload", arguments.output)
    if arguments.summary:
        print(summary(arguments.output, profile))


if __name__ == "__main__":
    main()
