"""Pinned per-user tools and bounded caches; build outputs remain checkout-local."""

import atexit
import hashlib
import json
import os
import platform
import shutil
import time
from pathlib import Path

BUDGET = 15 * 1024**3
_HELD = {}
_USED_HOMES = set()


def isolated():
    mode = os.environ.get("GLOB2_DEV_MODE", "shared")
    if mode not in ("shared", "isolated"):
        raise ValueError("GLOB2_DEV_MODE must be shared or isolated")
    return mode == "isolated"


def home():
    if os.environ.get("GLOB2_DEV_HOME"):
        return Path(os.environ["GLOB2_DEV_HOME"]).expanduser().resolve()
    if platform.system() == "Darwin":
        return Path.home() / "Library/Application Support/Glob2/Development"
    if os.name == "nt":
        return (
            Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData/Local"))
            / "Glob2/Development"
        )
    return (
        Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share"))
        / "glob2/development"
    )


def key(root, files, extra=None):
    digest = hashlib.sha256()
    for name in sorted(files):
        digest.update(name.encode() + b"\0" + (Path(root) / name).read_bytes())
    digest.update(json.dumps(extra, sort_keys=True).encode())
    return platform.system() + "-" + platform.machine() + "-" + digest.hexdigest()[:24]


class Lease:
    """OS-held resource lease. Stable lock files live outside prunable entries."""

    def __init__(self, path, exclusive=False, blocking=True, track_use=True):
        self.use_stamp = None
        locks = (
            Path(path).resolve().parent / ".glob2-locks"
            if isolated()
            else home() / "state/locks"
        )
        locks.mkdir(parents=True, exist_ok=True)
        name = hashlib.sha256(str(Path(path).resolve()).encode()).hexdigest()
        self.file = (locks / name).open("a+b")
        try:
            if os.name == "nt":
                import ctypes
                import msvcrt
                from ctypes import wintypes

                class Overlapped(ctypes.Structure):
                    _fields_ = [
                        ("Internal", ctypes.c_size_t),
                        ("InternalHigh", ctypes.c_size_t),
                        ("Offset", wintypes.DWORD),
                        ("OffsetHigh", wintypes.DWORD),
                        ("hEvent", wintypes.HANDLE),
                    ]

                self.overlapped = Overlapped()
                kernel = ctypes.WinDLL("kernel32", use_last_error=True)
                lock = kernel.LockFileEx
                lock.argtypes = [
                    wintypes.HANDLE,
                    wintypes.DWORD,
                    wintypes.DWORD,
                    wintypes.DWORD,
                    wintypes.DWORD,
                    ctypes.POINTER(Overlapped),
                ]
                lock.restype = wintypes.BOOL
                exclusive_lock = 0x2
                fail_immediately = 0x1
                flags = (exclusive_lock if exclusive else 0) | (
                    0 if blocking else fail_immediately
                )
                if not lock(
                    msvcrt.get_osfhandle(self.file.fileno()),
                    flags,
                    0,
                    1,
                    0,
                    ctypes.byref(self.overlapped),
                ):
                    raise ctypes.WinError(ctypes.get_last_error())
            else:
                import fcntl

                mode = fcntl.LOCK_EX if exclusive else fcntl.LOCK_SH
                fcntl.flock(
                    self.file.fileno(), mode | (0 if blocking else fcntl.LOCK_NB)
                )
        except OSError:
            self.file.close()
            raise ValueError("Shared resource is in use: " + str(path)) from None
        resource = Path(path).resolve()
        if track_use and not isolated() and resource.is_relative_to(home().resolve()):
            relative = resource.relative_to(home().resolve())
            if relative.parts and relative.parts[0] in (
                "toolchains",
                "caches",
                "dependencies",
            ):
                _USED_HOMES.add(home().resolve())
                state = home() / "state/use"
                state.mkdir(parents=True, exist_ok=True)
                self.use_stamp = (
                    state / hashlib.sha256(str(resource).encode()).hexdigest()
                )
                self.use_stamp.touch()

    def close(self):
        if not self.file.closed:
            self.file.close()
            if self.use_stamp is not None and self.use_stamp.parent.is_dir():
                self.use_stamp.touch()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()


def hold(path):
    path = Path(path).resolve()
    if path not in _HELD:
        _HELD[path] = Lease(path)
    return path


def managed(path, lease=True):
    return hold(path) if lease and not isolated() else Path(path)


def adopt(path):
    path = Path(path).expanduser().resolve()
    if not isolated() and path.is_relative_to(home().resolve()):
        relative = path.relative_to(home().resolve())
        if len(relative.parts) >= 2 and relative.parts[0] in ("caches", "dependencies"):
            hold(home() / relative.parts[0] / relative.parts[1])
        elif len(relative.parts) >= 3 and relative.parts[0] == "toolchains":
            hold(home() / relative.parts[0] / relative.parts[1] / relative.parts[2])
    return path


