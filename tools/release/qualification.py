#!/usr/bin/env python3
"""Validate reviewed release evidence; never infer approval from a successful build."""

import datetime
import re
from urllib.parse import parse_qs, urlsplit


PLATFORMS = {
    "windows": ("signed", "tested"),
    "macos": ("signed", "notarized", "tested"),
    "linux": ("tested",),
    "android": ("signed", "tested"),
    "ios": ("tested",),
}
COMPATIBILITY = ("simulationChecksumsMatch", "saveLoad", "replayNetwork", "onlinePlay")
TESTED_TARGETS = (
    "windows-x86_64", "macos-arm64", "macos-x86_64",
    "linux-flatpak", "linux-snap", "linux-tar.gz", "linux-rpm",
    "android-arm64", "android-armv7", "android-x86_64",
    "ios-iphone", "ios-ipad",
)


def https_url(value):
    if not isinstance(value, str):
        raise ValueError("URL must be a string")
    parsed = urlsplit(value)
    if parsed.scheme != "https" or not parsed.hostname or parsed.username or parsed.password or parsed.port:
        raise ValueError("URL must use HTTPS without credentials or an explicit port")
    return parsed


def validate(evidence, tag, source_commit, digests):
    """Return the small public gate summary after all private evidence gates pass."""
    if not isinstance(evidence, dict) or evidence.get("schemaVersion") != 1:
        raise ValueError("qualification schemaVersion must be 1")
    if evidence.get("tag") != tag or evidence.get("sourceCommit") != source_commit:
        raise ValueError("qualification tag/sourceCommit does not match selected source")
    if not isinstance(evidence.get("reviewedBy"), str) or not evidence["reviewedBy"].strip():
        raise ValueError("qualification requires a named reviewer")
    try:
        reviewed_at = datetime.datetime.fromisoformat(evidence["reviewedAt"].replace("Z", "+00:00"))
        if reviewed_at.tzinfo is None or reviewed_at > datetime.datetime.now(datetime.timezone.utc):
            raise ValueError("invalid review time")
    except (KeyError, TypeError, AttributeError, ValueError):
        raise ValueError("qualification reviewedAt must be a past ISO timestamp with timezone") from None
    https_url(evidence.get("evidenceUrl"))
    platforms = evidence.get("platforms")
    compatibility = evidence.get("compatibility")
    if not isinstance(platforms, dict) or not isinstance(compatibility, dict):
        raise ValueError("qualification platforms and compatibility must be objects")
    for platform, gates in PLATFORMS.items():
        assertions = platforms.get(platform, {})
        if not isinstance(assertions, dict):
            raise ValueError("platform qualification must be an object")
        for gate in gates:
            if assertions.get(gate) is not True:
                raise ValueError(f"qualification requires {platform}.{gate}")
    for gate in COMPATIBILITY:
        if compatibility.get(gate) is not True:
            raise ValueError(f"qualification requires compatibility.{gate}")
    targets = evidence.get("testedTargets")
    if not isinstance(targets, dict):
        raise ValueError("qualification testedTargets must be an object")
    for target in TESTED_TARGETS:
        if targets.get(target) is not True:
            raise ValueError(f"qualification requires testedTargets.{target}")
    attested = evidence.get("artifacts", {})
    if not isinstance(attested, dict) or attested != digests:
        raise ValueError("qualification artifact checksums must match every final release file exactly")
    stores = evidence.get("stores", {})
    if not isinstance(stores, dict):
        raise ValueError("qualification requires production store availability")
    public_stores = {}
    for channel in ("googlePlay", "appStore"):
        store = stores.get(channel, {})
        if not isinstance(store, dict) or store.get("production") is not True:
            raise ValueError(f"qualification requires {channel} production availability")
        parsed = https_url(store.get("url"))
        if channel == "googlePlay":
            valid = parsed.hostname == "play.google.com" and parsed.path == "/store/apps/details" and parse_qs(parsed.query).get("id") == ["org.globulation2.glob2"]
        else:
            valid = parsed.hostname == "apps.apple.com" and re.search(r"/id[0-9]+/?$", parsed.path)
        if not valid or parsed.fragment:
            raise ValueError(f"qualification requires a valid {channel} production listing URL")
        public_stores[channel] = {"url": store["url"], "production": True}
    return {"qualified": True, "sourceCommit": source_commit, "stores": public_stores}
