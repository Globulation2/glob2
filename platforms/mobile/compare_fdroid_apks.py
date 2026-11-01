#!/usr/bin/env python3
"""Compare a GitHub validation APK with the same ABI built by F-Droid."""

import argparse
import hashlib
import json
from dev_paths import android_sdk
from pathlib import Path
import zipfile

from android_release import ABI_CODES, ROOT, verify_apk


def contents(path):
    with zipfile.ZipFile(path) as archive:
        return {name: hashlib.sha256(archive.read(name)).hexdigest()
                for name in archive.namelist() if not name.endswith("/")}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", choices=tuple(ABI_CODES), required=True)
    parser.add_argument("--github-apk", type=Path, required=True)
    parser.add_argument("--fdroid-apk", type=Path, required=True)
    parser.add_argument("--android-sdk", type=Path, default=None)
    args = parser.parse_args()
    args.android_sdk=android_sdk(ROOT,args.android_sdk)
    github_digest = verify_apk(args.github_apk, args.arch, args.android_sdk)
    fdroid_digest = verify_apk(args.fdroid_apk, args.arch, args.android_sdk)
    github = contents(args.github_apk)
    fdroid = contents(args.fdroid_apk)
    missing = sorted(github.keys() - fdroid.keys())
    extra = sorted(fdroid.keys() - github.keys())
    changed = sorted(name for name in github.keys() & fdroid.keys() if github[name] != fdroid[name])
    non_native_changed = [name for name in changed if not name.startswith("lib/")]
    native_changed = [name for name in changed if name.startswith("lib/")]
    result = {"arch": args.arch, "github_sha256": github_digest, "fdroid_sha256": fdroid_digest,
              "missing_entries": missing, "extra_entries": extra,
              "changed_non_native_entries": non_native_changed,
              "changed_native_libraries": {name: {"github_sha256": github[name],
                                                   "fdroid_sha256": fdroid[name]}
                                           for name in native_changed}}
    print(json.dumps(result, indent=2))
    if missing or extra or non_native_changed:
        parser.exit(1, "APK contents differ outside native libraries\n")
    if result["changed_native_libraries"]:
        print("Native library differences need symbol/build provenance review before tagging.")


if __name__ == "__main__":
    main()
