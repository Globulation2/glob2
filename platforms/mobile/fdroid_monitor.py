#!/usr/bin/env python3
"""Report whether F-Droid has published every ABI of the latest GitHub release."""

import datetime as dt
import json
import os
import urllib.error
import urllib.request

from android_release import ABI_CODES, PACKAGE, release_identity, version_code


def fetch_json(url, missing_ok=False):
    request = urllib.request.Request(url, headers={"Accept": "application/json", "User-Agent": "glob2-fdroid-monitor"})
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            return json.load(response)
    except urllib.error.HTTPError as error:
        if missing_ok and error.code == 404:
            return None
        raise


def status(release, package, now=None):
    now = now or dt.datetime.now(dt.timezone.utc)
    name = release_identity()["versionName"]
    tag = "v" + name
    if not release or release.get("tag_name") != tag:
        return {"state": "not-released", "tag": tag, "missing_codes": []}
    published = dt.datetime.fromisoformat(release["published_at"].replace("Z", "+00:00"))
    expected = {version_code(arch) for arch in ABI_CODES}
    available = {entry.get("versionCode") for entry in (package or {}).get("packages", [])
                 if entry.get("versionName") == name}
    missing = sorted(expected - available)
    if not missing:
        state = "complete"
    elif now - published >= dt.timedelta(hours=72):
        state = "overdue"
    else:
        state = "pending"
    return {"state": state, "tag": tag, "missing_codes": missing}


def main():
    release = fetch_json("https://api.github.com/repos/Globulation2/glob2/releases/latest", missing_ok=True)
    package = None
    if release and release.get("tag_name") == "v" + release_identity()["versionName"]:
        package = fetch_json("https://f-droid.org/api/v1/packages/" + PACKAGE, missing_ok=True)
    result = status(release, package)
    print(json.dumps(result, sort_keys=True))
    output = os.environ.get("GITHUB_OUTPUT")
    if output:
        with open(output, "a") as target:
            target.write("state=" + result["state"] + "\n")
            target.write("tag=" + result["tag"] + "\n")
            target.write("missing_codes=" + ",".join(map(str, result["missing_codes"])) + "\n")


if __name__ == "__main__":
    main()