def mobile_tools(root, lease=True):
    if isolated():
        return Path(root) / "build/mobile-tools"
    identity = key(
        root,
        ["mobile/toolchain.json", "mobile/android-tools.json", "mobile/emulator.json"],
    )
    return managed(home() / "toolchains/mobile" / identity, lease)


def android_sdk(root, explicit=None):
    # Explicit selection retains validation; ambient SDK variables do not override pins.
    return adopt(explicit) if explicit else mobile_tools(root) / "android-sdk"


def browser_sdk(root, explicit=None, lease=True):
    if explicit:
        return adopt(explicit)
    if isolated():
        return Path(root) / "tools/browser-emsdk"
    return managed(
        home() / "toolchains/emscripten" / key(root, ["browser/toolchain.json"]), lease
    )


def cache(root, name, lease=True):
    if isolated():
        return Path(root) / "build/mobile-tools" / name
    return managed(home() / "caches" / name, lease)


def gradle_home(root):
    version = json.loads((Path(root) / "mobile/toolchain.json").read_text())["android"][
        "gradle"
    ]
    if isolated():
        return Path(root) / "build/mobile-tools/gradle-home"
    directory = cache(root, "gradle-" + version)
    from build_layout import write_if_changed

    write_if_changed(
        directory / "init.d/glob2-retention.gradle",
        """beforeSettings { settings ->
    settings.caches {
        downloadedResources.removeUnusedEntriesAfterDays = 7
        createdResources.removeUnusedEntriesAfterDays = 7
        buildCache.removeUnusedEntriesAfterDays = 7
    }
}
""",
    )
    return directory


def dependency_key(root, identity, fingerprint):
    files = ["mobile/vcpkg.json", "mobile/toolchain.json", "mobile/sdl-java.json"]
    files += [
        str(p.relative_to(root))
        for p in (Path(root) / "mobile/triplets").rglob("*.cmake")
    ]
    return key(root, files, {"identity": identity, "compiler": fingerprint})


def dependency_prefix(root, identity, fingerprint, lease=True):
    from build_layout import default_directory

    arch = {
        "arm64-v8a": "arm64",
        "armeabi-v7a": "arm",
        "x86_64": "x64",
        "arm64": "arm64",
    }[identity["arch"]]
    triplet = (
        "glob2-"
        + arch
        + "-"
        + identity["target"]
        + ("-simulator" if identity["environment"] == "simulator" else "")
    )
    if isolated():
        return Path(root) / default_directory(identity) / "vcpkg-installed" / triplet
    return managed(
        home() / "dependencies" / dependency_key(root, identity, fingerprint), lease
    )


def android_java_command(sdk, tool, env):
    """Invoke pinned SDK Java tools without the upstream launchers' path-space bugs."""
    import shlex

    sdk = Path(sdk)
    tools = sdk / "cmdline-tools/19.0"
    classes = {
        "sdkmanager": ("com.android.sdklib.tool.sdkmanager.SdkManagerCli", "sdklib"),
        "avdmanager": ("com.android.sdklib.tool.AvdManagerCli", "sdkmanager"),
    }
    main, property_name = classes[tool]
    java = Path(env["JAVA_HOME"]) / "bin/java" if env.get("JAVA_HOME") else Path("java")
    return [
        str(java),
        "-Dcom.android." + property_name + ".toolsdir=" + str(tools),
        *shlex.split(env.get("JAVA_OPTS", "")),
        *shlex.split(env.get(tool.upper() + "_OPTS", "")),
        "-classpath",
        str(tools / "lib" / (tool + "-classpath.jar")),
        main,
    ]


def paths(root):
    tools = mobile_tools(root, False)
    lock = json.loads((Path(root) / "mobile/android-tools.json").read_text())
    jdk = lock.get("jdk-" + platform.system() + "-" + platform.machine())
    return {
        "home": str(home()),
        "mode": "isolated" if isolated() else "shared",
        "mobile_tools": str(tools),
        "android_sdk": str(tools / "android-sdk"),
        "java_home": str(tools / jdk["directory"] / jdk.get("java_home", "."))
        if jdk
        else "",
        "gradle": str(tools / lock["gradle"]["directory"] / "bin/gradle"),
        "browser_sdk": str(browser_sdk(root, lease=False)),
        "budget_bytes": BUDGET,
    }


def size(path):
    if Path(path).is_symlink():
        return 0
    total = 0
    for base, dirs, files in os.walk(path, followlinks=False):
        dirs[:] = [d for d in dirs if not (Path(base) / d).is_symlink()]
        for name in files:
            p = Path(base) / name
            if not p.is_symlink():
                try:
                    total += (
                        p.stat().st_blocks * 512
                        if hasattr(p.stat(), "st_blocks")
                        else p.stat().st_size
                    )
                except FileNotFoundError:
                    pass
    return total


def entries():
    result = []
    for category in ("caches", "dependencies"):
        parent = home() / category
        if not parent.exists():
            continue
        for p in parent.iterdir():
            if p.name.startswith(".") or p.is_symlink() or not p.is_dir():
                continue
            stamp = (
                home()
                / "state/use"
                / hashlib.sha256(str(p.resolve()).encode()).hexdigest()
            )
            result.append(
                (
                    p,
                    size(p),
                    stamp.stat().st_mtime if stamp.exists() else p.stat().st_mtime,
                )
            )
    return result


def gradle_busy(path):
    for log in (path / "daemon").glob("**/daemon-*.out.log"):
        try:
            pid = int(log.name.split("-")[1].split(".")[0])
            os.kill(pid, 0)
            return True
        except (ProcessLookupError, ValueError):
            pass
        except PermissionError:
            return True
    return False


def space_free_alias(path):
    """Stable POSIX alias for third-party makefiles which do not quote SDK paths."""
    path = Path(path).resolve()
    if not any(character.isspace() for character in str(path)):
        return path
    if os.name == "nt":
        raise ValueError(
            "This dependency builder requires a space-free SDK override on Windows"
        )
    import stat

    directory = Path("/tmp") / ("glob2-dev-paths-" + str(os.getuid()))
    directory.mkdir(mode=0o700, exist_ok=True)
    info = directory.lstat()
    if (
        not stat.S_ISDIR(info.st_mode)
        or info.st_uid != os.getuid()
        or info.st_mode & 0o077
    ):
        raise ValueError("Unsafe SDK alias directory: " + str(directory))
    alias = directory / hashlib.sha256(str(path).encode()).hexdigest()[:32]
    if alias.is_symlink():
        if alias.resolve() != path:
            raise ValueError("SDK alias points to another installation")
    else:
        try:
            alias.symlink_to(path, target_is_directory=True)
        except FileExistsError:
            if not alias.is_symlink() or alias.resolve() != path:
                raise ValueError("SDK alias collision")
    return alias


def command_path(path):
    if os.name == "nt":
        import subprocess

        return subprocess.list2cmdline([str(path)])
    import shlex

    return shlex.quote(str(path))


def prune(dry_run=False, budget=BUDGET):
    """Evict complete idle entries; never unlink lock files or active resources."""
    if isolated():
        raise ValueError("Shared-store pruning requires GLOB2_DEV_MODE=shared")
    with Lease(home() / "state/prune", exclusive=True):
        return _prune_entries(dry_run, budget)


def _prune_entries(dry_run=False, budget=BUDGET):
    """Prune under the store-wide lock, shared by manual and automatic cleanup."""
    report = {
        "budget_bytes": budget,
        "before_bytes": 0,
        "after_bytes": 0,
        "removed": [],
        "busy": [],
    }
    data = entries()
    total = sum(item[1] for item in data)
    report["before_bytes"] = total
    for path, amount, stamp in sorted(data, key=lambda item: item[2]):
        if total <= budget:
            break
        # Gradle owns its journal and cleanup; never remove individual cache files.
        # A complete idle Gradle user home is regenerable, but respect live daemons.
        if path.name.startswith("gradle-") and gradle_busy(path):
            report["busy"].append(str(path))
            continue
        try:
            with Lease(path, exclusive=True, blocking=False, track_use=False):
                if not dry_run:
                    shutil.rmtree(path)
                total -= amount
                report["removed"].append({"path": str(path), "bytes": amount})
        except ValueError:
            report["busy"].append(str(path))
    report["after_bytes"] = total
    return report


def finish():
    used = home().resolve() in _USED_HOMES
    for lease in list(_HELD.values()):
        lease.close()
    _HELD.clear()
    if not used or isolated() or not home().exists():
        return
    try:
        with Lease(home() / "state/prune", exclusive=True, blocking=False):
            stamp = home() / "state/last-prune"
            if stamp.exists() and time.time() - stamp.stat().st_mtime < 86400:
                return
            result = _prune_entries()
            stamp.touch()
            if result["after_bytes"] > BUDGET:
                import sys

                print(
                    "Glob2 cache budget temporarily exceeded; active resources were retained.",
                    file=sys.stderr,
                )
    except (OSError, ValueError):
        pass


atexit.register(finish)
